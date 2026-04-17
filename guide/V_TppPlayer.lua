-- V_TppPlayer — player outfits, voice overrides, camo gameplay values.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- Build the parts-registration table that V_FrameWork.SetPlayerPartsPath
-- expects. Used for both the base suit and each variant body.
local function buildPartsTable(src, fallbackPlayerType)
    return {
        playerType = src.playerType or fallbackPlayerType,
        partsPath  = src.partsPath,
        fpkPath    = src.fpkPath,
        enableHead = src.enableHead ~= false,   -- default true
        enableHand = src.enableHand ~= false,   -- default true
        fv2Path    = src.fv2Path,
        fv2FpkPath = src.fv2FpkPath,
        faceFpk    = src.faceFpk,
        armFpk     = src.armFpk,
        diamondFpk = src.diamondFpk,
    }
end

-- Register a custom player outfit. Orchestrates:
--   1. SetPlayerPartsPath (base suit)         → partsType
--   2. AddToEquipDevelopTable (R&D entry)     → developId
--   3. LinkDevelopIdToPlayerSuit              (glues develop → partsType)
--   4. For each variant: register + bind to a variant group.
-- Returns (partsType, developId) on success, or false on failure.
function this.AddOutfit(outfitData)
    if type(outfitData) ~= "table" then return false end
    if not outfitData.partsPath or not outfitData.fpkPath then return false end
    if type(outfitData.develop) ~= "table" then return false end

    local partsType = V_FrameWork.SetPlayerPartsPath(
        buildPartsTable(outfitData, outfitData.playerType))
    if not partsType or partsType == false then return false end

    -- Develop key must be stable across sessions so persisted equipId/developId
    -- stay consistent. partsPath is unique per outfit, so use that.
    local developKey = outfitData.name or ("suit:" .. outfitData.partsPath)
    local developId = V_FrameWork.AddToEquipDevelopTable(developKey, outfitData.develop)
    if not developId or developId == false then return partsType end

    V_FrameWork.LinkDevelopIdToPlayerSuit(developId, partsType)

    -- Body variants: all variants share a single group id; base gets index 0,
    -- each variant gets index 1..N.
    if type(outfitData.variants) == "table" and #outfitData.variants > 0 then
        local groupId = V_FrameWork.AllocateVariantGroupId()
        if groupId and groupId ~= false then
            V_FrameWork.SetVariantGroup(partsType, groupId, 0)
            for i, variant in ipairs(outfitData.variants) do
                local variantTable = buildPartsTable(variant, outfitData.playerType)
                -- Inherit head/hand from base when variant omits them.
                if variant.enableHead == nil then
                    variantTable.enableHead = outfitData.enableHead ~= false
                end
                if variant.enableHand == nil then
                    variantTable.enableHand = outfitData.enableHand ~= false
                end
                local variantPartsType = V_FrameWork.SetPlayerPartsPath(variantTable)
                if variantPartsType and variantPartsType ~= false then
                    V_FrameWork.SetVariantGroup(variantPartsType, groupId, i)
                end
            end
        end
    end

    return partsType, developId
end

-- Camo gameplay table edits. These operate on the 117x82 camo/material table;
-- see APPENDIX B/C in the API reference for the full name → index mapping.
function this.SetCamoValue(camoType, materialType, value)
    return V_FrameWork.SetCamoValue(camoType, materialType, value)
end

function this.CloneCamoRow(dstCamoType, srcCamoType)
    return V_FrameWork.CloneCamoRow(dstCamoType, srcCamoType)
end

function this.ImportCamoRow(camoType, valuesTable)
    return V_FrameWork.ImportCamoRow(camoType, valuesTable)
end

-- Voice FPK overrides per player type (0=Snake, 1=DD_Male, 2=DD_Female, 3=Avatar).
function this.SetPlayerVoiceFpkPathForType(playerType, path)
    V_FrameWork.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(playerType)
    V_FrameWork.ClearPlayerVoiceFpkPathForType(playerType)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.ClearAllPlayerVoiceFpkOverrides()
end

return this
