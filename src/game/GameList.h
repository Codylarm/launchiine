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
    std::string vdir;
    GuiImageData *imageData;
} gameInfo;

class GameList {
public:
    GameList();

    ~GameList();

    int32_t size() {
        int32_t res = fullGameList.size();
        return res;
    }

    gameInfo *operator[](int32_t i) {
        if (i >= 0 && i < (int32_t) fullGameList.size()) {
            return fullGameList[i];
        }
        return nullptr;
    }

    gameInfo *getGameInfo(uint64_t titleId);

    void clear();

    int32_t load();
    int32_t save();

    void loadAbort() {
        // Ask threads to terminate
        stopAsyncLoading = true;
        DCFlushRange(&stopAsyncLoading, sizeof(stopAsyncLoading));

        // Wait until all threads are terminated
        while (asyncLoadingWorking > 0) {
            usleep(100000);
        }
    }

    sigslot::signal1<gameInfo *> titleUpdated;
    sigslot::signal1<gameInfo *> titleAdded;

protected:
    int32_t readGameList();

    void updateTitleInfo();

    std::vector<gameInfo *> fullGameList;

    std::atomic<bool> stopAsyncLoading   = false;
    std::atomic<int> asyncLoadingWorking = 0;

private:
    GuiImageData iconvWii;
    bool saveNeeded = true;

    int add(uint64_t titleId, MCPAppType appType, std::string name, std::string gamePath, GuiImageData *imageData);
    int add(struct MCPTitleListType *title);
    bool loadFromConfig(bool updateExistingOnly = false);
    void sortByName();
    bool setTitleName(uint64_t titleId, std::string name);

    void updateGameNames();
    void updateGameImages();
};

#endif
