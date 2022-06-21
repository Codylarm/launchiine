#include "GameList.h"
#include "Application.h"
#include "common/common.h"
#include "fs/CFile.hpp"
#include "fs/FSUtils.h"
#include "utils/AsyncExecutor.h"
#include "utils/StringTools.h"
#include "utils/image.h"
#include "utils/logger.h"
#include "utils/vwii.h"
#include <algorithm>
#include <cinttypes>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <iomanip>
#include <iostream>
#include <nn/acp/nn_acp_types.h>
#include <nn/acp/title.h>
#include <sstream>
#include <string.h>
#include <string>

std::string gamesCache = "fs:/vol/external01/wiiu/men/men.cache";

static std::string getTitleidAsString(uint64_t titleId) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(16) << std::hex << titleId;
    return ss.str();
}

GameList::GameList() : iconvWii(Resources::GetFile("iconvWii.png"), Resources::GetFileSize("iconvWii.png"), GX2_TEX_CLAMP_MODE_MIRROR) {
    FSUtils::CreateSubfolder("fs:/vol/external01/wiiu//men/icon");
}

GameList::~GameList() {
    loadAbort();
    clear();
};

void GameList::clear() {
    std::lock_guard guard(_lock);
    for (auto const &x : fullGameList) {
        if (x != nullptr) {
            if (x->imageData != nullptr) {
                AsyncExecutor::pushForDelete(x->imageData);
                x->imageData = nullptr;
            }
            delete x;
        }
    }
    fullGameList.clear();
    //! Clear memory of the vector completely
    std::vector<gameInfo *>().swap(fullGameList);
}

gameInfo *GameList::getGameInfo(uint64_t titleId) {
    std::lock_guard guard(_lock);

    for (uint32_t i = 0; i < fullGameList.size(); ++i) {
        if (titleId == fullGameList[i]->titleId) {
            return fullGameList[i];
        }
    }
    return nullptr;
}

static gameInfo *gameInfoNew(uint64_t titleId, MCPAppType appType, std::string name, std::string gamePath, GuiImageData *imageData) {
    std::string chars = "\r\n";
    for (char c : chars) {
        std::replace(name.begin(), name.end(), c, '_');
    }

    auto *newGameInfo     = new gameInfo;
    newGameInfo->titleId  = titleId;
    newGameInfo->appType  = appType;
    newGameInfo->gamePath = gamePath;
    if (name.length() != 0) {
        newGameInfo->name = name;
    } else {
        newGameInfo->name = getTitleidAsString(titleId);
    }
    newGameInfo->imageData = imageData;
    DCFlushRange(newGameInfo, sizeof(gameInfo));
    return newGameInfo;
}

int GameList::add(uint64_t titleId, MCPAppType appType, std::string name, std::string gamePath, GuiImageData *imageData) {
    auto *newGameInfo = gameInfoNew(titleId, appType, name, gamePath, imageData);
    {
        std::lock_guard guard(_lock);

        // Don't add a title already present in the list
        for (auto gameInfo : fullGameList) {
            if (titleId == gameInfo->titleId) {
                return 1;
            }
        }

        fullGameList.push_back(newGameInfo);
    }
    return 0;
}

int GameList::add(struct MCPTitleListType *title) {
    return add(title->titleId, title->appType, "", title->path, nullptr);
}

int32_t GameList::readGameList() {
    int32_t cnt = 0;

    MCPError mcp = MCP_Open();
    if (mcp < 0) {
        return 0;
    }

    MCPError titleCount = MCP_TitleCount(mcp);
    if (titleCount < 0) {
        MCP_Close(mcp);
        return 0;
    }

    std::vector<struct MCPTitleListType> titles(titleCount);
    uint32_t realTitleCount = 0;

    static const std::vector<MCPAppType> menuAppTypes{
            MCP_APP_TYPE_GAME,
            MCP_APP_TYPE_GAME_WII,
            MCP_APP_TYPE_SYSTEM_APPS,
            MCP_APP_TYPE_SYSTEM_SETTINGS,
            MCP_APP_TYPE_FRIEND_LIST,
            MCP_APP_TYPE_MIIVERSE,
            MCP_APP_TYPE_ESHOP,
            MCP_APP_TYPE_BROWSER,
            MCP_APP_TYPE_DOWNLOAD_MANAGEMENT,
            MCP_APP_TYPE_ACCOUNT_APPS,
            MCP_APP_TYPE_SYSTEM_MENU,
    };

    for (auto appType : menuAppTypes) {
        uint32_t titleCountByType = 0;
        MCPError err              = MCP_TitleListByAppType(mcp, appType, &titleCountByType, titles.data() + realTitleCount,
                                                           (titles.size() - realTitleCount) * sizeof(decltype(titles)::value_type));
        if (err < 0) {
            titleCountByType = 0;
            continue;
        }
        realTitleCount += titleCountByType;
    }
    if (realTitleCount != titles.size()) {
        titles.resize(realTitleCount);
    }

    for (auto title_candidate : titles) {
        add(&title_candidate);
        cnt++;
    }

    MCP_Close(mcp);

    return cnt;
}

