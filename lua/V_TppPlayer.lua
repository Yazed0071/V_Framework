local this = {}
local StrCode32 = Fox.StrCode32
local StrCode32Table = Tpp.StrCode32Table
local IsTypeString = Tpp.IsTypeString
local IsTypeTable  = Tpp.IsTypeTable

local function LogError(functionName, message)
    InfCore.Log("[V_FrameWork] [" .. tostring(functionName) .. "] ERROR: #### " .. tostring(message) .. " ####")
end

local function RequireString(functionName, valueName, value)
    if not IsTypeString(value) then
        LogError(functionName, valueName .. " must be a string!")
        return false
    end
    return true
end

local function RequireTable(functionName, valueName, value)
    if not IsTypeTable(value) then
        LogError(functionName, valueName .. " must be a table!")
        return false
    end
    return true
end

function this.SetPlayerVoiceFpkPathForType(playerType, path)
    V_FrameWork.SetPlayerVoiceFpkPathForType(playerType, path)
end

function this.ClearPlayerVoiceFpkPathForType(type)
    V_FrameWork.ClearPlayerVoiceFpkPathForType(type)
end

function this.ClearAllPlayerVoiceFpkOverrides()
    V_FrameWork.ClearAllPlayerVoiceFpkOverrides()
end

function this.AddToEquipDevelopTable(equipName, developData)
    if not RequireString("AddToEquipDevelopTable", "equipName", equipName) then
        return
    end

    if not RequireTable("AddToEquipDevelopTable", "developData", developData) then
        return
    end

    if not RequireTable("AddToEquipDevelopTable", "developData.const", developData.const) then
        return
    end

    if not RequireTable("AddToEquipDevelopTable", "developData.flow", developData.flow) then
        return
    end

    return V_FrameWork.AddToEquipDevelopTable(equipName, developData)
end

function this.AddOutfit(outfitData)
    if not RequireTable("AddOutfit", "outfitData", outfitData) then
        return
    end

    if outfitData.playerType == nil then
        LogError("AddOutfit", "playerType is required!")
        return
    end

    if not RequireString("AddOutfit", "partsPath", outfitData.partsPath) then
        return
    end

    if not RequireString("AddOutfit", "fpkPath", outfitData.fpkPath) then
        return
    end

    if not RequireTable("AddOutfit", "develop", outfitData.develop) then
        return
    end

    -- Default lang fields if not provided
    if outfitData.develop.const then
        local c = outfitData.develop.const
        c.langEquipName     = c.langEquipName     or "sub_mission_untitle"
        c.langEquipInfo     = c.langEquipInfo     or "sub_mission_untitle"
        c.langEquipRealName = c.langEquipRealName or "sub_mission_untitle"
    end

    -- Auto-generate unique key from playerType + all paths
    local autoKey = tostring(outfitData.playerType)
        .. ":" .. outfitData.partsPath
        .. ":" .. outfitData.fpkPath
        .. (outfitData.fv2Path and (":" .. outfitData.fv2Path) or "")
        .. (outfitData.fv2FpkPath and (":" .. outfitData.fv2FpkPath) or "")

    -- Derive camo enable: true if FV2 paths or camo gameplay values are provided
    local hasCamo = (outfitData.fv2FpkPath ~= nil)
        or (outfitData.camoValues ~= nil)
        or (outfitData.camoCloneFrom ~= nil)

    -- Register the suit
    local partsType = V_FrameWork.SetPlayerPartsPath{
        playerType = outfitData.playerType,
        enableHead = outfitData.enableHead,
        enableHand = outfitData.enableHand,
        enableCamo = hasCamo,
        partsPath  = outfitData.partsPath,
        fpkPath    = outfitData.fpkPath,
        fv2Path    = outfitData.fv2Path,
        fv2FpkPath = outfitData.fv2FpkPath,
    }

    if not partsType then
        LogError("AddOutfit", "SetPlayerPartsPath failed!")
        return
    end

    -- Register the develop entry with auto key
    local developId = this.AddToEquipDevelopTable(autoKey, outfitData.develop)

    if not developId then
        LogError("AddOutfit", "AddToEquipDevelopTable failed!")
        return partsType
    end

    -- Link them
    V_FrameWork.LinkDevelopIdToPlayerSuit(developId, partsType)

    -- Handle body variants (zipped/unzipped/scarfed/naked etc.)
    if outfitData.variants and type(outfitData.variants) == "table" and #outfitData.variants > 0 then
        local groupId = V_FrameWork.AllocateVariantGroupId()
        if groupId then
            -- Base outfit is variant index 0
            V_FrameWork.SetVariantGroup(partsType, groupId, 0)

            for i, variant in ipairs(outfitData.variants) do
                if variant.partsPath and variant.fpkPath then
                    local variantPartsType = V_FrameWork.SetPlayerPartsPath{
                        playerType = outfitData.playerType,
                        enableHead = variant.enableHead ~= nil and variant.enableHead or outfitData.enableHead,
                        enableHand = variant.enableHand ~= nil and variant.enableHand or outfitData.enableHand,
                        partsPath  = variant.partsPath,
                        fpkPath    = variant.fpkPath,
                        fv2Path    = variant.fv2Path,
                        fv2FpkPath = variant.fv2FpkPath,
                    }

                    if variantPartsType then
                        -- All variants share the same develop entry
                        V_FrameWork.LinkDevelopIdToPlayerSuit(developId, variantPartsType)
                        V_FrameWork.SetVariantGroup(variantPartsType, groupId, i)

                        InfCore.Log("[AddOutfit] Variant " .. i .. " partsType=" .. variantPartsType
                            .. " group=" .. groupId .. " langId=" .. tostring(variant.langId))
                    end
                end
            end
        end
    end

    -- Apply camo gameplay values if provided
    if outfitData.camoCloneFrom ~= nil then
        V_FrameWork.CloneCamoRow(partsType, outfitData.camoCloneFrom)
    end

    if outfitData.camoValues and type(outfitData.camoValues) == "table" then
        V_FrameWork.ImportCamoRow(partsType, outfitData.camoValues)
    end

    return partsType, developId
end

-- Sets one cell in the camo effectiveness table.
-- camoType and materialType accept numbers or string names.
-- Example: this.SetCamoValue("OLIVEDRAB", "MTR_LEAF", 50)
function this.SetCamoValue(camoType, materialType, value)
    return V_FrameWork.SetCamoValue(camoType, materialType, value)
end

-- Copies all 82 material values from one camo row to another.
-- Example: this.CloneCamoRow("BLACK", "OLIVEDRAB")
function this.CloneCamoRow(dstCamoType, srcCamoType)
    return V_FrameWork.CloneCamoRow(dstCamoType, srcCamoType)
end

return this
