-- V_TppPlayer — custom player outfits, camo table, voice FPK overrides.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- ============================================================
-- Player voice FPK overrides
-- ============================================================

function this.SetPlayerVoiceFpkPathForType(playerType, path)
    V_FrameWork.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(playerType)
    V_FrameWork.ClearPlayerVoiceFpkPathForType(playerType)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.ClearAllPlayerVoiceFpkOverrides()
end

-- ============================================================
-- Custom player outfits
-- ============================================================
--
-- High-level wrapper around V_FrameWork.RegisterOutfit. developId and
-- flowIndex are AUTO-ALLOCATED and persisted in V_FrameWork_State.lua
-- under the supplied `name` key — same mechanism weapons use. Repeated
-- calls with the same `name` get the SAME ids back across sessions.
--
-- The only required modder fields are: name, playerType, partsPath, fpkPath.
-- developId/flowIndex may still be passed explicitly if a mod needs to
-- pin them, but for normal use you should let auto-allocate handle it.
--
-- Returns (partsType, developId, flowIndex) on success, or false on failure.

local function buildVariantArray(srcVariants)
    if type(srcVariants) ~= "table" or #srcVariants == 0 then
        return nil
    end

    local out = {}
    for i, v in ipairs(srcVariants) do
        if type(v) == "table" then
            out[i] = {
                partsPath     = v.partsPath,
                fpkPath       = v.fpkPath,
                camoFpk       = v.camoFpk,
                camoFv2       = v.camoFv2,
                diamondFpk    = v.diamondFpk,
                displayName = v.displayName,
            }
        end
    end
    return out
end

