--------------------------------------------------------------------------------
-- V_TppPlayer — Player outfit, voice, and camo wrapper module
--
-- Functions:
--   AddOutfit(outfitData)                 Register a custom outfit (one call)
--   SetPlayerVoiceFpkPathForType(t, p)    Override voice FPK per player type
--   ClearPlayerVoiceFpkPathForType(t)     Clear voice override
--   ClearAllPlayerVoiceFpkOverrides()     Clear all voice overrides
--   SetCamoValue(camo, material, value)   Edit one camo table cell
--   CloneCamoRow(dst, src)                Clone a camo row
--------------------------------------------------------------------------------
local this = {}

--------------------------------------------------------------------------------
-- AddOutfit(outfitData)
--
-- Registers a custom player outfit with develop entry, linking, FV2 visuals,
-- body variants, and camo gameplay values — all in one call.
--
-- REQUIRED FIELDS:
--   playerType  (number)   0=Snake, 1=DD_Male, 2=DD_Female, 3=Avatar
--   partsPath   (string)   .parts asset path
--   fpkPath     (string)   .fpk asset path
--   develop     (table)    { const = {...}, flow = {...} }
--
-- OPTIONAL FIELDS:
--   enableHead    (bool, default true)    Show DD/Snake hair/face
--   enableHand    (bool, default true)    Show bionic arm
--   fv2Path       (string)               Custom FV2 visual variation file
--   fv2FpkPath    (string)               FPK containing the FV2 file
--   camoCloneFrom (string or number)     Clone camo bonuses from this pattern
--   camoValues    (table)                Array of 82 numbers (camo bonus values)
--   variants      (table)                Array of body variant entries
--
-- VARIANT ENTRIES:
--   partsPath   (string, REQUIRED)
--   fpkPath     (string, REQUIRED)
--   langId      (string, optional)       Language ID for UI display
--   enableHead  (bool, optional)         Inherits from base if omitted
--   enableHand  (bool, optional)         Inherits from base if omitted
--   fv2Path     (string, optional)
--   fv2FpkPath  (string, optional)
--
-- RETURNS: partsType (number), developId (number)
--------------------------------------------------------------------------------

-- EXAMPLE 1: Simple outfit
--[[
    V_TppPlayer.AddOutfit({
        playerType = 2,
        partsPath  = "/Assets/tpp/parts/chara/sna/my_outfit.parts",
        fpkPath    = "/Assets/tpp/pack/chara/my_mod/my_outfit.fpk",
        enableHead = true,
        enableHand = false,
        develop = {
            const = {
                equipID = TppEquip.EQP_SUIT,
                equipDevelopTypeID = TppMbDev.EQP_DEV_TYPE_Suit,
                iconFtexPath = "/Assets/tpp/ui/texture/EquipIcon/suit/my_icon",
                equipDevelopGroupID = TppMbDev.EQP_DEV_GROUP_TOOL_360,
                unk36 = 1,
            },
            flow = { grade = 4 }
        }
    })
]]

-- EXAMPLE 2: Outfit with FV2 visual + camo gameplay cloned from Sandstorm
--[[
    V_TppPlayer.AddOutfit({
        playerType = 2,
        partsPath  = "/Assets/.../desert_suit.parts",
        fpkPath    = "/Assets/.../desert_suit.fpk",
        fv2Path    = "/Assets/.../desert_skin.fv2",
        fv2FpkPath = "/Assets/.../desert_fova.fpk",
        camoCloneFrom = "SANDSTORM",
        develop = { const = { ... }, flow = { ... } }
    })
]]

-- EXAMPLE 3: Outfit with body variants (zipped/unzipped/no jacket)
--[[
    V_TppPlayer.AddOutfit({
        playerType = 2,
        partsPath  = "/Assets/.../jacket_zipped.parts",
        fpkPath    = "/Assets/.../jacket_zipped.fpk",
        variants = {
            { langId = "unzipped", partsPath = "/Assets/.../unzipped.parts",
              fpkPath = "/Assets/.../unzipped.fpk" },
            { langId = "no_jacket", partsPath = "/Assets/.../no_jacket.parts",
              fpkPath = "/Assets/.../no_jacket.fpk" },
        },
        develop = { const = { ... }, flow = { ... } }
    })
]]

-- EXAMPLE 4: Outfit with custom camo values (82 material bonuses)
--[[
    V_TppPlayer.AddOutfit({
        playerType = 0,
        partsPath  = "/Assets/.../forest_suit.parts",
        fpkPath    = "/Assets/.../forest_suit.fpk",
        camoValues = {
            0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
            50,0,0,50,50,50,50,50,50,50,0,50,0,0,0,50,50,0,0,0,0,0
        },
        develop = { const = { ... }, flow = { ... } }
    })
]]

--------------------------------------------------------------------------------
-- SetCamoValue(camoType, materialType, value)
--
-- Sets one cell in the camo effectiveness table.
-- Accepts string names ("OLIVEDRAB", "MTR_LEAF") or numeric indices (0, 60).
-- Changes apply to the game engine immediately.
--
-- EXAMPLES:
--   V_TppPlayer.SetCamoValue("OLIVEDRAB", "MTR_LEAF", 50)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_ROCK_A", 50)
--   V_TppPlayer.SetCamoValue(0, 60, 50)  -- same as first line
--
-- EXAMPLE — Edit existing camo, make BLACK effective in forests:
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_LEAF", 50)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_PLNT_A", 50)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_WOOD_A", 50)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_MOSS_A", 50)
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
-- CloneCamoRow(dstCamoType, srcCamoType)
--
-- Copies all 82 material values from one camo pattern to another.
-- Useful for giving a pattern the same effectiveness as another, then tweaking.
--
-- EXAMPLE — Clone WOODLAND onto BLACK, then remove sand bonus:
--   V_TppPlayer.CloneCamoRow("BLACK", "WOODLAND")
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_SAND_A", 0)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_SAND_B", 0)
--   V_TppPlayer.SetCamoValue("BLACK", "MTR_SAND_C", 0)
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
-- Voice overrides
--
-- playerType: 0=Snake, 1=DD_Male, 2=DD_Female, 3=Avatar
--
-- EXAMPLE:
--   V_TppPlayer.SetPlayerVoiceFpkPathForType(0, "/Assets/.../custom_voice.fpk")
--   V_TppPlayer.ClearPlayerVoiceFpkPathForType(0)
--   V_TppPlayer.ClearAllPlayerVoiceFpkOverrides()
--------------------------------------------------------------------------------

return this
