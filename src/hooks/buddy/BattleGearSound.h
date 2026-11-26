#pragma once

#include <cstdint>

namespace battleGearSound
{
    bool Install();
    void Uninstall();

    // the roof MG: while one is held, the walker sidearm's shot posts the event the tanks' cupola gun posts for each round
    struct SidearmShot
    {
        std::uint32_t* events = nullptr;   // the gun's four shot events: player, player suppressed, others, others suppressed
        std::uint32_t  vanilla[4] = {};
    };
    void BeforeSidearmShot(void* realizedEquipObject, std::uint32_t recordIndex, int equipId, SidearmShot& shot);
    void AfterSidearmShot(const SidearmShot& shot);

    // the rail gun: between these the stun gun state's own sound calls become the Battle Gear's charge and fire events
    void EnterRailState(const void* attackAction);
    void LeaveRailState(bool stateEnded);
    void RailImpact(const float* position);

    // each frame of the Battle Gear's walker: the rail gun's charge sounds follow its muzzle, its wind-down ends, and the tanks'
    // cupola gun traverse sound plays while the roof M2's mount turns
    void OnWalkerFrame(void* walkerSystem, std::uint32_t index, float dt);

    // the roof M2 fired a round, or its trigger was pulled empty: the tanks' cupola gun's own events
    void RoofGunShot(void* walkerSystem, std::uint32_t index, const float* position);
    void RoofGunEmpty(void* walkerSystem, std::uint32_t index, const float* position);

    // Battle Gear gone: its loops stop
    void Drop();
}
