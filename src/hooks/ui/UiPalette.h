#pragma once

#include <cstdint>

bool Install_UiPalette_Hook();
bool Uninstall_UiPalette_Hook();

namespace UiPalette
{
    bool SetColor(std::uint32_t keyHash, float r, float g, float b, float a);
    bool GetColor(std::uint32_t keyHash, float* outR, float* outG, float* outB, float* outA);
    void RestoreAll();
}
