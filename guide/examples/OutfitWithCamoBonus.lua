--==============================================================================
-- V_FrameWork outfit example — CAMO BONUS (surface-stat pinning)
--==============================================================================
-- Two ways to control your outfit's CAMOUFLAGE STAT BONUSES (the per-material
-- "blends in well with leaves / poorly on metal" numbers used by the AI's
-- detection math). Pick whichever fits the outfit:
--
--   Option A: camoBonusType
--     Pin the branch to a vanilla PlayerCamoType (0..116). The branch
--     INHERITS that camo's existing 82-material bonus row from the engine's
--     vanilla table. Cheapest option, no virtual-id slot consumed.
--
--   Option B: camoBonusValues
--     Define your OWN sparse 82-material row. The framework allocates a
--     virtual PlayerCamoType id (range 200..254 — pool of 55 slots, shared
--     across ALL custom branches across ALL mods).
--
-- Both fields live INSIDE each per-PT branch — different playerTypes can
-- have completely different bonus profiles. If both are passed on the same
-- branch, `camoBonusValues` wins.
--==============================================================================

function this.OnAllocate()

    --==========================================================================
    -- Option A — pin to LEAF camo's bonuses
    --==========================================================================
    V_TppPlayer.AddOutfit{
        name = "MyMod:ForestSuit",                                               -- Required

        snake = {
            partsPath = "/Assets/tpp/parts/chara/snk/forest_v00.parts",           -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/forest_v00.fpk",           -- Required

            camoBonusType = PlayerCamoType.LEAF,                                  -- Optional (vanilla PlayerCamoType pin, 0..116)
        },
    }


    --==========================================================================
    -- Option B — custom per-PT bonuses
    --==========================================================================
    V_TppPlayer.AddOutfit{
        name = "MyMod:HeatShieldedSuit",                                         -- Required

        ddMale = {
            partsPath = "/Assets/tpp/parts/chara/snk/heatshield_v00.parts",       -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/heatshield_v00.fpk",       -- Required

            camoBonusValues = {                                                   -- Optional (sparse 82-material row, anything not listed defaults to 0)
                -- Strong on metal / concrete / pipes:
                MTR_IRON_A = 80, MTR_IRON_B = 80, MTR_IRON_C = 80,
                MTR_PIPE_A = 70, MTR_PIPE_B = 70,
                MTR_CONC_A = 65, MTR_CONC_B = 65,
                MTR_TIN_A  = 60,
                MTR_BRIC_A = 55,

                -- Neutral on tile / glass:
                MTR_TILE_A = 30,
                MTR_GLAS_A = 20, MTR_GLAS_B = 20,

                -- Weak in foliage / soil:
                MTR_LEAF   = -20, MTR_RLEF = -20,
                MTR_PLNT_A = -15,
                MTR_SOIL_A = -5,
            },
        },
    }


    --==========================================================================
    -- Per-PT differentiation — Snake gets one profile, DDFemale another
    --==========================================================================
    V_TppPlayer.AddOutfit{
        name = "MyMod:DualProfileSuit",                                          -- Required

        snake = {
            partsPath     = "/Assets/tpp/parts/chara/snk/dual_v00.parts",         -- Required
            fpkPath       = "/Assets/tpp/pack/player/parts/dual_v00.fpk",         -- Required
            camoBonusType = PlayerCamoType.LEAF,                                  -- Optional (forest profile)
        },

        ddFemale = {
            partsPath = "/Assets/tpp/parts/chara/ddf/dual_v00.parts",             -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/dual_v00_ddf.fpk",         -- Required
            camoBonusValues = {                                                   -- Optional (urban profile)
                MTR_IRON_A = 80,
                MTR_CONC_A = 60,
                MTR_LEAF   = -20,
            },
        },
    }

end


--==============================================================================
-- Verifying it works
--==============================================================================
-- After equipping the outfit, watch mod\V_FrameWork\V_FrameWork_log.txt:
--
--   [OutfitCamoBonus] FIRST OVERRIDE: liveParts=0x42 livePlayer=2 ...
--   pinned=200 ... -> the slot byte got pinned (Option A: vanilla id;
--   Option B: virtual id 200..254).
--
--   [OutfitGetCamoufValue] FIRST VIRTUAL HIT: virtualId=200 materialType=60
--   value=80 ownerPT=2 ... -> the engine queried YOUR row. Fires for
--   Option B only — Option A queries the vanilla table directly.
--==============================================================================
