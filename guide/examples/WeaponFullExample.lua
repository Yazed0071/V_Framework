--==============================================================================
-- V_FrameWork weapon example — FULL FEATURE COMBINATION
--==============================================================================
-- Uses every weapon-related V_TppEquip function with every parameter.
-- Optional parameters are annotated `-- optional, default: <value>` showing
-- what the framework uses when the field is omitted.
--
-- This is a reference-style example: the values are illustrative, not all
-- meaningful for one weapon. In practice, omit any optional field you don't
-- care about — defaults are sensible.
--
-- Convention used below for required vs optional:
--   field = value,    -- REQUIRED                   <- must be set, no default
--   field = value,    -- optional, default: <X>     <- safe to omit
--==============================================================================


--------------------------------------------------------------------------------
-- 1. Reserve a persistent equip ID
--------------------------------------------------------------------------------

local equipId = V_TppEquip.RegisterConstantEquipId(
    "EQP_WP_MyMod_FullExample"   -- equipName, string, REQUIRED
)


--------------------------------------------------------------------------------
-- 2. Declare every weapon-component constant
-- Each Declare* takes one string. The returned numeric ID is also written
-- into the global TppEquip table under the same name.
--------------------------------------------------------------------------------

local wpId  = V_TppEquip.DeclareWPs ("WP_MyMod_FullExample_00")           -- weaponName, string, REQUIRED
local rcId  = V_TppEquip.DeclareRCs ("RC_MyMod_FullExample")              -- receiverName, string, REQUIRED
local baId  = V_TppEquip.DeclareBAs ("BA_MyMod_FullExample_Long")         -- barrelName, string, REQUIRED
local stId  = V_TppEquip.DeclareSTs ("ST_MyMod_FullExample_Folding")      -- stockName, string, REQUIRED
local mzId  = V_TppEquip.DeclareMZs ("MZ_MyMod_FullExample_Suppressor")   -- muzzleName, string, REQUIRED
local skId  = V_TppEquip.DeclareSKs ("SK_MyMod_FullExample_RedDot")       -- sightName, string, REQUIRED
local ubId  = V_TppEquip.DeclareUBs ("UB_MyMod_FullExample_M203")         -- underbarrelName, string, REQUIRED
local swpId = V_TppEquip.DeclareSWPs("EQP_SWP_MyMod_FullExample_Grenade") -- supportWeaponName, string, REQUIRED

-- Ammo: single name OR batch
V_TppEquip.DeclareAMs("AM_MyMod_FullExample_Standard")    -- ammoName, string, REQUIRED
V_TppEquip.DeclareAMs({                                   -- table of strings, REQUIRED
    "AM_MyMod_FullExample_Tracer",
    "AM_MyMod_FullExample_AP",
})
local ammoId = TppEquip.AM_MyMod_FullExample_Standard


--------------------------------------------------------------------------------
-- 3. Register the model + fpk paths against the equipId
-- Each row is a positional 6-tuple — ALL six entries are REQUIRED.
--------------------------------------------------------------------------------

V_TppEquip.AddToEquipIdTable({
    {
        equipId,                                       -- [1] equipId,    int,    REQUIRED
        TppEquip.EQP_TYPE_Assault,                     -- [2] type,       int,    REQUIRED  (TppEquip.EQP_TYPE_*)
        290,                                           -- [3] baseWeapon, int,    REQUIRED  (vanilla weapon to inherit defaults)
        TppEquip.EQP_BLOCK_MISSION,                    -- [4] block,      int,    REQUIRED  (TppEquip.EQP_BLOCK_*)
        "/Assets/tpp/parts/wp/wp_mymod_full.parts",    -- [5] partsPath,  string, REQUIRED
        "/Assets/tpp/pack/wp/wp_mymod_full.fpk",       -- [6] fpkPath,    string, REQUIRED
    },
    -- Add additional rows here for more equip slots in the same call.
})


--------------------------------------------------------------------------------
-- 4. Wire weapon components together
-- Only weaponId is required; every other slot defaults to -1 (no override,
-- inherits from baseWeapon row above).
--------------------------------------------------------------------------------

