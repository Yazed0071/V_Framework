--==============================================================================
-- V_FrameWork outfit example — FULL FEATURE COMBINATION
--==============================================================================
-- Single registration that exercises every feature. Important to know:
-- only `name` and `develop` are shared across playerType branches. EVERYTHING
-- else (paths, sub-assets, variants, head options, behavior flags, lang
-- name, camo bonus) lives INSIDE each per-PT branch and is independent.
--
-- Sub-asset value forms (camoFpk / camoFv2 / faceFpk / skinFv2 / diamondFpk /
-- diamondFv2 / voiceFpk):
--   string  → custom asset path (e.g. "/Assets/.../my_face.fpk")
--   true    → use vanilla asset (load whatever the engine would normally use
--             for this PT/partsType). Default for face / skin / voice / *Fv2.
--   false   → disable: load nothing for this slot. Use to strip out the
--             vanilla face on a no-head body, the skin tone override, etc.
--   nil     → per-field default (camoFpk / diamondFpk default to disabled;
--             face / skin / voice / *Fv2 default to vanilla).
--==============================================================================

function this.LoadLibraries()
    local partsType, devId, flowIndex = V_TppPlayer.AddOutfit{
        --
        -- Identity (the only outfit-level required field).
        --
        name = "MyMod:NeonSuitFull",                                             -- Required

        --
        -- DDFemale branch.
        --
        ddFemale = {
            partsPath  = "/Assets/tpp/parts/chara/neon/neon_full_v00.parts",      -- Required
            fpkPath    = "/Assets/tpp/pack/player/parts/neon_full_v00.fpk",       -- Required

            camoFpk    = "/Assets/tpp/pack/player/parts/neon_full_camo.fpk",      -- Optional
            camoFv2    = "/Assets/tpp/fova/chara/neon/neon_full_camo.fv2",        -- Optional

            diamondFpk = "/Assets/tpp/pack/player/parts/neon_full_diamond.fpk",   -- Optional
            diamondFv2 = "/Assets/tpp/fova/chara/neon/neon_full_diamond.fv2",     -- Optional

            voiceFpk   = "/Assets/tpp/pack/player/parts/neon_full_voice.fpk",     -- Optional

            faceFpk    = "/Assets/tpp/pack/player/parts/neon_face.fpk",           -- Optional
            skinFv2    = "/Assets/tpp/fova/chara/neon/neon_skin.fv2",             -- Optional

            enableArm  = true,                                                    -- Optional
            enableHead = false,                                                   -- Optional

            langEquipName = "name_neon_suit_ddf",                                 -- Optional

            headOptions = {                                                       -- Optional
                "NONE",
                "BALACLAVA",
                "SP-HEADGEAR",
                "HP-HEADGEAR",
            },

            camoBonusValues = {                                                   -- Optional (per-PT custom 82-material row, OR use camoBonusType)
                MTR_IRON_A = 80, MTR_IRON_B = 80,
                MTR_PIPE_A = 70, MTR_PIPE_B = 70,
                MTR_CONC_A = 65, MTR_CONC_B = 65,
                MTR_LEAF   = -20, MTR_RLEF = -20,
                MTR_PLNT_A = -15,
            },

            variants = {                                                          -- Optional (cycle button alternates 1..N)
                {
                    fpkPath     = "/Assets/tpp/pack/player/parts/neon_full_dark.fpk",         -- Optional in variant
                    camoFpk     = "/Assets/tpp/pack/player/parts/neon_full_dark_camo.fpk",    -- Optional
                    displayName = "name_neon_dark",                                            -- Optional
                },
                {
                    fpkPath     = "/Assets/tpp/pack/player/parts/neon_full_red.fpk",          -- Optional
                    camoFpk     = "/Assets/tpp/pack/player/parts/neon_full_red_camo.fpk",     -- Optional
                    displayName = "name_neon_red",                                             -- Optional
                },
                {
                    partsPath   = "/Assets/tpp/parts/chara/neon/neon_full_v01.parts",         -- Optional
                    fpkPath     = "/Assets/tpp/pack/player/parts/neon_full_v01.fpk",          -- Optional
                    camoFpk     = "/Assets/tpp/pack/player/parts/neon_full_v01_camo.fpk",     -- Optional
                    voiceFpk    = "/Assets/tpp/pack/player/parts/neon_full_v01_voice.fpk",    -- Optional
                    displayName = "name_neon_alt",                                             -- Optional
                },
            },
        },

        --
        -- Snake branch — totally independent params. Different paths,
        -- different head options (BANDANA family), different camo bonus
        -- (vanilla BATTLEDRESS camo's profile this time), different variant set.
        --
        snake = {
            partsPath = "/Assets/tpp/parts/chara/neon/neon_snake_v00.parts",      -- Required
            fpkPath   = "/Assets/tpp/pack/player/parts/neon_snake_v00.fpk",       -- Required

            camoFpk   = "/Assets/tpp/pack/player/parts/neon_snake_camo.fpk",      -- Optional
            voiceFpk  = "/Assets/tpp/pack/player/parts/neon_snake_voice.fpk",     -- Optional

            enableArm  = true,                                                    -- Optional
            enableHead = false,                                                   -- Optional

            langEquipName = "name_neon_suit_snake",                               -- Optional

            headOptions = {                                                       -- Optional
                "NONE",
                "BANDANA",
                "INFINITE BANDANA",
            },

            camoBonusType = PlayerCamoType.BATTLEDRESS,                           -- Optional

            variants = {                                                          -- Optional
                {
                    fpkPath     = "/Assets/tpp/pack/player/parts/neon_snake_v00_dark.fpk",  -- Optional
                    displayName = "name_neon_dark",                                          -- Optional
                },
            },
        },

        -- Avatar omitted — bridges to Snake automatically.
        -- DDMale omitted — outfit unavailable for that PT.

        --
        -- R&D table entry (the second of the two cross-PT shared fields).
        -- The wrapper auto-fills const.p00 / const.p50 with the allocated
        -- developId / flowIndex.
        --
        develop = {                                                               -- Optional (R&D table entry)
            const = {
                -- p00 = developId   (auto-filled by wrapper)
                equipID              = TppEquip.EQP_SUIT,                                          -- Optional (p01: bound equipId)
                equipDevelopTypeID   = TppMbDev.EQP_DEV_TYPE_Suit,                                 -- Optional (p02: develop-type bucket)
                baseEquipDevelopId   = 0,                                                          -- Optional (p03: prerequisite developId)
                skill                = 0,                                                          -- Optional (p04: required skill id)
                bluePrintId          = 0,                                                          -- Optional (p05: blueprint id)
                langEquipName        = "name_neon_suit",                                           -- Optional (p06: name LangId)
                langEquipInfo        = "info_neon_suit",                                           -- Optional (p07: description LangId)
                iconFtexPath         = "/Assets/mod/ui/icon/neon.ftex",                            -- Optional (p08: icon ftex path)
                equipDevelopGroupID  = 0,                                                          -- Optional (p09: develop UI group)

                -- p10..p21: 12 power-up info-line LangIds shown on the R&D info panel.
                langPowerUpInfo0     = nil,                                                        -- Optional (p10)
                langPowerUpInfo1     = nil,                                                        -- Optional (p11)
                langPowerUpInfo2     = nil,                                                        -- Optional (p12)
                langPowerUpInfo3     = nil,                                                        -- Optional (p13)
                langPowerUpInfo4     = nil,                                                        -- Optional (p14)
                langPowerUpInfo5     = nil,                                                        -- Optional (p15)
                langPowerUpInfo6     = nil,                                                        -- Optional (p16)
                langPowerUpInfo7     = nil,                                                        -- Optional (p17)
                langPowerUpInfo8     = nil,                                                        -- Optional (p18)
                langPowerUpInfo9     = nil,                                                        -- Optional (p19)
                langPowerUpInfo10    = nil,                                                        -- Optional (p20)
                langPowerUpInfo11    = nil,                                                        -- Optional (p21)

                -- p30..p36: secondary lang + flags
                langEquipRealName    = "name_neon_suit",                                           -- Optional (p30: short LangId)
                isResultRankLimited  = 0,                                                          -- Optional (p31: 1 = rank-locked)
                isCustomEnable       = 0,                                                          -- Optional (p32)
                isColorChangeEnable  = 0,                                                          -- Optional (p33)
                unk34                = 0,                                                          -- Optional (p34)
                isSecurityStaffEquip = 0,                                                          -- Optional (p35: 1 = staff-uniform style)
                unk36                = 0,                                                          -- Optional (p36)
                -- p50 = flowIndex   (auto-filled by wrapper)
            },
            flow = {
                sideGrade               = 0,                                                       -- Optional (p51: side-grade level)
                grade                   = 1,                                                       -- Optional (p52: equipment grade/tier)
                developGmpCost          = 50000,                                                   -- Optional (p53: GMP to develop)
                usageGmpCost            = 0,                                                       -- Optional (p54: GMP per deploy)

                sectionLvForDevelop     = 0,                                                       -- Optional (p55: required section level)
                sectionID2ForDevelop    = 0,                                                       -- Optional (p56: secondary section id)
                sectionLv2ForDevelop    = 0,                                                       -- Optional (p57: secondary section level)

                resourceType1           = "COMMON",                                                -- Optional (p58: primary resource id)
                resourceType1Count      = 10,                                                      -- Optional (p59: primary resource amount)
                resourceType2           = "",                                                      -- Optional (p60: secondary resource id)
                resourceType2Count      = 0,                                                       -- Optional (p61: secondary resource amount)

                initialAvailable        = 0,                                                       -- Optional (p62: 0 = locked until researched)
                sectionIDForDevelop     = 0,                                                       -- Optional (p63: primary section id)
                developSectionLv        = 0,                                                       -- Optional (p64: develop section level)

                resourceUsageType1      = nil,                                                     -- Optional (p65: per-use resource id 1)
                resourceUsageType1Count = 0,                                                       -- Optional (p66: per-use resource amount 1)
                resourceUsageType2      = nil,                                                     -- Optional (p67: per-use resource id 2)
                resourceUsageType2Count = 0,                                                       -- Optional (p68: per-use resource amount 2)

                displayInfo             = 0,                                                       -- Optional (p69: display flag bits)
                unk70                   = 0,                                                       -- Optional (p70)
                developTimeMinute       = 20,                                                      -- Optional (p71: minutes to complete)
                isValidMbCoin           = 0,                                                       -- Optional (p72: 1 = MB Coin payable)
                intimacyPoint           = 0,                                                       -- Optional (p73: buddy intimacy req.)
                isFobAvailable          = 0,                                                       -- Optional (p74: 1 = available in FOB)
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
end

-- Optional: switch to variant 1 (dark) programmatically. Capture
-- `devId` from the AddOutfit return above.
-- V_TppPlayer.SetOutfitVariant(devId, 1)