void GameList::updateGameNames() {
    std::lock_guard guard(_lock);
    ACPMetaXml *meta = (ACPMetaXml *) malloc(0x4000);

    for (auto newHeader : fullGameList) {
        DCFlushRange(&stopAsyncLoading, sizeof(stopAsyncLoading));
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async game names loading");
            break;
        }

        DEBUG_FUNCTION_LINE("Load extra infos of %016llX", newHeader->titleId);

        memset(meta, 0, sizeof(0x4000));
        auto acp = ACPGetTitleMetaXml(newHeader->titleId, meta);
        if (acp == ACP_RESULT_SUCCESS && meta->shortname_en != NULL && *meta->shortname_en != 0 && newHeader->name != meta->shortname_en) {
            saveNeeded      = true;
            newHeader->name = meta->shortname_en;
            DCFlushRange(newHeader, sizeof(gameInfo));
            titleUpdated(newHeader);
        }
    }
    free(meta);
}

void GameList::updateGameImages() {
    std::lock_guard guard(_lock);

    for (auto newHeader : fullGameList) {
        DCFlushRange(&stopAsyncLoading, sizeof(stopAsyncLoading));
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async game images loading");
            break;
        }

        if (newHeader->imageData != nullptr)
            continue;

        bool saveToCache      = false;
        uint8_t *buffer       = nullptr;
        uint32_t bufferSize   = 0;
        std::string filepath  = "fs:" + newHeader->gamePath + META_PATH + "/iconTex.tga";
        std::string cachepath = "fs:/vol/external01/wiiu/men/icon/" + getTitleidAsString(newHeader->titleId) + ".png";

        int iResult = FSUtils::LoadFileToMem(cachepath.c_str(), &buffer, &bufferSize);
        if (iResult <= 0) {
            iResult     = FSUtils::LoadFileToMem(filepath.c_str(), &buffer, &bufferSize);
            saveToCache = true;
        }

        if (iResult > 0) {
            auto *imageData = new GuiImageData(buffer, bufferSize, GX2_TEX_CLAMP_MODE_MIRROR);
            //! free original image buffer which is converted to texture now and not needed anymore
            free(buffer);

            newHeader->imageData = imageData;

            DCFlushRange(newHeader, sizeof(gameInfo));
            titleUpdated(newHeader);
            if (saveToCache) {
                // FSUtils::saveBufferToFile(cachepath.c_str(), buffer, bufferSize); // Save same as original

                // Save compressed image
                std::string savePath = "fs:/vol/external01/wiiu/men/icon/" + getTitleidAsString(newHeader->titleId) + ".png";
                imageConvert(filepath.c_str(), savePath.c_str());
            }
        }
    }
}


bool GameList::loadFromConfig(bool updateExistingOnly) {
    uint8_t *buffer     = nullptr;
    uint32_t bufferSize = 0;
    int iResult         = FSUtils::LoadFileToMem(gamesCache.c_str(), &buffer, &bufferSize);

    if (iResult <= 0)
        return false;

    std::string strBuffer((char *) buffer);
    free(buffer);

    // Remove '\r'
    strBuffer.erase(std::remove(strBuffer.begin(), strBuffer.end(), '\r'), strBuffer.end());

    std::vector<std::string> lines = StringTools::stringSplit(strBuffer, "\n");

    for (auto line : lines) {
        std::vector<std::string> valueSplit = StringTools::stringSplit(line, ";");
        if (valueSplit.size() < 6)
            continue;

        uint64_t titleId     = strtoll(valueSplit[0].c_str(), NULL, 16);
        MCPAppType appType   = (MCPAppType) strtoll(valueSplit[1].c_str(), NULL, 16);
        std::string gamePath = valueSplit[2];
        std::string folder   = valueSplit[3];
        int position         = atoi(valueSplit[4].c_str());
        std::string name     = valueSplit[5];

        if (updateExistingOnly) {
            setTitleName(titleId, name);
        } else {
            add(titleId, appType, name, gamePath, nullptr);
        }
    }

    return true;
}

bool GameList::setTitleName(uint64_t titleId, std::string name) {
    // TODO: Add ref counter on gameInfo?
    gameInfo *gameInfo = getGameInfo(titleId);
    if (gameInfo != nullptr) {
        std::lock_guard guard(_lock);
        gameInfo->name = name;
        return true;
    }
    return false;
}

void GameList::sortByName() {
    std::lock_guard guard(_lock);

    std::sort(fullGameList.begin(),
              fullGameList.end(),
              [](gameInfo *l, gameInfo *r) {
                  if (l != r)
                      return strcasecmp(l->name.c_str(), r->name.c_str()) < 0;

                  return l < r;
              });
}


int32_t GameList::load() {
    clear();

    add(TITLEID_VWII, MCP_APP_TYPE_SYSTEM_APPS, "vWii", "", &iconvWii);
    readGameList();

    loadFromConfig(true);
    sortByName();

    for (auto gameInfo : fullGameList) {
        titleAdded(gameInfo);
    }

    // AsyncExecutor::execute([&] { updateTitleInfo(); });
    updateGameImages();
    updateGameNames();

    save();
    return size();
}

int32_t GameList::save() {
    if (!saveNeeded)
        return 0;

    CFile file(gamesCache, CFile::WriteOnly);
    if (file.isOpen()) {
        std::lock_guard guard(_lock);
        for (int i = 0; i < (int) fullGameList.size(); i++) {
            gameInfo *game = fullGameList[i];
            // Format: titleId, appType, gamePath, folder, position, name
            file.fwrite("%.16" PRIx64 ";%.8x;%s;%s;%i;%s\n", game->titleId, (uint32_t) game->appType, game->gamePath.c_str(), "", i, game->name.c_str());
        }

        file.close();
    }

    return 0;
}
