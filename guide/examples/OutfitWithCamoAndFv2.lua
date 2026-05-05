--==============================================================================
-- V_FrameWork outfit example — CAMO + FV2 + DIAMOND
--==============================================================================
-- Outfit with custom camo pattern (.fpk + .fv2) and a custom diamond mark.
-- Sub-asset fields live INSIDE the per-playerType branch.
--
-- Each sub-asset accepts FOUR forms:
--   string  → custom asset path (hashed to FoxPath code64ext)
--   true    → use vanilla asset (load whatever the engine would normally
--             use for this PT/partsType). This is the DEFAULT for face /
--             skin / voice / *Fv2.
--   false   → disable: load nothing for this slot. Use this to strip out
--             the vanilla face on a no-head body, suppress a skin tone
--             override, etc.
--   nil     → per-field default (camoFpk / diamondFpk default to disabled;
--             face / skin / voice / *Fv2 default to vanilla).
--==============================================================================

function this.OnAllocate()
    V_TppPlayer.AddOutfit{
        name = "MyMod:NeonSuitCamo",                                             -- Required

        ddFemale = {
            partsPath  = "/Assets/tpp/parts/chara/neon/neon_v00.parts",           -- Required
            fpkPath    = "/Assets/tpp/pack/player/parts/neon_v00.fpk",            -- Required

            camoFpk    = "/Assets/tpp/pack/player/parts/neon_camo_v00.fpk",       -- Optional
            camoFv2    = "/Assets/tpp/fova/chara/neon/neon_camo_v00.fv2",         -- Optional

            diamondFpk = "/Assets/tpp/pack/player/parts/neon_diamond_v00.fpk",    -- Optional
            diamondFv2 = "/Assets/tpp/fova/chara/neon/neon_diamond_v00.fv2",      -- Optional

            faceFpk    = "/Assets/tpp/pack/player/parts/neon_face_v00.fpk",       -- Optional (string=custom, true=vanilla, false=disabled)
            skinFv2    = "/Assets/tpp/fova/chara/neon/neon_skin_v00.fv2",         -- Optional (string=custom, true=vanilla, false=disabled)

            enableArm  = true,                                                    -- Optional (default true)
            enableHead = false,                                                   -- Optional (default false)
        },
    }
end