V_TppEquip.SetGunBasic({
    weaponId       = wpId,    -- int, REQUIRED
    receiverId     = rcId,    -- int, optional, default: -1
    barrelId       = baId,    -- int, optional, default: -1
    ammoId         = ammoId,  -- int, optional, default: -1
    stockId        = stId,    -- int, optional, default: -1
    muzzleId       = mzId,    -- int, optional, default: -1
    muzzleOptionId = 0,       -- int, optional, default: -1  (vanilla muzzle accessory id, or custom MO_*)
    scope1Id       = skId,    -- int, optional, default: -1  (primary scope)
    scope2Id       = 0,       -- int, optional, default: -1  (secondary scope, dual-mount only)
    underBarrelId  = ubId,    -- int, optional, default: -1
    laserFlash1Id  = 0,       -- int, optional, default: -1  (laser/flashlight slot 1)
    laserFlash2Id  = 0,       -- int, optional, default: -1  (laser/flashlight slot 2)
    weaponGrade    = 8,       -- int, optional, default: -1  (clamped to [1..15] when set)
})


--------------------------------------------------------------------------------
-- 5. Per-component fine-tuning via SetEquipParameters
-- Every sub-table is optional — omit any sub-table you don't need to override.
-- Within a sub-table, the *Id field is the only required one; the rest carry
-- numeric defaults (typically 0 or 0.0).
--------------------------------------------------------------------------------

