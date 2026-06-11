#pragma once

#include <cstdint>

bool Install_SetEyeLampColor_Hook();
bool Uninstall_SetEyeLampColor_Hook();

void Set_EyeLampColor(int mode, float r, float g, float b, float a);

void Clear_EyeLampColor();

void Set_EyeLampDisco(bool enabled, float speed, float a);

void Set_HeartLightColor(int mode, float r, float g, float b, float a);
void Clear_HeartLightColor();
void Set_HeartLightDisco(bool enabled, float speed, float a);

void Set_EyeLampColorLogging(bool enabled);
bool Is_EyeLampColorLogging();
