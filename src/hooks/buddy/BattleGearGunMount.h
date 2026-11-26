#pragma once

// The game hangs the walker's main gun model on its left mount every frame (WalkerGear_UpdateAttachmentsAndHoldPoint, from the
// body's and the mount's connect points). A registered Battle Gear main gun that names its own mount goes to the rail gun's barrel
// instead: right after the game placed the parts, its model is moved there and re-posed.
namespace battleGearGunMount
{
    void AfterPartsPlaced(void* action);   // from the walker's part placement hook, every walker frame
}