V_TppEquip.SetEquipParameters({
    key = "MyMod:FullExample",   -- string, optional, default: ""  (label for diagnostic logs)

    -- ------------------------------------------------------------------------
    -- Receiver — base weapon body parameters
    -- ------------------------------------------------------------------------
    receiver = {
        receiverId = rcId,              -- int, REQUIRED
        attackId   = 0,                 -- int, optional, default: 0

        -- The four ParamSets arrays are optional; pass them to override
        -- the matching fixed-length slot in the receiver param row.
        receiverParamSetsBase = {       -- optional, 8 floats — base ballistic/handling stats
            -- f1, f2, f3, f4, f5, f6, f7, f8
        },
        receiverParamSetsWobbling = {   -- optional, 7 floats — wobble/sway stats
            -- f1, f2, f3, f4, f5, f6, f7
        },
        receiverParamSetsSystem = {     -- optional, 12 ints — system flags
            -- i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12
        },
        receiverParamSetsSound = {      -- optional, 1 string — sound bank name
            -- "snd_bank_name"
        },
    },

    -- ------------------------------------------------------------------------
    -- Barrel — barrel length + mount-availability flags
    -- ------------------------------------------------------------------------
    barrel = {
        barrelId = baId,                -- int, REQUIRED
        barrelParamSetsBase = {         -- optional, 7 floats
            -- f1..f7
        },
        barrelLength  = TppEquip.BARREL_LENGTH_LONG,  -- int, optional, default: 0
        hasScopeMount = 1,                            -- int, optional, default: 0  (0/1)
        unk2          = 0,                            -- int, optional, default: 0
        hasSideMount  = 0,                            -- int, optional, default: 0  (0/1)
        hasUnderMount = 1,                            -- int, optional, default: 0  (0/1; allows underbarrel)
    },

    -- ------------------------------------------------------------------------
    -- Magazine — capacity + ammo binding
    -- ------------------------------------------------------------------------
    magazine = {
        ammoId             = ammoId,    -- int, REQUIRED
        eqpAmmoId          = 0,         -- int, optional, default: 0
        magCapacity        = 30,        -- int, optional, default: 0  (rounds per magazine; clamped to 0..0x3FF)
        totalCarryCapacity = 180,       -- int, optional, default: 0  (total ammo carried; clamped to 0..0x3FFF)
        bulletId           = 0,         -- int, optional, default: 0  (vanilla BL_* or custom)
    },

    -- ------------------------------------------------------------------------
    -- MuzzleOption — suppressor / compensator attachment
    -- ------------------------------------------------------------------------
    muzzleOption = {
        muzzleOptionId = 0,             -- int, REQUIRED  (vanilla MO_* or custom)
        grouping       = 0.0,           -- float, optional, default: 0.0  (accuracy bonus)
        durability     = 0,             -- int,   optional, default: 0    (wear before failure)
        suppressor     = 0,             -- int,   optional, default: 0    (0/1 is-suppressor)
    },

    -- ------------------------------------------------------------------------
    -- Option — laser / light attachment
    -- ------------------------------------------------------------------------
    option = {
        optionId = 0,                   -- int, REQUIRED
        isLaser  = 0,                   -- int, optional, default: 0  (0/1)
        isLight  = 0,                   -- int, optional, default: 0  (0/1)
    },

    -- ------------------------------------------------------------------------
    -- Sight — scope/optic configuration
    -- ------------------------------------------------------------------------
    sight = {
        sightId     = skId,             -- int, REQUIRED
        zoom1       = 1.5,              -- number, optional, default: 0  (zoom level 1)
        zoom2       = 0,                -- number, optional, default: 0
        zoom3       = 0,                -- number, optional, default: 0
        scopeUiId   = 0,                -- int,    optional, default: 0  (TppEquip.SCOPE_UI_*)
        booster     = 0,                -- int,    optional, default: 0  (0/1 magnification booster)
        nvg         = 0,                -- int,    optional, default: 0  (0/1 night-vision)
        builtIn     = 0,                -- int,    optional, default: 0  (0/1 not detachable)
        rangeFinder = 0,                -- int,    optional, default: 0  (0/1 has rangefinder)
        bdc         = 0,                -- int,    optional, default: 0  (0/1 bullet-drop reticle)
    },

    -- ------------------------------------------------------------------------
    -- Stock — buttstock parameters
    -- ------------------------------------------------------------------------
    stock = {
        stockId = stId,                 -- int, REQUIRED
        field2  = 0.0,                  -- float, optional, default: 0.0
        field3  = 0.0,                  -- float, optional, default: 0.0
    },

    -- ------------------------------------------------------------------------
    -- UnderBarrel — underbarrel attachment (M203, masterkey, etc)
    -- ------------------------------------------------------------------------
    underBarrel = {
        underBarrelId = ubId,           -- int, REQUIRED
        field2 = 0,                     -- int, optional, default: 0
        field3 = 0,                     -- int, optional, default: 0
        field4 = 0,                     -- int, optional, default: 0
    },

    -- ------------------------------------------------------------------------
    -- Bullet — ballistic stats (damage, penetration, muzzle velocity, etc)
    -- ------------------------------------------------------------------------
    bullet = {
        bulletId = 0,                   -- int, REQUIRED  (vanilla BL_* or custom)
        bulletParamSetsBase = {         -- optional, 12 floats — primary ballistic stats
            -- f1..f12
        },
        bulletTrailEffectList = {       -- optional, 1 string — particle effect name
            -- "fx_bullet_trail_name"
        },
        bullet = {                      -- optional, mixed structure — see SetEquipParameters.cpp:906+ for layout
            -- advanced ballistic / behavioral config
        },
    },
})


--------------------------------------------------------------------------------
-- 6. Mother Base R&D entry
-- The framework auto-injects p00 (developId) in const and p50 (flowIndex)
-- in flow — do not set those yourself. All other fields are OPTIONAL with
-- a numeric default of 0 or string default of "" — but several need to be
-- set for the row to actually display in R&D (equipID, equipDevelopTypeID,
-- langEquipName, equipDevelopGroupID, unk36).
--
-- Every field below can be referenced by either its raw pNN name or its
-- readable alias — they're equivalent.
--------------------------------------------------------------------------------

