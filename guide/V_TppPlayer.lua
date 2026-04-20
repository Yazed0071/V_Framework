-- V_TppPlayer — player outfits, voice overrides, camo gameplay values.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

-- Logging helper. Prefers V_FrameWork.Log so Lua-side messages land in
-- mod\V_FrameWork\V_FrameWork_log.txt alongside the C++ hook logs (unified
-- chronological view). Falls back to InfCore.Log (writes to ih_log.txt) when
-- the binding isn't present, and silently no-ops otherwise — so scripts run
-- even in environments without either logger.
local function log(msg)
    local line = "[V_TppPlayer] " .. tostring(msg)
    if V_FrameWork and V_FrameWork.Log then
        V_FrameWork.Log(line)
    elseif InfCore and InfCore.Log then
        InfCore.Log(line)
    end
end

-- Emit a visible warning for keys the user passed that the current wrapper
-- doesn't consume. Helps catch typos / aspirational options before they get
-- silently dropped (e.g. old scripts passing `camoCloneFrom`).
local SUPPORTED_OUTFIT_KEYS = {
    name        = true, playerType = true,
    partsPath   = true, fpkPath    = true,
    enableHead  = true, enableHand = true,
    fv2Path     = true, fv2FpkPath = true,
    faceFpk     = true, armFpk     = true, diamondFpk = true, camoFpk = true,
    variants    = true, develop    = true,
    -- Per-variant-only keys (allowed at variant level, warned at outfit level only)
    langId      = false, langInfo = false, langRealName = false, iconFtexPath = false,
}

local SUPPORTED_VARIANT_KEYS = {
    name        = true, playerType = true,
    partsPath   = true, fpkPath    = true,
    enableHead  = true, enableHand = true,
    fv2Path     = true, fv2FpkPath = true,
    faceFpk     = true, armFpk     = true, diamondFpk = true, camoFpk = true,
    langId      = true, langInfo   = true, langRealName = true, iconFtexPath = true,
    develop     = true,  -- allow full per-variant develop override
}

local function warnUnknownKeys(tbl, allowed, where)
    if type(tbl) ~= "table" then return end
    for k, _ in pairs(tbl) do
        if allowed[k] == nil then
            if k == "camoCloneFrom" then
                log(string.format(
                    "WARN %s: 'camoCloneFrom' is not a supported key. " ..
                    "For a custom camo pattern, pass camoFpk = '<path>'. " ..
                    "For camo-table material edits use V_TppPlayer.CloneCamoRow. Ignoring.",
                    where))
            else
                log(string.format("WARN %s: unknown key '%s' ignored", where, tostring(k)))
            end
        elseif allowed[k] == false then
            log(string.format(
                "WARN %s: '%s' belongs on a variant, not the outfit root. Ignoring here.",
                where, tostring(k)))
        end
    end
end

-- Build a stable develop-key from the outfit's identity fields. Used when the
-- user doesn't pass an explicit `name` — (playerType, partsPath, fpkPath)
-- together uniquely identify an outfit body, so the persisted developId in
-- mod\V_FrameWork\V_FrameWork_State.lua survives Lua script reloads.
--
-- Users can still pass `name = "MyMod:MySuit"` to get a short, readable key;
-- this is purely the automatic fallback.
local function autoDevelopKey(outfitData)
    return string.format("suit:%s:%s:%s",
        tostring(outfitData.playerType or "?"),
        tostring(outfitData.partsPath or ""),
        tostring(outfitData.fpkPath or ""))
end

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
        camoFpk    = src.camoFpk,
    }
end

-- Shallow-clone a develop table (const + flow sub-tables) so per-variant
-- overrides don't mutate the base. Variant's langId / langInfo / langRealName /
-- iconFtexPath override the corresponding fields in const when provided.
--
-- Variants are treated as free sub-items of the outfit family: developing the
-- base once grants all variants. This wrapper zeroes out the variant's GMP and
-- resource costs and sets initialAvailable = 1 so the variant is equippable
-- from the start. Users who WANT a paid variant can override via
-- variant.develop = { flow = { developGmpCost = 10000, initialAvailable = 0 } }.
local function cloneDevelopForVariant(baseDevelop, variant)
    local function cloneSub(sub)
        local c = {}
        if type(sub) == "table" then
            for k, v in pairs(sub) do c[k] = v end
        end
        return c
    end

    local result = {
        const = cloneSub(baseDevelop.const),
        flow  = cloneSub(baseDevelop.flow),
    }

    -- Default variants to "already developed, zero cost". You paid for the
    -- outfit family by developing the base; variants cycle for free.
    result.flow.developGmpCost          = 0
    result.flow.usageGmpCost            = 0
    result.flow.resourceType1           = ""
    result.flow.resourceType1Count      = 0
    result.flow.resourceType2           = ""
    result.flow.resourceType2Count      = 0
    result.flow.resourceUsageType1      = ""
    result.flow.resourceUsageType1Count = 0
    result.flow.resourceUsageType2      = ""
    result.flow.resourceUsageType2Count = 0
    result.flow.developTimeMinute       = 0
    result.flow.initialAvailable        = 1

    -- Variant-level langId overrides the base's langEquipName only for this
    -- variant's develop entry.
    if variant.langId       then result.const.langEquipName     = variant.langId       end
    if variant.langInfo     then result.const.langEquipInfo     = variant.langInfo     end
    if variant.langRealName then result.const.langEquipRealName = variant.langRealName end
    if variant.iconFtexPath then result.const.iconFtexPath      = variant.iconFtexPath end

    -- Variant may also fully override develop via a `develop` sub-table.
    -- This override runs AFTER the free-variant defaults, so users can
    -- explicitly request a paid variant if they want.
    if type(variant.develop) == "table" then
        if type(variant.develop.const) == "table" then
            for k, v in pairs(variant.develop.const) do result.const[k] = v end
        end
        if type(variant.develop.flow) == "table" then
            for k, v in pairs(variant.develop.flow) do result.flow[k] = v end
        end
    end

    return result
