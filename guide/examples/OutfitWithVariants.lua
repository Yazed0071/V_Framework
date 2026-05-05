--==============================================================================
-- V_FrameWork outfit example — VARIANTS
--==============================================================================
-- Each playerType branch (snake / ddMale / ddFemale / avatar) has its OWN
-- variants array. Variant 0 is the branch's base appearance (paths /
-- displayName at the branch level); entries in `variants` define variants
-- 1..N.
--
-- A variant inherits any field it doesn't explicitly set:
--   - partsPath / fpkPath: nil → inherit branch base
--   - camoFpk / camoFv2 / diamondFpk / diamondFv2 / voiceFpk:
--     kSubAssetUseVanilla → inherit branch base
--
-- Different playerTypes can have completely different variant sets —
-- e.g. Snake has 3 variants, DDFemale only has 1, etc. The cycle button's
-- visible count adapts to whatever the LIVE playerType branch declares.
--==============================================================================

function this.OnAllocate()
    local _, devId = V_TppPlayer.AddOutfit{
        name = "MyMod:NeonSuitVariants",                                         -- Required

        ddFemale = {
            partsPath = "/Assets/tpp/parts/chara/neon/neon_v00.parts",            -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/neon_v00.fpk",             -- Required
            camoFpk   = "/Assets/tpp/pack/player/parts/neon_v00_camo.fpk",        -- Optional

            variants = {                                                          -- Optional (up to 14 alternates)
                -- Variant 1: dark version (different fpk, same parts)
                {
                    fpkPath = "/Assets/tpp/pack/player/parts/neon_v00_dark.fpk",          -- Optional in variant (inherits branch base if omitted)
                    camoFpk = "/Assets/tpp/pack/player/parts/neon_v00_dark_camo.fpk",     -- Optional
                },
                -- Variant 2: red version
                {
                    fpkPath = "/Assets/tpp/pack/player/parts/neon_v00_red.fpk",           -- Optional
                    camoFpk = "/Assets/tpp/pack/player/parts/neon_v00_red_camo.fpk",      -- Optional
                },
                -- Variant 3: full alternate (different parts AND fpk)
                {
                    partsPath = "/Assets/tpp/parts/chara/neon/neon_v01.parts",            -- Optional
                    fpkPath   = "/Assets/tpp/pack/player/parts/neon_v01.fpk",             -- Optional
                    camoFpk   = "/Assets/tpp/pack/player/parts/neon_v01_camo.fpk",        -- Optional
                },
            },
        },

        snake = {
            partsPath = "/Assets/tpp/parts/chara/neon/neon_snake_v00.parts",      -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/neon_snake_v00.fpk",       -- Required
            variants = {                                                          -- Optional
                {
                    fpkPath = "/Assets/tpp/pack/player/parts/neon_snake_v00_dark.fpk",   -- Optional
                },
            },
        },

        -- Avatar omitted — auto-bridges to Snake's data (and Snake's variants).
    }

    -- Optional: programmatically switch to variant 2 right away.
    -- V_TppPlayer.SetOutfitVariant(devId, 2)
end
