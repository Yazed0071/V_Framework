#include "pch.h"

#include "EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex.h"

#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetDevelopInfoIndex_t = std::uint64_t(__fastcall*)(void* base20, std::uint8_t buddyType,
                                                             std::uint8_t value, std::uint8_t isSuit);
    using GetPartsFromIndex_t   = std::uint64_t(__fastcall*)(void* self, std::uint16_t flowIndex);
    using VtblIndexById_t       = std::uint64_t(__fastcall*)(void* base20, std::uint16_t developId);
    using VtblIdByIndex_t       = std::uint64_t(__fastcall*)(void* base20, std::uint16_t flowIndex);

    constexpr std::size_t   kVtblIndexByDevelopId = 0xE0;
    constexpr std::size_t   kVtblDevelopIdByIndex = 0x128;
    constexpr std::size_t   kPrimaryToBase20      = 0x20;
    constexpr std::uint16_t kNoIndex              = 0x400;

    GetDevelopInfoIndex_t g_OrigIndex = nullptr;
    GetPartsFromIndex_t   g_OrigValue = nullptr;
    GetPartsFromIndex_t   g_OrigKind  = nullptr;
    void* g_TargetIndex = nullptr;
    void* g_TargetValue = nullptr;
    void* g_TargetKind  = nullptr;

    template <typename Fn>
    Fn VtblFn(void* obj, std::size_t offset)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(obj))[offset / sizeof(void*)]);
    }

    bool EntryAtFlowIndex(void* base20, std::uint16_t flowIndex, buddyoutfit::Entry* e)
    {
        if (flowIndex == kNoIndex || !base20 || !buddyoutfit::HasEntries())
            return false;
        const std::uint16_t developId = static_cast<std::uint16_t>(
            VtblFn<VtblIdByIndex_t>(base20, kVtblDevelopIdByIndex)(base20, flowIndex));
        return developId != 0 && buddyoutfit::TryGetByDevelopId(developId, e);
    }

    std::uint64_t __fastcall hkGetBuddyPartsDevelopInfoIndex(void* base20, std::uint8_t buddyType,
                                                             std::uint8_t value, std::uint8_t isSuit)
    {
        buddyoutfit::Kind kind = buddyoutfit::Kind::QuietCostume;
        buddyoutfit::Entry e;
        if (base20 && buddyoutfit::KindFromBuddyType(buddyType, isSuit != 0, &kind)
            && buddyoutfit::TryGetByValue(kind, value, &e))
            return VtblFn<VtblIndexById_t>(base20, kVtblIndexByDevelopId)(base20, e.developId);
        return g_OrigIndex(base20, buddyType, value, isSuit);
    }

    std::uint64_t __fastcall hkGetBuddyPartsId(void* base20, std::uint16_t flowIndex)
    {
        buddyoutfit::Entry e;
        if (EntryAtFlowIndex(base20, flowIndex, &e))
            return e.value;
        return g_OrigValue(base20, flowIndex);
    }

    std::uint64_t __fastcall hkGetBuddyPartsKind(void* primary, std::uint16_t flowIndex)
    {
        buddyoutfit::Entry e;
        if (primary && EntryAtFlowIndex(static_cast<std::uint8_t*>(primary) + kPrimaryToBase20, flowIndex, &e))
            return buddyoutfit::BuddyTypeOf(e.kind);
        return g_OrigKind(primary, flowIndex);
    }

    bool HookOne(std::uintptr_t addr, void* detour, void** orig, void*& target, const char* what)
    {
        void* t = ResolveGameAddress(addr);
        if (!t)
        {
            LogError("BuddyOutfit: %s address missing on this build, custom buddy outfits off", what);
            return false;
        }
        if (!CreateAndEnableHook(t, detour, orig))
        {
            LogError("BuddyOutfit: %s hook failed, custom buddy outfits off", what);
            return false;
        }
        target = t;
        return true;
    }
}

bool Install_EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex_Hooks()
{
    bool ok = true;
    if (!g_TargetIndex)
        ok &= HookOne(gAddr.EquipDevCtrl_GetBuddyPartsDevelopInfoIndex,
                      reinterpret_cast<void*>(&hkGetBuddyPartsDevelopInfoIndex),
                      reinterpret_cast<void**>(&g_OrigIndex), g_TargetIndex, "buddy equip row lookup");
    if (!g_TargetValue)
        ok &= HookOne(gAddr.EquipDevCtrl_GetBuddyPartsId,
                      reinterpret_cast<void*>(&hkGetBuddyPartsId),
                      reinterpret_cast<void**>(&g_OrigValue), g_TargetValue, "buddy equip value lookup");
    if (!g_TargetKind)
        ok &= HookOne(gAddr.EquipDevCtrl_GetBuddyPartsKind,
                      reinterpret_cast<void*>(&hkGetBuddyPartsKind),
                      reinterpret_cast<void**>(&g_OrigKind), g_TargetKind, "buddy equip type lookup");
    return ok;
}

bool Uninstall_EquipDevelopControllerImpl_GetBuddyPartsDevelopInfoIndex_Hooks()
{
    for (void** t : { &g_TargetKind, &g_TargetValue, &g_TargetIndex })
    {
        if (*t)
        {
            DisableAndRemoveHook(*t);
            *t = nullptr;
        }
    }
    g_OrigIndex = nullptr;
    g_OrigValue = nullptr;
    g_OrigKind = nullptr;
    return true;
}
