--==============================================================================
-- V_FrameWork outfit example — CAMO + FV2 + DIAMOND
--==============================================================================
-- Outfit with custom camo pattern (.fpk + .fv2), and a custom diamond mark.
-- Demonstrates the per-sub-asset cascade: each asset can be a string path,
-- the literal `true` (use vanilla), the literal `false` (disable), or omitted
-- entirely (default — see V_FrameWork_API_Reference for per-field defaults).
--==============================================================================

function this.OnAllocate()
    -- developId and flowIndex auto-allocated under the `name`.
    V_TppPlayer.AddOutfit{
        name       = "MyMod:NeonSuitCamo",
        playerType = "DDFemale",

        partsPath = "/Assets/tpp/parts/chara/neon/neon_v00.parts",
        fpkPath   = "/Assets/tpp/pack/player/parts/neon_v00.fpk",

        -- Custom camo pattern (.fpk + .fv2 paired):
        camoFpk   = "/Assets/tpp/pack/player/parts/neon_camo_v00.fpk",
        camoFv2   = "/Assets/tpp/fova/chara/neon/neon_camo_v00.fv2",

        -- Custom diamond filter override:
        diamondFpk = "/Assets/tpp/pack/player/parts/neon_diamond_v00.fpk",
        diamondFv2 = "/Assets/tpp/fova/chara/neon/neon_diamond_v00.fv2",

        -- Use vanilla face/arm (default behavior anyway, shown for clarity):
        faceFpk    = true,               -- true = use vanilla
        enableArm  = true,               -- bionic arm on (false to suppress)

        -- No skin tone override:
        skinFv2 = true,                  -- (default)
    }
end
