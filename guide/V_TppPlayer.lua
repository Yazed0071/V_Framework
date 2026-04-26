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
                displayNameId = v.displayNameId,
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
        armFpk              = opts.armFpk,
        skinFv2             = opts.skinFv2,
        diamondFpk          = opts.diamondFpk,
        camoFv2             = opts.camoFv2,
        diamondFv2          = opts.diamondFv2,

        supportsHeadOptions = opts.supportsHeadOptions or false,
        headOptions         = opts.headOptions,

        variants            = buildVariantArray(opts.variants),

        partsTypeHint       = opts.partsTypeHint,
        selectorCodeHint    = opts.selectorCodeHint,
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

return this