local devId = V_TppEquip.AddToEquipDevelopTable(
    "MyMod:FullExample",   -- key, string, REQUIRED  (stable persistence key)
    {
        const = {
            -- Identity
            equipID             = equipId,                              -- p01, optional, default: 0
            equipDevelopTypeID  = TppMbDev.EQP_DEV_TYPE_Assault,        -- p02, optional, default: 0
            baseEquipDevelopId  = 0,                                    -- p03, optional, default: 0
            skill               = 0,                                    -- p04, optional, default: 0
            bluePrintId         = 0,                                    -- p05, optional, default: 0

            -- Display strings (LangIds; hashed via StrCode64)
            langEquipName       = "wp_mymod_full_name",                 -- p06, optional, default: ""
            langEquipInfo       = "wp_mymod_full_desc",                 -- p07, optional, default: ""
            iconFtexPath        = "/Assets/tpp/ui/icon/wp_full_icon",   -- p08, optional, default: ""
            equipDevelopGroupID = TppMbDev.EQP_DEV_GROUP_WEAPON_120,    -- p09, optional, default: 0

            -- Power-up info LangIds (12 slots; usually unused unless this
            -- equip has tier-up "powerup" descriptions in the R&D screen)
            langPowerUpInfo0    = 0,                                    -- p10, optional, default: 0
            langPowerUpInfo1    = 0,                                    -- p11, optional, default: 0
            langPowerUpInfo2    = 0,                                    -- p12, optional, default: 0
            langPowerUpInfo3    = 0,                                    -- p13, optional, default: 0
            langPowerUpInfo4    = 0,                                    -- p14, optional, default: 0
            langPowerUpInfo5    = 0,                                    -- p15, optional, default: 0
            langPowerUpInfo6    = 0,                                    -- p16, optional, default: 0
            langPowerUpInfo7    = 0,                                    -- p17, optional, default: 0
            langPowerUpInfo8    = 0,                                    -- p18, optional, default: 0
            langPowerUpInfo9    = 0,                                    -- p19, optional, default: 0
            langPowerUpInfo10   = 0,                                    -- p20, optional, default: 0
            langPowerUpInfo11   = 0,                                    -- p21, optional, default: 0

            -- Misc flags / labels
            langEquipRealName   = "",                                   -- p30, optional, default: ""
            isResultRankLimited = 0,                                    -- p31, optional, default: 0
            isCustomEnable      = 0,                                    -- p32, optional, default: 0
            isColorChangeEnable = 0,                                    -- p33, optional, default: 0
            unk34               = 0,                                    -- p34, optional, default: 0
            isSecurityStaffEquip = 0,                                   -- p35, optional, default: 0
            unk36               = 1,                                    -- p36, optional, default: 0
                                                                        --       (set to 1 — vanilla rows have this; without it
                                                                        --        the row tends not to render in the dev menu)
        },
        flow = {
            -- Tier / cost
            sideGrade               = 0,                                -- p51, optional, default: 0
            grade                   = 8,                                -- p52, optional, default: 0
            developGmpCost          = 50000,                            -- p53, optional, default: 0
            usageGmpCost            = 0,                                -- p54, optional, default: 0

            -- Section requirements
            sectionLvForDevelop     = 0,                                -- p55, optional, default: 0
            sectionID2ForDevelop    = 0,                                -- p56, optional, default: 0
            sectionLv2ForDevelop    = 0,                                -- p57, optional, default: 0
            sectionIDForDevelop     = 0,                                -- p63, optional, default: 0
            developSectionLv        = 0,                                -- p64, optional, default: 0

            -- Resource cost (development)
            resourceType1           = "CommonMetal",                    -- p58, optional, default: 0  (string OR numeric ResourceType)
            resourceType1Count      = 260,                              -- p59, optional, default: 0
            resourceType2           = "PreciousMetal",                  -- p60, optional, default: 0
            resourceType2Count      = 50,                               -- p61, optional, default: 0

            -- Resource cost (per-use)
            resourceUsageType1      = 0,                                -- p65, optional, default: 0
            resourceUsageType1Count = 0,                                -- p66, optional, default: 0
            resourceUsageType2      = 0,                                -- p67, optional, default: 0
            resourceUsageType2Count = 0,                                -- p68, optional, default: 0

            -- Availability / display
            initialAvailable        = 0,                                -- p62, optional, default: 0  (0/1; 1 = pre-researched)
            displayInfo             = 0,                                -- p69, optional, default: 0
            unk70                   = 0,                                -- p70, optional, default: 0
            developTimeMinute       = 30,                               -- p71, optional, default: 0
            isValidMbCoin           = 0,                                -- p72, optional, default: 0  (0/1; can use MB Coins)
            intimacyPoint           = 0,                                -- p73, optional, default: 0  (buddy intimacy req)
            isFobAvailable          = 0,                                -- p74, optional, default: 0  (0/1)
        },
    }
)


