#include <algorithm>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <iomanip>
#include <iostream>
#include <nn/acp/nn_acp_types.h>
#include <nn/acp/title.h>
#include <sstream>
#include <string.h>
#include <string>

#include "GameList.h"
#include "common/common.h"
#include "utils/AsyncExecutor.h"

#include "fs/FSUtils.h"
#include "utils/logger.h"


static std::string getTitleidAsString(uint64_t titleId) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(16) << std::hex << titleId;
    return ss.str();
}

GameList::GameList() {
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
    };

    for (auto appType : menuAppTypes) {
        uint32_t titleCountByType = 0;
        MCPError err              = MCP_TitleListByAppType(mcp, appType, &titleCountByType, titles.data() + realTitleCount,
                                                           (titles.size() - realTitleCount) * sizeof(decltype(titles)::value_type));
        if (err < 0) {
            MCP_Close(mcp);
            return 0;
        }
        realTitleCount += titleCountByType;
    }
    if (realTitleCount != titles.size()) {
        titles.resize(realTitleCount);
    }

    for (auto title_candidate : titles) {
        auto *newGameInfo      = new gameInfo;
        newGameInfo->titleId   = title_candidate.titleId;
        newGameInfo->appType   = title_candidate.appType;
        newGameInfo->gamePath  = title_candidate.path;
        newGameInfo->name      = getTitleidAsString(title_candidate.titleId);
        newGameInfo->imageData = nullptr;
        DCFlushRange(newGameInfo, sizeof(gameInfo));

        {
            std::lock_guard guard(_lock);
            fullGameList.push_back(newGameInfo);
        }
        titleAdded(newGameInfo);
        cnt++;
    }

    return cnt;
}

void GameList::updateTitleInfo() {
    std::lock_guard guard(_lock);

    for (int i = 0; i < (int) fullGameList.size(); i++) {

        DCFlushRange(&stopAsyncLoading, sizeof(stopAsyncLoading));
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async title loading");
            break;
        }

        gameInfo *newHeader = fullGameList[i];

        bool hasChanged = false;

        //        if (newHeader->name.empty()) {
        DEBUG_FUNCTION_LINE("Load extra infos of %016llX", newHeader->titleId);
        auto *meta = (ACPMetaXml *) calloc(1, 0x4000); //TODO fix wut
        if (meta) {
            auto acp = ACPGetTitleMetaXml(newHeader->titleId, meta);
            if (acp >= 0) {
                newHeader->name = meta->shortname_en;
                hasChanged      = true;
            }
            free(meta);
        }
        //        }

        if (newHeader->imageData == nullptr) {
            std::string filepath = "fs:" + newHeader->gamePath + META_PATH + "/iconTex.tga";
            uint8_t *buffer      = nullptr;
            uint32_t bufferSize  = 0;
            int iResult          = FSUtils::LoadFileToMem(filepath.c_str(), &buffer, &bufferSize);

            if (iResult > 0) {
                auto *imageData      = new GuiImageData(buffer, bufferSize, GX2_TEX_CLAMP_MODE_MIRROR);
                newHeader->imageData = imageData;
                hasChanged           = true;

                //! free original image buffer which is converted to texture now and not needed anymore
                free(buffer);
            }
        }
        if (hasChanged) {
            DCFlushRange(newHeader, sizeof(gameInfo));
            titleUpdated(newHeader);
        }
    }
}

int32_t GameList::load() {
    clear();
    readGameList();

    // AsyncExecutor::execute([&] { updateTitleInfo(); });
    updateTitleInfo();

    return size();
}