function this.AddOutfit(opts)
    if type(opts) ~= "table" then return false end

    -- `name` is required when developId / flowIndex are auto-allocated
    -- (it's the persistence key in V_FrameWork_State.lua). Direct
    -- developId / flowIndex pinning is supported for migration cases
    -- but discouraged for new mods.
    if (opts.developId == nil or opts.flowIndex == nil)
        and (type(opts.name) ~= "string" or opts.name == "")
    then
        V_FrameWork.Log("[V_TppPlayer] AddOutfit: 'name' is required when developId/flowIndex are not provided")
        return false
    end

    local partsType, developId, flowIndex = V_FrameWork.RegisterOutfit({
        key                 = opts.name,
        developId           = opts.developId,    -- nil = auto from state file
        flowIndex           = opts.flowIndex,    -- nil = auto from state file
        playerType          = opts.playerType or 0,

        partsPath           = opts.partsPath,
        fpkPath             = opts.fpkPath,

        camoFpk             = opts.camoFpk,
        faceFpk             = opts.faceFpk,
        skinFv2             = opts.skinFv2,
        diamondFpk          = opts.diamondFpk,
        camoFv2             = opts.camoFv2,
        diamondFv2          = opts.diamondFv2,

        enableArm           = opts.enableArm,

        -- enableHead forces the framework to load a default DD head FPK
        -- on top of the body for outfits whose body parts file has no
        -- integrated head mesh (FROG / SSD ports etc.). Default false
        -- so Quiet-style integrated-head outfits (e.g. Jill BattleSuit)
        -- aren't disturbed.
        enableHead           = opts.enableHead,

        -- Optional override for the soldier face index (info+0x04).
        -- When enableHead=true AND the player slot has 0 (no manual
        -- face chosen), the framework writes this value before orig
        -- reads the face FPK. Use a populated FaceUnit index if 0
        -- doesn't load (1..899). Leave nil to keep playerFaceId at 0.
        defaultSoldierFaceId = opts.defaultSoldierFaceId,

        -- The C++ auto-enables the HEAD OPTION submenu whenever
        -- `headOptions` is non-empty, so just pass the array through.
        headOptions         = opts.headOptions,

        -- Top-level cycle-button label for variant 0 (the base appearance
        -- shown in SORTIE PREP > UNIFORMS before the user cycles to a
        -- variant). Per-variant `displayName` lives inside each entry of
        -- the `variants` array (handled by buildVariantArray above).
        -- The bridge accepts either a LangId string (`displayName`) and
        -- computes StrCode64, or a precomputed `displayNameHash` number.
        displayName         = opts.displayName,
        displayNameHash     = opts.displayNameHash,

        variants            = buildVariantArray(opts.variants),

        -- camoBonusType — pin a vanilla PlayerCamoType (0..116) for
        -- surface-bonus lookup while this outfit is equipped. Pass a
        -- number (typically `PlayerCamoType.BATTLEDRESS` etc., which
        -- is just the vanilla MGSV lua enum). Default nil = no pin.
        camoBonusType      = opts.camoBonusType,

        -- camoBonusValues — give the outfit its OWN unique surface-
        -- bonus row instead of inheriting a vanilla one. Sparse table
        -- keyed by material name OR 1-based numeric index:
        --   camoBonusValues = {
        --       MTR_LEAF = 50, MTR_RLEF = 50, MTR_PLNT_A = 50,
        --       -- everything not listed defaults to 0
        --   }
        -- The framework allocates a virtual PlayerCamoType id and
        -- routes the engine's bonus-table lookup through our
        -- GetCamoufValue hook. If both camoBonusType and
        -- camoBonusValues are set, values wins (more specific).
        camoBonusValues    = opts.camoBonusValues,

        -- langEquipName forwarded from develop.const (if present) so the
        -- framework can hash it via FoxStrHash and use it to override the
        -- vanilla suit-name UI lookup that returns blank for our custom
        -- partsType range. Without this, SORTIE PREP > SELECT CHARACTER >
        -- UNIFORMS row shows blank text when wearing our custom suits.
        -- The user can still override by setting `langEquipName` directly
        -- on the AddOutfit call (rare).
        langEquipName       = opts.langEquipName
                              or (opts.develop and opts.develop.const
                                  and opts.develop.const.langEquipName),
    })

    if partsType == false or partsType == nil then
        return false
    end

    -- If the mod also wants R&D table entries (cost, time, lang strings,
    -- icon, etc.) — register them here with the auto-allocated ids,
    -- so the mission-prep R&D screen can show the outfit properly.
    if opts.develop ~= nil then
        local const = opts.develop.const or {}
        local flow  = opts.develop.flow  or {}
        const.p00 = developId
        const.p50 = flowIndex
        V_FrameWork.AddToEquipDevelopTable(opts.name, { const = const, flow = flow })
    end

    return partsType, developId, flowIndex
end

-- Programmatic variant switch. Equivalent to V_FrameWork.SetOutfitVariant
-- but namespaced under V_TppPlayer for consistency with the rest of the
-- guide files.
function this.SetOutfitVariant(developId, variantIndex)
    return V_FrameWork.SetOutfitVariant(developId, variantIndex)
end

-- Returns a table { partsType, selectorCode, flowIndex, playerType,
-- variantCount, activeVariant, supportsHeadOptions } or nil if the
-- developId is not registered.
function this.GetOutfitInfo(developId)
    return V_FrameWork.GetOutfitInfo(developId)
end

-- ============================================================
-- Custom HEAD OPTION submenu entries (Tier-3-A)
-- ============================================================
--
-- High-level wrapper around V_FrameWork.RegisterHeadOption that ALSO
-- handles the paired V_TppEquip.AddToEquipDevelopTable call, so a mod
-- can add a new HEAD OPTION row in one shot:
--
--   V_TppPlayer.AddHeadOption{
--       name           = "MyMod:CoolHelmet",
--       TppEnemyFaceId = TppEnemyFaceId.dds_balaclava2,
--       langEquipName  = "name_my_helmet",
--       iconFtexPath   = "/Assets/mod/.../ui_helmet_alp",
--       develop = {
--           const = { ... },   -- optional const overrides (p06/p08 default
--                              -- to langEquipName / iconFtexPath above)
--           flow  = {          -- optional R&D cost / availability
--               grade            = 2,
--               developGmpCost   = 50000,
--               initialAvailable = 0,    -- 1 to start researched
--           },
--       },
--   }
--
-- Then reference the head by name in any outfit's headOptions:
--   headOptions = { "NONE", "BALACLAVA", "MyMod:CoolHelmet" }
--
-- Returns the assigned equipId on success, false on failure (logged).
--
-- The wrapper enforces the call order (AddToEquipDevelopTable first,
-- then RegisterHeadOption) — required because RegisterHeadOption looks
-- up the row index assigned to the shared `name` key.
--
-- Visual: chooses a vanilla balaclava via TppEnemyFaceId. Distinct
-- custom-mesh heads aren't yet supported (Tier-3-B); see
-- guide/examples/OutfitWithCustomHead.lua for the design rationale.

function this.AddHeadOption(opts)
    if type(opts) ~= "table" then return false end
    if type(opts.name) ~= "string" or opts.name == "" then
        V_FrameWork.Log("[V_TppPlayer] AddHeadOption: 'name' is required")
        return false
    end

    -- 1) Paired AddToEquipDevelopTable — drives iDroid label/icon AND
    --    the R&D develop-gate. The wrapper builds a sane default const
    --    block from langEquipName / iconFtexPath; modder overrides via
    --    `develop.const = { ... }` win.
    local const = (opts.develop and opts.develop.const) or {}
    local flow  = (opts.develop and opts.develop.flow)  or {}

    -- Default const block — minimal viable EquipDevelopConstSetting row
    -- for a head-option entry. The values mirror the BALACLAVA vanilla
    -- row's shape (suit-type, no skill/blueprint, default group).
    if const.p01 == nil then const.p01 = TppEquip.EQP_None end
    if const.p02 == nil then const.p02 = TppMbDev.EQP_DEV_TYPE_Suit end
    if const.p03 == nil then const.p03 = 0     end
    if const.p04 == nil then const.p04 = 0     end
    if const.p05 == nil then const.p05 = 65535 end
    if const.p09 == nil then const.p09 = 0 end
    if const.p36 == nil then const.p36 = 1 end

    -- p06 (langEquipName) / p07 (langEquipInfo) / p08 (iconFtexPath) /
    -- p30 (langEquipRealName) take user shortcuts when not explicitly set.
    if const.p06 == nil then const.p06 = opts.langEquipName end
    if const.p07 == nil then const.p07 = opts.langEquipInfo or opts.langEquipName end
    if const.p08 == nil then const.p08 = opts.iconFtexPath  end
    if const.p30 == nil then const.p30 = opts.langEquipName end

    -- Default flow block — develop-gated by default (initialAvailable=0)
    -- so the player has to research the head in R&D before it appears.
    -- Pass `flow.initialAvailable = 1` to skip the gate.
    if flow.grade            == nil then flow.grade            = 1 end
    if flow.developGmpCost   == nil then flow.developGmpCost   = 0 end
    if flow.initialAvailable == nil then flow.initialAvailable = 0 end

    V_TppEquip.AddToEquipDevelopTable(opts.name, { const = const, flow = flow })

    -- 2) Register the head visual + slot allocation. Same `name` key, so
    --    the developId from step 1 is reused. iDroid label/icon are
    --    driven by step 1's const block (p06 / p08), so we don't pass
    --    `langName` / `iconFtex` here — those are RegisterHeadOption
    --    fallbacks for the no-AddToEquipDevelopTable path that this
    --    wrapper never takes.
    return V_FrameWork.RegisterHeadOption{
        name           = opts.name,
        TppEnemyFaceId = opts.TppEnemyFaceId,
    }
