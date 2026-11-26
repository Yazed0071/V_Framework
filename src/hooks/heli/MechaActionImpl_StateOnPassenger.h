#pragma once

#include <cstdint>

bool Install_MechaActionImpl_StateOnPassenger_Hook();
bool Uninstall_MechaActionImpl_StateOnPassenger_Hook();

enum class HeliDoorRequest : std::uint8_t
{
    None,
    Open,
    Close,
};

void HeliDoor_SetRequest(std::uint32_t heliKey, std::uint32_t side, HeliDoorRequest request);
void HeliDoor_ApplyRequest(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc);
void HeliDoor_Announce(void* mecha, std::uint32_t realizedIndex, std::uint32_t side, int proc);
void HeliDoor_Clear();
void HeliDoor_ClearForReload();   // the last door move announced, keeps the Lua door orders
