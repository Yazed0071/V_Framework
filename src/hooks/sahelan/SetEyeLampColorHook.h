#pragma once

#include <cstdint>

bool Install_SetEyeLampColor_Hook();
bool Uninstall_SetEyeLampColor_Hook();

void Set_EyeLampColor(int mode, float r, float g, float b, float pulseSpeed);

void Clear_EyeLampColor();

void Set_EyeLampDisco(bool enabled, float speed);

void Set_HeartLightColor(float r, float g, float b, float pulseSpeed);
void Clear_HeartLightColor();

void Set_EyeLampColorLogging(bool enabled);
bool Is_EyeLampColorLogging();