end

-- ============================================================
-- Camo table editing  (117 camo types x 82 materials)
-- ============================================================
--
-- Names below are authoritative from the retail binary's
-- PlayerCamoType enum (all 117) and camo-parameter MaterialType
-- table (all 82). Numeric indices are equally accepted.

local k_CamoByName = {
    OLIVEDRAB         = 0,    SPLITTER          = 1,    SQUARE            = 2,
    TIGERSTRIPE       = 3,    GOLDTIGER         = 4,    FOXTROT           = 5,
    WOODLAND          = 6,    WETWORK           = 7,    ARBANGRAY         = 8,
    ARBANBLUE         = 9,    SANDSTORM         = 10,   REALTREE          = 11,
    INVISIBLE         = 12,   BLACK             = 13,   SNEAKING_SUIT_GZ  = 14,
    SNEAKING_SUIT_TPP = 15,   BATTLEDRESS       = 16,   PARASITE          = 17,
    NAKED             = 18,   LEATHER           = 19,   SOLIDSNAKE        = 20,
    NINJA             = 21,   RAIDEN            = 22,   HOSPITAL          = 23,
    GOLD              = 24,   SILVER            = 25,   PANTHER           = 26,
    AVATAR_EDIT_MAN   = 27,   MGS3              = 28,   MGS3_NAKED        = 29,
    MGS3_SNEAKING     = 30,   MGS3_TUXEDO       = 31,   EVA_CLOSE         = 32,
    EVA_OPEN          = 33,   BOSS_CLOSE        = 34,   BOSS_OPEN         = 35,
    C23               = 36,   C24               = 37,   C27               = 38,
    C29               = 39,   C30               = 40,   C35               = 41,
    C38               = 42,   C39               = 43,   C42               = 44,
    C46               = 45,   C49               = 46,   C52               = 47,
    C16               = 48,   C17               = 49,   C18               = 50,
    C19               = 51,   C20               = 52,   C22               = 53,
    C25               = 54,   C26               = 55,   C28               = 56,
    C31               = 57,   C32               = 58,   C33               = 59,
    C36               = 60,   C37               = 61,   C40               = 62,
    C41               = 63,   C43               = 64,   C44               = 65,
    C45               = 66,   C47               = 67,   C48               = 68,
    C50               = 69,   C51               = 70,   C53               = 71,
    C54               = 72,   C55               = 73,   C56               = 74,
    C57               = 75,   C58               = 76,   C59               = 77,
    C60               = 78,
    SWIMWEAR_C00      = 79,   SWIMWEAR_C01      = 80,   SWIMWEAR_C02      = 81,
    SWIMWEAR_C03      = 82,   SWIMWEAR_C05      = 83,   SWIMWEAR_C06      = 84,
    SWIMWEAR_C38      = 85,   SWIMWEAR_C39      = 86,   SWIMWEAR_C44      = 87,
    SWIMWEAR_C46      = 88,   SWIMWEAR_C48      = 89,   SWIMWEAR_C53      = 90,
    SWIMWEAR_G_C00    = 91,   SWIMWEAR_G_C01    = 92,   SWIMWEAR_G_C02    = 93,
    SWIMWEAR_G_C03    = 94,   SWIMWEAR_G_C05    = 95,   SWIMWEAR_G_C06    = 96,
    SWIMWEAR_G_C38    = 97,   SWIMWEAR_G_C39    = 98,   SWIMWEAR_G_C44    = 99,
    SWIMWEAR_G_C46    = 100,  SWIMWEAR_G_C48    = 101,  SWIMWEAR_G_C53    = 102,
    SWIMWEAR_H_C00    = 103,  SWIMWEAR_H_C01    = 104,  SWIMWEAR_H_C02    = 105,
    SWIMWEAR_H_C03    = 106,  SWIMWEAR_H_C05    = 107,  SWIMWEAR_H_C06    = 108,
    SWIMWEAR_H_C38    = 109,  SWIMWEAR_H_C39    = 110,  SWIMWEAR_H_C44    = 111,
    SWIMWEAR_H_C46    = 112,  SWIMWEAR_H_C48    = 113,  SWIMWEAR_H_C53    = 114,
    OCELOT            = 115,  QUIET             = 116,
}

