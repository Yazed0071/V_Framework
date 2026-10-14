#pragma once

#include <cstdint>

bool Install_ThrowingSystemImpl_ExecuteSerially_Hook();
void Uninstall_ThrowingSystemImpl_ExecuteSerially_Hook();

enum class ThrowingRequestKind : std::uint8_t
{
    Activate,
    Deactivate,
    SetLifeLeft,
    Stop,
    OverwriteVelocity,
    UpdateActionGrenade,
    UpdateActionSmoke,
    UpdateActionMolotovCocktail,
    UpdateActionDecoy,
    UpdateActionKibidango,
    UpdateActionFakeSign,
    AddReboundNoise,
    PostReboundSound,
    CreateEffectLanding,
    CreateEffectDropInWater,
};

struct ThrowingRequest
{
    ThrowingRequestKind kind = ThrowingRequestKind::Activate;
    std::uint32_t handle  = 0;
    std::uint16_t equipId = 0;   // real id; the live engine id is re-read when applied
    int           option  = 0;
    float         values[4] = {};
};

bool ThrowingSystem_QueueRequest(const ThrowingRequest& request);
