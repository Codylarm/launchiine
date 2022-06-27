#pragma once

#include "utils/logger.h"
#include <coreinit/cache.h>
#include <future>
#include <gui/GuiElement.h>
#include <queue>
#include <thread>
#include <vector>

class AsyncExecutor {
public:
    static void execute(std::function<void()> func) {
        if (!instance) {
            instance = new AsyncExecutor();
        }
        instance->executeInternal(func);
    }

    static void destroyInstance() {
        if (instance) {
            delete instance;
            instance = nullptr;
        }
    }

private:
    static AsyncExecutor *instance;

    AsyncExecutor();

    ~AsyncExecutor();

    void executeInternal(std::function<void()> func);

    std::recursive_mutex mutex;
    std::thread *thread;
    volatile bool exitThread = false;

    std::vector<std::future<void>> elements;
};