end

-- Register a custom player outfit. Orchestrates:
--   1. SetPlayerPartsPath (base suit)             → partsType
--   2. AddToEquipDevelopTable (R&D entry)         → developId
--   3. LinkDevelopIdToPlayerSuit                  (glues develop → partsType, resolves flowIndex)
--   4. For each variant:
--        4a. SetPlayerPartsPath (variant body)    → variantPartsType
--        4b. If any variant has langId/langInfo/langRealName/iconFtexPath, register a
--            per-variant develop entry so R&D shows distinct names. Otherwise variants
--            share the base's developId.
--        4c. SetVariantGroup links the variant into the base's group.
--
-- Returns (partsType, developId) on success, or false on failure.
--
-- Every step writes a log line via V_FrameWork.Log (→ mod\V_FrameWork\V_FrameWork_log.txt)
-- so issues are visible in the unified log alongside the C++ hook events
-- without needing breakpoints. Falls back to InfCore.Log (→ ih_log.txt) when
-- V_FrameWork.Log isn't available.
function this.AddOutfit(outfitData)
    if type(outfitData) ~= "table" then
        log("AddOutfit: outfitData is not a table — abort")
        return false
    end
    if not outfitData.partsPath or not outfitData.fpkPath then
        log("AddOutfit: missing partsPath or fpkPath — abort")
        return false
    end
    if type(outfitData.develop) ~= "table" then
        log("AddOutfit: 'develop' is missing or not a table — abort")
        return false
    end

    warnUnknownKeys(outfitData, SUPPORTED_OUTFIT_KEYS, "outfit")

    log(string.format(
        "AddOutfit start name=%s playerType=%s enableHead=%s enableHand=%s parts=%s fpk=%s",
        tostring(outfitData.name or "(unnamed)"),
        tostring(outfitData.playerType),
        tostring(outfitData.enableHead ~= false),
        tostring(outfitData.enableHand ~= false),
        tostring(outfitData.partsPath),
        tostring(outfitData.fpkPath)
    ))

    -- 1. Base parts
    local partsType = V_FrameWork.SetPlayerPartsPath(
        buildPartsTable(outfitData, outfitData.playerType))
    if not partsType or partsType == false then
        log("AddOutfit: SetPlayerPartsPath FAILED for base — abort")
        return false
    end
    log(string.format("AddOutfit base partsType=0x%02X", partsType))

    -- 2. Base develop entry. Use explicit `name` when given, otherwise derive
    -- a stable key from (playerType, partsPath, fpkPath) so the persisted
    -- developId is reused across script reloads without the user having to
    -- name every outfit.
    local developKey = outfitData.name or autoDevelopKey(outfitData)
    local developId = V_FrameWork.AddToEquipDevelopTable(developKey, outfitData.develop)
    if not developId or developId == false or developId == 0 then
        log(string.format(
            "AddOutfit: AddToEquipDevelopTable FAILED for key=%s — parts registered but no develop entry",
            developKey))
        return partsType
    end
    log(string.format("AddOutfit base developId=%d key=%s", developId, developKey))

    -- 3. Link base develop → parts (auto-resolves flowIndex internally)
    V_FrameWork.LinkDevelopIdToPlayerSuit(developId, partsType)
    log(string.format("AddOutfit linked base developId=%d -> partsType=0x%02X",
        developId, partsType))

    -- 4. Variants
    if type(outfitData.variants) == "table" and #outfitData.variants > 0 then
        local groupId = V_FrameWork.AllocateVariantGroupId()
        if not groupId or groupId == false or groupId == 0 then
            log("AddOutfit: AllocateVariantGroupId FAILED — variants skipped")
            return partsType, developId
        end

        log(string.format(
            "AddOutfit variant-group groupId=%d size=%d (base + %d)",
            groupId,
            #outfitData.variants + 1,
            #outfitData.variants
        ))

        V_FrameWork.SetVariantGroup(partsType, groupId, 0)
        log(string.format("AddOutfit base in group=%d index=0", groupId))

        -- Each variant MUST get its own AddToEquipDevelopTable + Link call,
        -- even when langId/etc are absent. Without it the variant's
        -- CustomSuitEntry.linkedFlowIndex stays 0xFFFF, and the C++
        -- hkAddListSuit sub-slot emission would write 0xFFFF into the UI's
        -- flow-index array → crash on sortie commit. Per-variant develop
        -- inherits the base's const/flow when the variant doesn't override
        -- fields, so "shared naming" is achieved implicitly.
        for i, variant in ipairs(outfitData.variants) do
            if type(variant) ~= "table" then
                log(string.format("AddOutfit variant[%d] is not a table — skipping", i))
            else
                warnUnknownKeys(variant, SUPPORTED_VARIANT_KEYS,
                    string.format("variant[%d]", i))

                local variantTable = buildPartsTable(variant, outfitData.playerType)
                if variant.enableHead == nil then
                    variantTable.enableHead = outfitData.enableHead ~= false
                end
                if variant.enableHand == nil then
                    variantTable.enableHand = outfitData.enableHand ~= false
                end

                if not variantTable.partsPath or not variantTable.fpkPath then
                    log(string.format(
                        "AddOutfit variant[%d] missing partsPath or fpkPath — skipping", i))
                else
                    local variantPartsType = V_FrameWork.SetPlayerPartsPath(variantTable)
                    if not variantPartsType or variantPartsType == false then
                        log(string.format(
                            "AddOutfit variant[%d] SetPlayerPartsPath FAILED — skipping", i))
                    else
                        log(string.format(
                            "AddOutfit variant[%d] partsType=0x%02X parts=%s",
                            i, variantPartsType, tostring(variantTable.partsPath)))

                        -- Must register develop BEFORE linking — link resolves
                        -- flowIndex from developId.
                        local variantKey = variant.name
                            or (developKey .. ":v" .. i)
                        local variantDevelop =
                            cloneDevelopForVariant(outfitData.develop, variant)

                        local variantDevelopId = V_FrameWork.AddToEquipDevelopTable(
                            variantKey, variantDevelop)

                        if not variantDevelopId
                            or variantDevelopId == false
                            or variantDevelopId == 0 then
                            log(string.format(
                                "AddOutfit variant[%d] AddToEquipDevelopTable FAILED key=%s — variant WILL NOT CYCLE (skipping group attach to avoid 0xFFFF flowIndex crash)",
                                i, variantKey))
                        else
                            log(string.format(
                                "AddOutfit variant[%d] developId=%d key=%s langId=%s",
                                i, variantDevelopId, variantKey,
                                tostring(variant.langId or "(inherited from base)")))

                            V_FrameWork.LinkDevelopIdToPlayerSuit(
                                variantDevelopId, variantPartsType)
                            log(string.format(
                                "AddOutfit variant[%d] linked developId=%d -> partsType=0x%02X",
                                i, variantDevelopId, variantPartsType))

                            -- Only attach to the group AFTER Link succeeded, so
                            -- hkAddListSuit's sub-slot emission sees a valid
                            -- linkedFlowIndex.
                            V_FrameWork.SetVariantGroup(variantPartsType, groupId, i)
                            log(string.format(
                                "AddOutfit variant[%d] in group=%d index=%d",
                                i, groupId, i))
                        end
                    end
                end
            end
        end
    end

    -- 5. HEAD OPTION entries auto-gate (discovered 2026-04-20).
    --
    -- The sortie-prep HEAD OPTION menu populates vanilla entries
    -- (BALACLAVA/BANDANA/HEADGEAR etc.) only for suits that have a
    -- non-zero variantGroupId. Without this flag the game's internal
    -- HasHeadOptions check returns false and the menu is empty even
    -- when the HEAD OPTION ROW is forced visible — user cannot pick a
    -- head option.
    --
    -- For suits that the user DIDN'T declare as variant groups (i.e.
    -- single-suit outfits with enableHead=true), auto-allocate a lone
    -- variant group so the game treats them as "has head options".
    -- `hkAddListSuit` sub-slot emission skips when group size < 2, so
    -- no spurious UI entries appear; the only observable effect is
    -- that HEAD OPTION entries populate in sortie prep.
    --
    -- Skip for enableHead=false — those suits should hide HEAD OPTION
    -- entirely (handled by hkGetSelectionNum returning 2).
    local enableHead = outfitData.enableHead ~= false
    local hadVariants = (type(outfitData.variants) == "table" and #outfitData.variants > 0)
    if enableHead and not hadVariants and
       V_FrameWork.AllocateVariantGroupId and V_FrameWork.SetVariantGroup then
        local loneGroup = V_FrameWork.AllocateVariantGroupId()
        if loneGroup and loneGroup ~= false and loneGroup > 0 then
            V_FrameWork.SetVariantGroup(partsType, loneGroup, 0)
            log(string.format(
                "AddOutfit auto lone-variant-group=%d for enableHead=true suit (HEAD OPTION gate)",
                loneGroup))
        end
    end

    log(string.format("AddOutfit done name=%s partsType=0x%02X developId=%d",
        tostring(outfitData.name or developKey), partsType, developId))

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