local k_MaterialByName = {
    MTR_IRON_A = 0,  MTR_IRON_B = 1,  MTR_IRON_C = 2,  MTR_IRON_D = 3,
    MTR_IRON_E = 4,  MTR_IRON_F = 5,  MTR_IRON_G = 6,  MTR_IRON_M = 7,
    MTR_IRON_N = 8,  MTR_IRON_W = 9,
    MTR_PIPE_A = 10, MTR_PIPE_B = 11, MTR_PIPE_S = 12,
    MTR_TIN_A  = 13,
    MTR_FENC_A = 14, MTR_FENC_B = 15, MTR_FENC_F = 16,
    MTR_CONC_A = 17, MTR_CONC_B = 18,
    MTR_BRIC_A = 19,
    MTR_PLAS_A = 20, MTR_PLAS_B = 21, MTR_PLAS_W = 22,
    MTR_PAPE_A = 23, MTR_PAPE_B = 24, MTR_PAPE_C = 25, MTR_PAPE_D = 26,
    MTR_RUBB_A = 27, MTR_RUBB_B = 28,
    MTR_CLOT_A = 29, MTR_CLOT_B = 30, MTR_CLOT_C = 31, MTR_CLOT_D = 32,
    MTR_CLOT_E = 33,
    MTR_GLAS_A = 34, MTR_GLAS_B = 35, MTR_GLAS_C = 36,
    MTR_VINL_A = 37, MTR_VINL_W = 38,
    MTR_TILE_A = 39,
    MTR_TLRF_A = 40,
    MTR_ALRM_A = 41,
    MTR_COPS_A = 42, MTR_COPS_B = 43,
    MTR_BRIR_A = 44,
    MTR_BLOD_A = 45,
    MTR_SOIL_A = 46, MTR_SOIL_B = 47, MTR_SOIL_C = 48, MTR_SOIL_D = 49,
    MTR_SOIL_E = 50, MTR_SOIL_F = 51, MTR_SOIL_G = 52, MTR_SOIL_H = 53,
    MTR_SOIL_R = 54, MTR_SOIL_W = 55,
    MTR_GRAV_A = 56,
    MTR_SAND_A = 57, MTR_SAND_B = 58, MTR_SAND_C = 59,
    MTR_LEAF   = 60, MTR_RLEF   = 61, MTR_RLEF_B = 62,
    MTR_WOOD_A = 63, MTR_WOOD_B = 64, MTR_WOOD_C = 65, MTR_WOOD_D = 66,
    MTR_WOOD_G = 67, MTR_WOOD_M = 68, MTR_WOOD_W = 69,
    MTR_FWOD_A = 70,
    MTR_PLNT_A = 71,
    MTR_ROCK_A = 72, MTR_ROCK_B = 73, MTR_ROCK_P = 74,
    MTR_MOSS_A = 75,
    MTR_TURF_A = 76,
    MTR_WATE_A = 77, MTR_WATE_B = 78, MTR_WATE_C = 79,
    MTR_AIR_A  = 80,
    MTR_NONE_A = 81,
}

