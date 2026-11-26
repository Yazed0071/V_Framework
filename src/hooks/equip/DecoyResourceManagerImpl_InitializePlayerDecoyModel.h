#pragma once

#include <cstdint>

struct DecoyPlayerModels
{
    std::uint32_t generation = 0;   // changes every time the game builds the player decoy models
    void* controller = nullptr;
    void* effects = nullptr;
    void* block = nullptr;
    void* canister = nullptr;
    void* figure = nullptr;
    void* motion = nullptr;
};

bool Install_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook();
void Uninstall_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook();
bool DecoyResourceManagerImpl_PlayerModels(DecoyPlayerModels* out);   // false = no game models known, generation still set
