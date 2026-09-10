#pragma once

#include <cstddef>

struct LaserColor
{
    float r = 1.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.5f;
};

constexpr int kLaserTweakCount = 17;

struct LaserTweaks
{
    float value[kLaserTweakCount] = {};
    bool  set[kLaserTweakCount]   = {};
};

const char* LaserTweak_NameForIndex(int index);

void LaserColor_SetDefaultForOption(int optionId, const LaserColor& color);
void LaserColor_SetForOptionEquip(int optionId, int equipId, const LaserColor& color);
void LaserTweak_SetForOption(int optionId, int index, float value);
void LaserColor_ClearOption(int optionId);

bool LaserColor_Resolve(int optionId, int equipId, LaserColor& out);
bool LaserTweaks_Resolve(int optionId, LaserTweaks& out);
bool LaserColor_AnyRegistered();
bool LaserColor_LoneDefault(LaserColor& out);

void LaserColor_NoteWeaponOption(int equipId, int optionId);

bool LaserSight_UpdatePrim_Install();
void LaserSight_UpdatePrim_Uninstall();
