#ifndef GAME_LIST_H_
#define GAME_LIST_H_

#include <atomic>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <gui/GuiImageData.h>
#include <gui/sigslot.h>
#include <mutex>
#include <stdint.h>
#include <vector>

typedef struct _gameInfo {
    uint64_t titleId;
    MCPAppType appType;
    std::string name;
    std::string gamePath;
    GuiImageData *imageData;
} gameInfo;

class GameList {
public:
    GameList();

    ~GameList();

    int32_t size() {
        std::lock_guard guard(_lock);
        int32_t res = fullGameList.size();
        return res;
    }

    gameInfo *operator[](int32_t i) {
        std::lock_guard guard(_lock);
        if (i >= 0 && i < (int32_t) fullGameList.size()) {
            return fullGameList[i];
        }
        return nullptr;
    }

    gameInfo *getGameInfo(uint64_t titleId);

    void clear();

    int32_t load();

    void loadAbort() {
        stopAsyncLoading = true;
        DCFlushRange(&stopAsyncLoading, sizeof(stopAsyncLoading));
    }

    sigslot::signal1<gameInfo *> titleUpdated;
    sigslot::signal1<gameInfo *> titleAdded;

protected:
    int32_t readGameList();

    void updateTitleInfo();

    std::vector<gameInfo *> fullGameList;

    std::mutex _lock;

    std::atomic<bool> stopAsyncLoading = false;

private:
    GuiImageData iconvWii;

    int add(uint64_t titleId, MCPAppType appType, std::string name, std::string gamePath, GuiImageData *imageData);
    int add(struct MCPTitleListType *title);
};

#endif
