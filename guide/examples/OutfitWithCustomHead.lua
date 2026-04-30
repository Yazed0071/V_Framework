-- ============================================================================
-- Custom-head example (Tier-3-A architecture)
--
-- The framework lets you add new entries to the HEAD OPTION submenu with
-- custom names and icons. The visual is one of the vanilla balaclava
-- styles (modder picks via TppEnemyFaceId).
--
-- Distinct custom-mesh heads aren't yet supported — sentinel face id
-- values outside the FaceUnit table hang the orig asset loader (the
-- bookkeeping needs a real FaceUnit row to signal load completion).
-- Workaround: pick the vanilla balaclava that best matches your design.
--
-- Use V_TppPlayer.AddHeadOption{...} — the high-level wrapper that
-- handles BOTH the AddToEquipDevelopTable row (drives iDroid label /
-- icon / R&D entry) AND the RegisterHeadOption call (slot byte +
-- visual face id). One key, one call.
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
    -- 1) Register the custom head — one call, internally pairs the
    --    AddToEquipDevelopTable row with the RegisterHeadOption call.
    --    Visual is the vanilla balaclava chosen by TppEnemyFaceId.
    --
    --    DDFemale-compatible TppEnemyFaceId values:
    --      TppEnemyFaceId.dds_balaclava0  full-coverage tactical
    --      TppEnemyFaceId.dds_balaclava1  similar
    --      TppEnemyFaceId.dds_balaclava2  similar
    --      TppEnemyFaceId.dds_balaclava3  default DDFemale balaclava
    --      TppEnemyFaceId.dds_balaclava4  variant
    --      TppEnemyFaceId.dds_balaclava5  another variant (default if unset)
    --      TppEnemyFaceId.svs_balaclava   SVS-style balaclava
    V_TppPlayer.AddHeadOption{
        name           = "MyMod:CoolHelmet",
        TppEnemyFaceId = TppEnemyFaceId.dds_balaclava2,

        -- iDroid label / icon (default const-block fields p06 / p08)
        langEquipName  = "name_my_helmet",
        iconFtexPath   = "/Assets/mod/ui/texture/EquipIcon/head/ui_my_helmet_alp",

        -- Optional R&D cost overrides. Defaults: grade=2, cost=0,
        -- initialAvailable=0 (develop-gated). Set initialAvailable=1
        -- if you want the head pre-researched.
        develop = {
            flow = {
                grade            = 2,
                developGmpCost   = 0,
                initialAvailable = 0,
            },
        },
    }

    -- 2) Reference the head by name in any outfit's headOptions array.
    V_TppPlayer.AddOutfit{
        name       = "MyMod:DDFemale_Outfit",
        playerType = "DDFemale",
        partsPath  = "/Assets/mod/chara/ddf/ddf_Outfit.parts",
        fpkPath    = "/Assets/mod/pack/chara/ddf/ddf_Outfit.fpk",

        headOptions = {
            "NONE",
            "BALACLAVA",
            "MyMod:CoolHelmet",   -- ← matches the AddHeadOption `name`
        },

        develop = {
            const = { langEquipName = "Suit" },
            flow  = { developGmpCost = 50000, initialAvailable = 0 },
        },
    }
end

return this
