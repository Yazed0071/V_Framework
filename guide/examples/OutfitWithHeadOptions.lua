--==============================================================================
-- V_FrameWork outfit example — HEAD OPTIONS
--==============================================================================
-- Outfit that exposes the iDroid HEAD OPTION submenu in mission prep with a
-- list of selectable headgear entries.
--
-- Each entry in `headOptions = {...}` can be:
--   • A vanilla alias string (case- and separator-insensitive):
--       "NONE"             → 0x400 sentinel
--       "BANDANA"          → 0x20E   (only renders on Snake / Avatar)
--       "INFINITE BANDANA" → 0x20F   (only renders on Snake / Avatar)
--       "BALACLAVA"        → 0x210
--       "SP-HEADGEAR"      → 0x211
--       "HP-HEADGEAR"      → 0x212
--   • A vanilla equipId number (same values as above)
--   • A custom-head NAME registered via V_FrameWork.RegisterHeadOption
--     (see OutfitWithCustomHead.lua for the full pattern)
--   • A custom-head equipId number returned by RegisterHeadOption
--
-- supportsHeadOptions auto-implies true when headOptions is non-empty,
-- so you can omit the boolean.
--
-- IMPORTANT: this example uses vanilla heads only. The body parts file
-- must NOT ship an integrated head — use a "shaved" / headless body so
-- the orig face-FPK pipeline can swap heads at runtime. Set
-- enableHead=true so the framework keeps the face system live for this
-- outfit.
--==============================================================================

local this = {}

function this.OnAllocate()
    V_TppPlayer.AddOutfit{
        name       = "MyMod:NeonSuitHeads",
        playerType = "DDFemale",

        partsPath = "/Assets/tpp/parts/chara/neon/neon_v01.parts",
        fpkPath   = "/Assets/tpp/pack/player/parts/neon_v01.fpk",

        headOptions = {
            "NONE",
            "BALACLAVA",
            "SP-HEADGEAR",
            "HP-HEADGEAR",
        },

        enableHead = true,    -- body has no integrated head; let the orig
                              -- face/head system run for this outfit
        enableArm  = true,
    }
end

return this