--------------------------------------------------------------------------------
-- 7. Custom weapon assembly motion data
-- Each entry is a positional 2-tuple: { equipId, mtarPath }.
-- equipId    : the WP_* / EQP_WP_* you want to bind a motion archive to
-- mtarPath   : "/Assets/.../foo.mtar" path string (hashed via fox::Path)
--
-- Calls accumulate in a C++ queue; the framework hooks the engine's
-- ReloadEquipMotionData entry point and splices the queued pairs into
-- the arg.MotionDataTable array before forwarding to the original. So
-- vanilla entries + queued additions both end up in the engine buffer
-- on every reload — no manual reload trigger needed.
--
-- Same equipId queued twice → the latest mtarPath wins (dedup by id).
--
-- For per-bone motions / poses / receiver assignment rows, see
-- TppEquip.ReloadEquipMotionData2(t) — that's the game's native v2
-- entry point with a different (richer) schema. The framework doesn't
-- wrap that; mods using it call it directly. Chimera's
-- EquipMotionDataForChimera.lua is the canonical full-replacement
-- example for v2.
--------------------------------------------------------------------------------

V_TppEquip.AddToEquipMotionDataTable({
    -- Each row: { equipId, mtarPath }     -- both required
    { equipId, "/Assets/tpp/motion/mtar/equip/chimera/assemble/wp_mymod_full_asm.mtar" },
    -- Multiple weapons can share the same .mtar — vanilla does this
    -- extensively (e.g. all West sm_* weapons share sm01_asm.mtar):
    -- { otherEquipId, "/Assets/tpp/motion/mtar/equip/chimera/assemble/sm01_asm.mtar" },
})


--------------------------------------------------------------------------------
-- 8. Support-weapon type bindings
-- These three only matter if you declared a SWP via DeclareSWPs and want
-- the engine to treat it as a specific support-weapon category.
--------------------------------------------------------------------------------

V_TppEquip.SetSupportWeaponType(
    swpId,                          -- supportWeaponId, int, REQUIRED
    TppEquip.SWP_TYPE_Grenade       -- swpType,         int, REQUIRED  (TppEquip.SWP_TYPE_*)
)

-- Cleanup variants (rarely needed — the framework also exposes these via
-- V_FrameWork.* directly; the V_TppEquip wrapper currently only exposes
-- SetSupportWeaponType):
-- V_FrameWork.RemoveSupportWeaponType(swpId)   -- supportWeaponId, int, REQUIRED
-- V_FrameWork.ClearSupportWeaponTypes()         -- no args


--------------------------------------------------------------------------------
-- 9. Custom inventory icon
--------------------------------------------------------------------------------

V_TppEquip.SetEquipIdIconFtexPath(
    equipId,                                          -- equipId, int, REQUIRED
    "/Assets/tpp/ui/icon/wp_mymod_full_icon.ftex"     -- path,    string, REQUIRED
)

-- Cleanup variants (run-time icon swap support):
-- V_TppEquip.ClearIconFtexPath(equipId)   -- equipId, int, REQUIRED
-- V_TppEquip.ClearAllIconFtexPaths()       -- no args


--==============================================================================
-- That's every weapon-related V_TppEquip / V_FrameWork function with every
-- parameter. Strip everything you don't need — every optional field really
-- is optional. WeaponSimple.lua shows the minimum-viable version for
-- comparison.
--==============================================================================
