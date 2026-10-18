#include "src/devices/elecrow_crowpanel_10_1/sdmmc.h"
#include "src/io/storage/sd_card.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1)

namespace DeviceElecrowCrowPanel10 {

bool initSDCard() {
    return Storage::initSDCard();
}

bool sdReady() {
    return Storage::sdReady();
}

fs::FS& sdFS() {
    return Storage::sdFS();
}

bool suspendSDCardForNetworkTransition() {
    return Storage::suspendSDCardForNetworkTransition();
}

bool resumeSDCardAfterNetworkTransition() {
    return Storage::resumeSDCardAfterNetworkTransition();
}

}  // namespace DeviceElecrowCrowPanel10

#endif
