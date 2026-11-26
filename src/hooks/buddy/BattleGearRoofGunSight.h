#pragma once

// riding the Battle Gear with its roof gun picked, the reticle's blocked-shot test runs from the roof gun's muzzle to where its
// rounds go, not from the D-Walker's own gun point inside the hull
namespace battleGearRoofGunSight
{
    bool Install();
    void Uninstall();
}
