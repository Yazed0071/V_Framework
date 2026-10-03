#pragma once

#include <cstdint>

bool EnsureDeployMissionCaptureInstalled();
void BeginDeployMissionCapture();
bool EndDeployMissionCapture(std::uint8_t* record);
void Uninstall_DeployControllerImpl_RegisterDeployMissionParam_Capture();