local function resolveCamoIndex(v)
    if type(v) == "number" then return v end
    if type(v) == "string" then return k_CamoByName[v] end
    return nil
end
local function resolveMaterialIndex(v)
    if type(v) == "number" then return v end
    if type(v) == "string" then return k_MaterialByName[v] end
    return nil
end

function this.SetCamoValue(camoType, materialType, value)
    local c = resolveCamoIndex(camoType)
    local m = resolveMaterialIndex(materialType)
    if c == nil or m == nil then return false end
    return V_FrameWork.SetCamoValue(c, m, value)
end

function this.GetCamoValue(camoType, materialType)
    local c = resolveCamoIndex(camoType)
    local m = resolveMaterialIndex(materialType)
    if c == nil or m == nil then return 0 end
    return V_FrameWork.GetCamoValue(c, m)
end

function this.CloneCamoRow(dst, src)
    local d = resolveCamoIndex(dst)
    local s = resolveCamoIndex(src)
    if d == nil or s == nil then return false end
    return V_FrameWork.CloneCamoRow(d, s)
end

function this.ImportCamoRow(camoType, values)
    local c = resolveCamoIndex(camoType)
    if c == nil or type(values) ~= "table" then return false end
    return V_FrameWork.ImportCamoRow(c, values)
end

-- Bulk import the entire 117x82 camo table in one shot. Equivalent to the
-- vanilla `Player.InitCamoufTable(table)` call that mods like Infinite
-- Heaven use to ship a full default table — replaces every row, pushes
-- to the engine ONCE.
--
-- `tbl` is a 1-based 2D Lua array:
--   tbl[1]      = { v1, v2, ..., v82 }   -- camoType 0 (OLIVEDRAB), 82 material weights
--   tbl[2]      = { ... }                -- camoType 1 (SPLITTER)
--   ...
--   tbl[117]    = { ... }                -- camoType 116 (QUIET)
--
-- Rows past 117 / cells past 82 are ignored. Missing cells become 0.
function this.ImportCamoTable(tbl)
    if type(tbl) ~= "table" then return false end
    return V_FrameWork.ImportCamoTable(tbl)
end

return this
