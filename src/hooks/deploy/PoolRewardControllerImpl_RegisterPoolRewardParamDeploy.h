#pragma once

#include <cstdint>

bool EnsureDeployRewardCaptureInstalled();
void BeginDeployRewardCapture();
bool EndDeployRewardCapture(std::uint8_t* reward);
void Uninstall_PoolRewardControllerImpl_RegisterPoolRewardParamDeploy_Capture();
