--==============================================================================
-- V_FrameWork outfit example — SIMPLE
--==============================================================================
-- The minimal correct registration. No head options, no variants, no FV2.
-- Only one body asset (parts + fpk) for one playerType.
--
-- Drop this file in your mod's Lua entry path. The framework's mod loader
-- runs OnAllocate during boot.
--==============================================================================

local outfit_registered = false

function this.OnAllocate()
    if outfit_registered then return end

    local partsType, developId, flowIndex = V_TppPlayer.AddOutfit{
        name = "MyMod:NeonSuit",                                                  -- Required (persistence key)

        ddFemale = {
            partsPath = "/Assets/tpp/parts/chara/jill/jill_def_v00.parts",         -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/jill_def_v00.fpk",          -- Required
        },
    }

    if partsType then
        V_FrameWork.Log(string.format(
            "Registered NeonSuit -> partsType=0x%02X developId=%d flowIndex=%d",
            partsType, developId, flowIndex))
        outfit_registered = true
    else
        V_FrameWork.Log("Failed to register NeonSuit")
    end
end
