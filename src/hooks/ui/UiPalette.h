#pragma once

#include <cstdint>

bool Install_UiPalette_Hook();
bool Uninstall_UiPalette_Hook();

namespace UiPalette
{
    bool SetColor(std::uint32_t keyHash, float r, float g, float b, float a);
    bool GetColor(std::uint32_t keyHash, float* outR, float* outG, float* outB, float* outA);
    bool RestoreColor(std::uint32_t keyHash);
    void RestoreAll();

    bool AnimateColor(std::uint32_t keyHash, const char* mode, double periodSec,
                      const float* rgba, int colorCount);
    bool ClearAnimation(std::uint32_t keyHash);
    void ClearAllAnimations();
}
