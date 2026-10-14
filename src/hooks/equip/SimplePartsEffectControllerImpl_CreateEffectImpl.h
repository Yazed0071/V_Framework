#pragma once

#include <cstdint>

bool Install_SimplePartsEffectControllerImpl_CreateEffectImpl_Hooks();
bool Uninstall_SimplePartsEffectControllerImpl_CreateEffectImpl_Hooks();

void ShellFxDiag_NoteQuietShot(void* realizedEquipObject, std::uint32_t slot, std::uint32_t flagsIn,
                               std::uint32_t flagsOut, const std::uint8_t* gunInfo);
