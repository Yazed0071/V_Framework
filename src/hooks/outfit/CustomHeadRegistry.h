#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{
    // ----------------------------------------------------------------------
    // CustomHeadRegistry — Tier-3 custom head support.
    //
    // Modders register a head via lua:
    //   V_FrameWork.RegisterCustomHead{
    //       name     = "MyMod:Helmet",
    //       fpkPath  = "/Assets/.../helmet.fpk",
    //   }
    //
    // The framework looks up the `name` in the shared V_FrameWorkState key
    // registry first. If the same `name` was used in a prior call to
    // V_TppEquip.AddToEquipDevelopTable, the previously-allocated developId
    // is reused as the head's `equipId`. Otherwise the framework allocates
    // a new developId via the same pool. Either way, the developId space
    // is shared so a mod can use ONE key for both APIs:
    //
    //   V_TppEquip.AddToEquipDevelopTable("MyMod:Helmet", { const = {...,
    //       langEquipName = "name_my_helmet",
    //       iconFtexPath  = "/Assets/.../ui_helmet" }, flow = {...} })
    //   V_FrameWork.RegisterCustomHead{
    //       name = "MyMod:Helmet",
    //       fpkPath = "/Assets/.../helmet.fpk",
    //   }
    //
    // Both end up referring to the same equipId. The const-setting row's
    // langEquipName / iconFtexPath populate the iDroid label/icon; this
    // module owns the FPK load.
    //
    // The slot byte (info[3] value) is allocated independently from a
    // sequential 0x06..0xFF pool — vanilla orig only special-cases slots
    // 1..5; we use 6+ for our registered heads.
    // ----------------------------------------------------------------------

    constexpr std::uint8_t  kCustomHeadSlotBase = 0x06;
    constexpr std::size_t   kMaxCustomHeads     = 250;
    static_assert(kCustomHeadSlotBase + kMaxCustomHeads <= 0x100,
                  "slot byte allocations must fit in a uint8");

    // Default visual face id — DDFemale BALACLAVA (dds_balaclava5).
    // Verified 2026-04-30 as a known-good vanilla TppEnemyFaceId that
    // loads correctly through the head-overlay pipeline (asset loader's
    // bookkeeping has an entry for it, completion signal fires).
    constexpr std::uint16_t kDefaultTppEnemyFaceId = 0x22D;

    struct CustomHeadEntry
    {
        bool           used         = false;
        std::uint16_t  equipId      = 0;          // EquipDevelopConstSetting row index
                                                  // (NOT the developId — the orig's
                                                  // FUN_14951AE20 lookup uses row
                                                  // index, computed via EDC vtable[0x70])
        std::uint16_t  developId    = 0;          // V_FrameWorkState pool — links
                                                  // back to AddToEquipDevelopTable's
                                                  // row by p00 field
        std::uint16_t  flowIndex    = 0;          // V_FrameWorkState pool — links
                                                  // back to AddToEquipDevelopTable's
                                                  // row by p50 field. Used by the
                                                  // develop-gate (orig's
                                                  // IsEquipDeveloped is keyed on
                                                  // flowIndex) so the head only
                                                  // appears in the iDroid HEAD OPTION
                                                  // submenu after the player has
                                                  // researched it in MotherBase R&D.
        std::uint8_t   slotByte     = 0;          // info[3] value
        std::uint16_t  TppEnemyFaceId = kDefaultTppEnemyFaceId;
                                                  // Vanilla TppEnemyFaceId that the
                                                  // ConverFaceIdWithFaceEquipId hook
                                                  // returns for our slot byte. Picks
                                                  // which vanilla balaclava render
                                                  // style this head visually appears
                                                  // as. Modder-controlled via the
                                                  // `TppEnemyFaceId` lua field
                                                  // (matches the vanilla MGSV lua
                                                  // table `TppEnemyFaceId`).
                                                  //
                                                  // WHY NOT A SENTINEL: tested 2026-04-30
                                                  // — sentinel face id outside the
                                                  // FaceUnit table (>= 900) hangs the
                                                  // asset loader because completion
                                                  // bookkeeping can't find an entry to
                                                  // signal "load done" against. Routing
                                                  // to a real face id keeps the chain
                                                  // valid end-to-end.
        std::uint64_t  langNameHash = 0;          // optional iDroid label hash
        std::uint64_t  iconFtexCode = 0;          // optional iDroid icon path
        char           name[64]     = { 0 };      // mod-side identifier
    };

    // Registers a head option. Returns the assigned equipId on success,
    // 0 on failure.
    //
    // Tier-3-A architecture (current): the head's visual asset is a
    // vanilla balaclava chosen by `TppEnemyFaceId`. Distinct custom-mesh
    // heads aren't yet supported (would require FaceUnit-table
    // registration via Soldier2FaceSystemImpl, future work). The
    // framework still owns:
    //   - name → equipId mapping (so headOptions can reference it)
    //   - slot byte allocation
    //   - linking to V_TppEquip.AddToEquipDevelopTable for iDroid label
    //     and icon (via the shared developId key)
    //
    // Idempotent on `name`: re-registering the same name returns the
    // same equipId (no re-allocation). Lang / icon / TppEnemyFaceId
    // ARE refreshed on re-registration.
    //
    // Suggested TppEnemyFaceId values (DDFemale-compatible):
    //   0x228 (dds_balaclava0)  0x22B (dds_balaclava3)
    //   0x229 (dds_balaclava1)  0x22C (dds_balaclava4)
    //   0x22A (dds_balaclava2)  0x22D (dds_balaclava5)  ← default
    std::uint16_t RegisterHeadOption(
        const char* name,
        std::uint16_t TppEnemyFaceId,
        std::uint64_t langNameHash = 0,
        std::uint64_t iconFtexCode = 0);

    // Look up by lua-side name. Returns nullptr if not registered.
    const CustomHeadEntry* TryGetCustomHeadByName(const char* name);

    // Look up by equipId (the value in outfit's headOptions array, also
    // the face-fv2 sentinel for the FPK path resolver hook).
    const CustomHeadEntry* TryGetCustomHeadByEquipId(std::uint16_t equipId);

    // Look up by slot byte (the value written to info[3]).
    const CustomHeadEntry* TryGetCustomHeadBySlot(std::uint8_t slotByte);

    // Predicate helpers for hook sites — registry-driven (no fast range
    // check since equipIds are allocated from the shared developId pool
    // which can land anywhere in 16-bit space).
    inline bool IsCustomHeadEquipId(std::uint16_t equipId)
    {
        return TryGetCustomHeadByEquipId(equipId) != nullptr;
    }
    inline bool IsCustomHeadSlot(std::uint8_t slotByte)
    {
        return TryGetCustomHeadBySlot(slotByte) != nullptr;
    }
    inline bool IsCustomHeadFaceFv2(std::uint16_t faceFv2)
    {
        return IsCustomHeadEquipId(faceFv2);  // identity mapping
    }
}
