#pragma once

#include <cstdint>

void  MtarRegistry_ArmFromLiveArchive(void* mtar);
void* MtarRegistry_Lookup(std::uint64_t pathId);
void  MtarRegistry_SetUnloadHook(void (*fn)(void*));
void  MtarRegistry_Uninstall();
