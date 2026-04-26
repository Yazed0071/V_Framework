--==============================================================================
-- V_FrameWork outfit example — FULL FEATURE COMBINATION
--==============================================================================
-- Single registration that uses every Phase-2/Phase-3 feature:
--   * Custom body (parts + fpk)
--   * Custom camo + camo FV2
--   * Custom diamond + diamond FV2
--   * Vanilla face + arm (passes through to vanilla face/arm system)
--   * Disabled skin tone override
--   * HEAD OPTION submenu enabled with 4 vanilla head equipIds
--   * 3 variants (cycle in mission prep with the variant button)
--   * R&D table entry (cost, time, lang strings, icon)
--==============================================================================

function this.OnAllocate()
    -- developId and flowIndex auto-allocated under the `name`.
    -- The wrapper auto-fills develop.const.p00 / .p50 with the allocated
    -- ids before forwarding to V_FrameWork.AddToEquipDevelopTable, so the
    -- R&D table entry stays in sync with the outfit registration.
    local partsType, devId, flowIndex = V_TppPlayer.AddOutfit{
        --
        -- Identity
        --
        name       = "MyMod:NeonSuitFull",
        playerType = "DDFemale",

        --
        -- Required body
        --
        partsPath = "/Assets/tpp/parts/chara/neon/neon_full_v00.parts",
        fpkPath   = "/Assets/tpp/pack/player/parts/neon_full_v00.fpk",

        --
        -- Sub-assets
        --
        camoFpk    = "/Assets/tpp/pack/player/parts/neon_full_camo.fpk",
        camoFv2    = "/Assets/tpp/fova/chara/neon/neon_full_camo.fv2",
        diamondFpk = "/Assets/tpp/pack/player/parts/neon_full_diamond.fpk",
        diamondFv2 = "/Assets/tpp/fova/chara/neon/neon_full_diamond.fv2",
        faceFpk    = true,                  -- vanilla face
        armFpk     = true,                  -- vanilla arm
        skinFv2    = false,                 -- disable skin tone override

        --
        -- Head options
        --
        supportsHeadOptions = true,
        headOptions = {
            0x17CA, 0x17CB, 0x17CC, 0x17CD,
        },

        --
        -- Variants (cycle button in mission prep)
        --
        variants = {
            { fpkPath = "/Assets/tpp/pack/player/parts/neon_full_dark.fpk",
              camoFpk = "/Assets/tpp/pack/player/parts/neon_full_dark_camo.fpk" },

            { fpkPath = "/Assets/tpp/pack/player/parts/neon_full_red.fpk",
              camoFpk = "/Assets/tpp/pack/player/parts/neon_full_red_camo.fpk" },

            { partsPath = "/Assets/tpp/parts/chara/neon/neon_full_v01.parts",
              fpkPath   = "/Assets/tpp/pack/player/parts/neon_full_v01.fpk",
              camoFpk   = "/Assets/tpp/pack/player/parts/neon_full_v01_camo.fpk" },
        },

        --
        -- R&D table entry — full set of const + flow params. The wrapper
        -- auto-fills const.p00 (developId) and const.p50 (flowIndex) from
        -- the values it allocated above, so don't set those manually.
        --
        -- Every field is OPTIONAL — you only need the ones that affect
        -- the behavior you want. Unset fields default to 0 / nil. Both
        -- the readable name (e.g. `langEquipName`) and the raw alias
        -- (e.g. `p06`) are accepted.
        --
        develop = {
            -- ----------------------------------------------------------
            -- const block (p01..p36) — equipment identity, lang, icon
            -- ----------------------------------------------------------
            const = {
                -- p00 = developId   (auto-filled by wrapper)
                equipID              = TppEquip.EQP_SUIT,                              -- p01: bound equipId
                equipDevelopTypeID   = TppMbDev.EQP_DEV_TYPE_Suit,     -- p02: develop type bucket
                baseEquipDevelopId   = 0,                              -- p03: prerequisite developId
                skill                = 0,                              -- p04: required skill id
                bluePrintId          = 0,                              -- p05: blueprint id (if any)
                langEquipName        = "staff_hideo_kojima",            -- p06: name string
                langEquipInfo        = "staff_hideo_kojima",            -- p07: description string
                iconFtexPath         = "/Assets/tpp/ui/texture/EquipIcon/buddy/ui_qwp_suit_qui0_alp",  -- p08
                equipDevelopGroupID  = 0,                              -- p09: develop UI group

                -- p10..p21: power-up info string ids (12 slots, one per
                -- visible "power-up" line on the R&D info panel). Set
                -- only the ones you actually populate.
                langPowerUpInfo0     = nil,
                langPowerUpInfo1     = nil,
                langPowerUpInfo2     = nil,
                langPowerUpInfo3     = nil,
                langPowerUpInfo4     = nil,
                langPowerUpInfo5     = nil,
                langPowerUpInfo6     = nil,
                langPowerUpInfo7     = nil,
                langPowerUpInfo8     = nil,
                langPowerUpInfo9     = nil,
                langPowerUpInfo10    = nil,
                langPowerUpInfo11    = nil,

                -- p30..p36: flags + secondary lang
                langEquipRealName    = "staff_hideo_kojima",                 -- p30
                isResultRankLimited  = 0,                              -- p31
                isCustomEnable       = 0,                              -- p32
                isColorChangeEnable  = 0,                              -- p33
                unk34                = 0,                              -- p34
                isSecurityStaffEquip = 0,                              -- p35: 1 = staff-uniform-style
                unk36                = 0,                              -- p36

                -- p50 = flowIndex   (auto-filled by wrapper)
            },

            -- ----------------------------------------------------------
            -- flow block (p51..p74) — cost, time, requirements
            -- ----------------------------------------------------------
            flow = {
                sideGrade               = 0,                           -- p51: side-grade level
                grade                   = 1,                           -- p52: equipment grade/tier
                developGmpCost          = 50000,                       -- p53: GMP to develop
                usageGmpCost            = 0,                           -- p54: GMP per deploy

                sectionLvForDevelop     = 0,                           -- p55: required section level
                sectionID2ForDevelop    = 0,                           -- p56: secondary section id
                sectionLv2ForDevelop    = 0,                           -- p57: secondary section level

                resourceType1           = "",                    -- p58: primary resource id (string)
                resourceType1Count      = 0,                          -- p59: primary resource amount
                resourceType2           = "",                         -- p60: secondary resource id
                resourceType2Count      = 0,                           -- p61: secondary resource amount

                initialAvailable        = 0,                           -- p62: 0 = locked until researched
                sectionIDForDevelop     = 0,                           -- p63: primary section id
                developSectionLv        = 0,                           -- p64: develop section level

                resourceUsageType1      = nil,                         -- p65: per-use resource id 1
                resourceUsageType1Count = 0,                           -- p66: per-use resource amount 1
                resourceUsageType2      = nil,                         -- p67: per-use resource id 2
                resourceUsageType2Count = 0,                           -- p68: per-use resource amount 2

                displayInfo             = 0,                           -- p69: display flag bits
                unk70                   = 0,                           -- p70: unknown
                developTimeMinute       = 0,                          -- p71: minutes to complete
                isValidMbCoin           = 0,                           -- p72: 1 = MB Coin payable
                intimacyPoint           = 0,                           -- p73: buddy intimacy requirement
                isFobAvailable          = 0,                           -- p74: 1 = available in FOB
            },
        },
    }

    if not partsType then
        V_FrameWork.Log("OutfitFullExample: registration failed")
        return
    end

    V_FrameWork.Log(string.format(
        "OutfitFullExample: registered partsType=0x%02X devId=%d flowIndex=%d",
        partsType, devId, flowIndex))

    -- Verify allocated values via the query API.
    local info = V_TppPlayer.GetOutfitInfo(devId)
    if info then
        V_FrameWork.Log(string.format(
            "  flowIndex=%d  playerType=%d  selector=0x%02X  variantCount=%d  hasHeadOpts=%s",
            info.flowIndex, info.playerType, info.selectorCode,
            info.variantCount, tostring(info.supportsHeadOptions)))
    end
end

-- Optional: switch to variant 1 (dark) programmatically. Capture
-- `devId` from the AddOutfit return above and pass it here.
-- V_TppPlayer.SetOutfitVariant(devId, 1)
