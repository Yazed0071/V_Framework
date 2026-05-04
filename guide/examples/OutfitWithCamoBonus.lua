--==============================================================================
-- V_FrameWork outfit example — CAMO BONUS (surface-stat pinning)
--==============================================================================
-- Two ways to control your outfit's CAMOUFLAGE STAT BONUSES (the per-material
-- "blends in well with leaves / poorly on metal" numbers used by the AI's
-- detection math). Pick whichever fits the outfit:
--
--   Option A: camoBonusType
--     Pin the outfit to a vanilla PlayerCamoType (0..116). The outfit
--     INHERITS that camo's existing 82-material bonus row from the engine's
--     vanilla table. Cheapest option, no virtual-id slot consumed, unlimited
--     outfits can use it.
--
--   Option B: camoBonusValues
--     Define your OWN sparse 82-material row. The framework allocates a
--     virtual PlayerCamoType id (range 200..254 — pool of 55) and routes
--     the engine's bonus lookup through V_FrameWork's GetCamoufValue hook.
--     Use when no vanilla camo matches the profile you want.
--
-- If both are set on the same outfit, `camoBonusValues` wins (more specific).
--==============================================================================

function this.OnAllocate()

    --==========================================================================
    -- Option A — pin to LEAF camo's bonuses
    --==========================================================================
    -- "Forest sneaking suit" reuses the existing LEAF camo's stat profile.
    -- The visual camo pattern is independent — it follows whatever camoFpk /
    -- camoFv2 you provide (or vanilla if omitted).
    V_TppPlayer.AddOutfit{
        name       = "MyMod:ForestSuit",
        playerType = "Snake",
        partsPath  = "/Assets/tpp/parts/chara/snk/forest_v00.parts",
        fpkPath    = "/Assets/tpp/pack/player/parts/forest_v00.fpk",

        -- Pass a number 0..116. PlayerCamoType is the vanilla MGSV lua
        -- enum, so you can use its named members directly.
        camoBonusType = PlayerCamoType.LEAF,
    }


    --==========================================================================
    -- Option B — custom per-outfit bonuses
    --==========================================================================
    -- "Heat-shielded suit": strong vs metal/concrete (urban infiltration),
    -- weak in foliage. Doesn't match any vanilla camo's profile, so we
    -- specify the row explicitly.
    --
    -- Material names are MTR_* identifiers from V_TppPlayer.lua's
    -- k_MaterialByName table. Anything not listed defaults to 0.
    -- Values are signed int32; vanilla rows typically use 0..100, with
    -- negatives meaning "the AI sees you BETTER on this surface".
    V_TppPlayer.AddOutfit{
        name       = "MyMod:HeatShieldedSuit",
        playerType = "DDMale",
        partsPath  = "/Assets/tpp/parts/chara/snk/heatshield_v00.parts",
        fpkPath    = "/Assets/tpp/pack/player/parts/heatshield_v00.fpk",

        camoBonusValues = {
            -- Strong on metal / concrete / pipes:
            MTR_IRON_A = 80, MTR_IRON_B = 80, MTR_IRON_C = 80,
            MTR_PIPE_A = 70, MTR_PIPE_B = 70,
            MTR_CONC_A = 65, MTR_CONC_B = 65,
            MTR_TIN_A  = 60,
            MTR_BRIC_A = 55,

            -- Neutral on tile / glass:
            MTR_TILE_A = 30,
            MTR_GLAS_A = 20, MTR_GLAS_B = 20,

            -- Weak in foliage / soil (you'd light up like a beacon):
            MTR_LEAF   = -20, MTR_RLEF = -20,
            MTR_PLNT_A = -15,
            MTR_GRAS_A = -10, MTR_GRAS_B = -10,
            MTR_SOIL_A = -5,

            -- All other 60+ materials default to 0.
        },
    }


    --==========================================================================
    -- Both fields set on one outfit (rare, but legal)
    --==========================================================================
    -- When both are passed, `camoBonusValues` wins — the framework allocates
    -- a virtual id and uses your custom row, ignoring `camoBonusType`. This
    -- is just so you can experiment freely without removing one before
    -- adding the other.
    V_TppPlayer.AddOutfit{
        name       = "MyMod:HybridSuit",
        playerType = "Snake",
        partsPath  = "/Assets/tpp/parts/chara/snk/hybrid_v00.parts",
        fpkPath    = "/Assets/tpp/pack/player/parts/hybrid_v00.fpk",

        camoBonusType   = PlayerCamoType.BATTLEDRESS,  -- ignored (values wins)
        camoBonusValues = {
            MTR_LEAF = 100,
            MTR_RLEF = 100,
        },
    }

end


--==============================================================================
-- Verifying it works
--==============================================================================
-- After equipping the outfit, watch mod\V_FrameWork\V_FrameWork_log.txt:
--
--   [OutfitCamoBonus] FIRST OVERRIDE: livePT=0x42 ... pinned=200 ...
--     -> confirms the slot byte got pinned (fires for both Option A and B)
--
--   [OutfitGetCamoufValue] FIRST VIRTUAL HIT: virtualId=200 materialType=60
--   value=80 ... -> confirms the engine queried YOUR row (Option B only —
--     Option A queries the vanilla table directly so this line won't fire)
--
-- If FIRST OVERRIDE never appears, the outfit's camoBonusType / camoBonusValues
-- wasn't set, OR the outfit isn't actually equipped on the live player slot.
--==============================================================================
