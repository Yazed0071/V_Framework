--==============================================================================
-- V_FrameWork outfit example — VARIANTS
--==============================================================================
-- One outfit definition exposes multiple appearance variants without
-- duplicating boilerplate. The base outfit fields define variant 0;
-- entries in the `variants` array define variants 1..N.
--
-- A variant inherits any field it doesn't explicitly set:
--   - partsPath / fpkPath: nil → inherit base
--   - camoFpk / camoFv2 / diamondFpk / voiceFpk: kSubAssetUseVanilla → inherit base
--
-- Active variant is published when the user commits a variant via the
-- mission-prep UI (the cycle button writes the variant index into the
-- commit blob). For programmatic switching, use V_TppPlayer.SetOutfitVariant.
--==============================================================================

function this.OnAllocate()
    -- developId and flowIndex auto-allocated under the `name`.
    -- Capture the returned developId for SetOutfitVariant calls below.
    local _, devId = V_TppPlayer.AddOutfit{
        name       = "MyMod:NeonSuitVariants",
        playerType = "DDFemale",

        -- Variant 0 (base)
        partsPath = "/Assets/tpp/parts/chara/neon/neon_v00.parts",
        fpkPath   = "/Assets/tpp/pack/player/parts/neon_v00.fpk",
        camoFpk   = "/Assets/tpp/pack/player/parts/neon_v00_camo.fpk",

        -- Variants 1..N override the base per-field. Up to 8 entries.
        variants = {
            -- Variant 1: dark version (different fpk, same parts)
            {
                fpkPath = "/Assets/tpp/pack/player/parts/neon_v00_dark.fpk",
                camoFpk = "/Assets/tpp/pack/player/parts/neon_v00_dark_camo.fpk",
            },
            -- Variant 2: red version
            {
                fpkPath = "/Assets/tpp/pack/player/parts/neon_v00_red.fpk",
                camoFpk = "/Assets/tpp/pack/player/parts/neon_v00_red_camo.fpk",
            },
            -- Variant 3: full alternate (different parts AND fpk)
            {
                partsPath = "/Assets/tpp/parts/chara/neon/neon_v01.parts",
                fpkPath   = "/Assets/tpp/pack/player/parts/neon_v01.fpk",
                camoFpk   = "/Assets/tpp/pack/player/parts/neon_v01_camo.fpk",
            },
        },
    }

    -- Optional: programmatically switch to variant 2 right away.
    -- V_TppPlayer.SetOutfitVariant(devId, 2)
end
