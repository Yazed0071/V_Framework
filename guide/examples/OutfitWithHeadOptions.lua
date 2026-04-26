--==============================================================================
-- V_FrameWork outfit example — HEAD OPTIONS
--==============================================================================
-- An outfit that exposes the HEAD OPTION submenu in mission prep with a
-- list of selectable headgear equipIds. Vanilla head-option equipIds in
-- the 0x17CA..0x17CE range are the BALACLAVA / SP HEADGEAR family — those
-- always work. Custom head equipIds require their own registration via
-- the equip system (out of scope for this example).
--
-- PHASE-3 LIMITATION:
--   The framework currently ships the HEAD OPTION GATE (the submenu opens
--   for outfits that declare supportsHeadOptions=true). The submenu CONTENT
--   uses vanilla head-option entries. Custom-outfit-specific head-option
--   list rendering is deferred — see Phase-5 follow-up notes.
--==============================================================================

function this.OnAllocate()
    -- developId and flowIndex auto-allocated under the `name`.
    V_TppPlayer.AddOutfit{
        name       = "MyMod:NeonSuitHeads",
        playerType = "DDFemale",

        partsPath = "/Assets/tpp/parts/chara/neon/neon_v01.parts",
        fpkPath   = "/Assets/tpp/pack/player/parts/neon_v01.fpk",

        -- Enable the HEAD OPTION submenu and declare which head equipIds
        -- the player can choose from. Up to 8 entries.
        supportsHeadOptions = true,
        headOptions = {
            0x17CA,        -- BALACLAVA
            0x17CB,        -- HEADGEAR_A (eyepatch)
            0x17CC,        -- HEADGEAR_B (helmet)
            0x17CD,        -- HEADGEAR_C
            -- 0 = NONE sentinel; the framework recognizes it but vanilla UI may
            -- render it differently. Use omission instead of an explicit 0
            -- if you don't want NONE in the list.
        },

        -- Outfit uses vanilla head/face since head options will swap them at runtime.
        faceFpk = true,
        armFpk  = true,
    }
end
