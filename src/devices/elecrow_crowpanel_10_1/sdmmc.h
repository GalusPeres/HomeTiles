#pragma once

#include <FS.h>

namespace DeviceElecrowCrowPanel10 {

bool initSDCard();
bool sdReady();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

}  // namespace DeviceElecrowCrowPanel10
