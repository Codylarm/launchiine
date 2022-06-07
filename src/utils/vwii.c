#include "vwii.h"

#include <nn/cmpt/cmpt.h>
#include <padscore/kpad.h>

// Code comes from AutobootModule
static void launchvWiiTitle(uint32_t titleId_low, uint32_t titleId_high) {
    // we need to init kpad for cmpt
    KPADInit();

    // Try to find a screen type that works
    CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_BOTH);
    if (CMPTCheckScreenState() < 0) {
        CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_DRC);
        if (CMPTCheckScreenState() < 0) {
            CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_TV);
        }
    }

    uint32_t dataSize = 0;
    CMPTGetDataSize(&dataSize);

    void *dataBuffer = memalign(0x40, dataSize);

    if (titleId_low == 0 && titleId_high == 0) {
        CMPTLaunchMenu(dataBuffer, dataSize);
    } else {
        CMPTLaunchTitle(dataBuffer, dataSize, titleId_low, titleId_high);
    }

    free(dataBuffer);
}

void bootvWiiMenu() {
    launchvWiiTitle(0, 0);
}
