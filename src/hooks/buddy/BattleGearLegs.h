#pragma once

#include <cstdint>

namespace battleGearLegs
{
    bool Install();
    void Uninstall();

    // the Battle Gear's walker model record (model +0x68) while it is out in the field, nullptr otherwise
    void SetFieldRecord(std::uint8_t* record);

    // runs right after the game wrote a skeleton's bone matrices for this frame
    void OnBonesWritten(std::uint8_t* skeleton);

    // true while the hull glides in the air in fire posture or its rear pods glide on the hover; point = the ground under the glide
    bool HoverGround(float* point);

    // first collision on the segment start -> end against the given collision flags
    bool FirstHit(const float* start, const float* end, std::uint32_t flags, float* hit);

    // the same, with the hit surface's normal (world, unit length)
    bool FirstHitFace(const float* start, const float* end, std::uint32_t flags, float* hit, float* face);

    // a thing standing at a spot under `ceiling`: true when its top spreads round the spot both ways (a ledge, a block, a
    // vehicle), false for what legs step over (a barrier, a hedgehog, a rock, a fence) and where nothing tops out under the ceiling
    bool SpreadsBothWays(const float* spot, float ceiling);
}
