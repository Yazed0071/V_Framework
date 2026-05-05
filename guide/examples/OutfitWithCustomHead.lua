-- ============================================================================
-- Custom-head example (Tier-3-A architecture)
--
-- The framework lets you add new entries to the HEAD OPTION submenu with
-- custom names and icons. The visual is one of the vanilla balaclava
-- styles (modder picks via TppEnemyFaceId).
--
-- Use V_TppPlayer.AddHeadOption{...} — the high-level wrapper that handles
-- BOTH the AddToEquipDevelopTable row (drives iDroid label / icon / R&D
-- entry) AND the RegisterHeadOption call (slot byte + visual face id).
-- One key, one call.
--
-- The shared developId drives:
--   • iDroid label and icon (from langEquipName / iconFtexPath)
--   • R&D cost / time / prerequisites (from develop.flow)
--   • Develop-gate visibility (head only appears in the HEAD OPTION
--     submenu after MotherBase R&D completes — set
--     `develop.flow.initialAvailable = 1` to start it pre-researched)
-- ============================================================================

local this = {}

function this.LoadLibraries()
    -- 1) Register the custom head.
    V_TppPlayer.AddHeadOption{
        name           = "MyMod:CoolHelmet",                                     -- Required (persistence key)
        TppEnemyFaceId = TppEnemyFaceId.dds_balaclava2,                          -- Required (vanilla balaclava to render)

        langEquipName  = "name_my_helmet",                                        -- Optional (iDroid label)
        iconFtexPath   = "/Assets/mod/ui/texture/EquipIcon/head/ui_my_helmet_alp",-- Optional (iDroid icon)

        develop = {                                                               -- Optional (R&D cost / availability)
            flow = {
                grade            = 2,                                             -- Optional
                developGmpCost   = 0,                                             -- Optional
                initialAvailable = 0,                                             -- Optional (0 = locked until researched)
            },
        },
    }

    -- 2) Reference the head by name in any per-PT branch's headOptions array.
    V_TppPlayer.AddOutfit{
        name = "MyMod:DDFemale_Outfit",                                          -- Required

        ddFemale = {
            partsPath = "/Assets/mod/chara/ddf/ddf_Outfit.parts",                 -- Required
            fpkPath   = "/Assets/mod/pack/chara/ddf/ddf_Outfit.fpk",              -- Required

            headOptions = {                                                       -- Optional (per-PT)
                "NONE",
                "BALACLAVA",
                "MyMod:CoolHelmet",   -- matches the AddHeadOption `name`
            },
        },

        develop = {                                                               -- Optional (R&D row for the OUTFIT)
            const = { langEquipName = "Suit" },                                   -- Optional
            flow  = { developGmpCost = 50000, initialAvailable = 0 },             -- Optional
        },
    }
end

return this
