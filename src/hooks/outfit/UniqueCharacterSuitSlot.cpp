#include "pch.h"

#include "UniqueCharacterSuitSlot.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "OutfitRegistry.h"
#include "UniqueCharacterPartsTypePin.h"

#include "log.h"

namespace
{
    constexpr std::size_t kPatchLen  = 2;
    constexpr std::size_t kSiteCount = 2;

    struct Site
    {
        std::ptrdiff_t offset;
        std::uint8_t   original[kPatchLen];
        std::uint8_t   patched[kPatchLen];
    };

    const Site kSites[kSiteCount] = {
        { 0x00, { 0x74, 0x5C }, { 0x90, 0x90 } },
        { 0x12, { 0x74, 0x4A }, { 0x90, 0x90 } },
    };

    static bool g_Applied = false;
    static bool g_ApplyFailLogged = false;

    using SlotNum_t = std::uint32_t (__fastcall*)(void*);
    static SlotNum_t g_OrigSlotNum   = nullptr;
    static bool      g_HookInstalled = false;

    constexpr std::size_t kMenuModeOff  = 0x3CA0;
    constexpr std::uint32_t kCountWithHeadOption = 3;
    constexpr std::uint32_t kCountWithoutHead    = 2;

    static bool TryReadMenuMode(void* self, std::int32_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<std::int32_t*>(
                static_cast<std::uint8_t*>(self) + kMenuModeOff);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static std::uint32_t __fastcall hkCharaSlotSelectionNum(void* self)
    {
        const std::uint32_t ret = g_OrigSlotNum ? g_OrigSlotNum(self) : 0;
        if (ret != kCountWithHeadOption || !self) return ret;
        if (MissionCodeGuard::ShouldBypassHooks()) return ret;

        const std::uint8_t livePT = outfit::ReadLivePlayerType();
        if (!outfit::IsUniqueCharacterPlayerType(livePT)) return ret;

        std::int32_t mode = -1;
        if (!TryReadMenuMode(self, &mode)) return ret;
        if (mode != 0 && mode != 2) return ret;

        const std::uint8_t parts = outfit::ReadLivePartsType();
        bool offered = false;
        if (!uniquecharpin::IsOwnSuitPartsType(livePT, parts))
        {
            if (parts >= outfit::kCustomPartsTypeStart
             && parts <= outfit::kCustomPartsTypeEnd)
            {
                const outfit::OutfitEntry* entry = nullptr;
                if (!outfit::TryGetOutfitByPartsType(parts, &entry) || !entry)
                    return ret;
                offered = entry->HasHeadOptionsForVariant(
                    livePT, outfit::GetActiveVariant(parts));
            }
            else
            {
                offered = outfit::VanillaExtHasAnyHeadOptions(
                    parts, livePT, outfit::ReadLiveSelectorCode());
            }
        }

        if (offered) return ret;

        return kCountWithoutHead;
    }

    static bool InstallSlotNumHook()
    {
        if (g_HookInstalled) return true;

        void* target = ResolveGameAddress(gAddr.CharaSlotSelectionNum);
        if (!target)
        {
            LogError("SuitSlot: CharaSlotSelectionNum address missing for this build, "
                "Ocelot/Quiet slot list stays vanilla");
            return false;
        }

        g_HookInstalled = CreateAndEnableHook(target,
            reinterpret_cast<void*>(&hkCharaSlotSelectionNum),
            reinterpret_cast<void**>(&g_OrigSlotNum));
        if (!g_HookInstalled)
            LogError("SuitSlot: CharaSlotSelectionNum hook failed, Ocelot/Quiet slot "
                "list stays vanilla");
        return g_HookInstalled;
    }


    static bool WriteBytes(void* target, const std::uint8_t* src, std::size_t len)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            if (!g_ApplyFailLogged)
            {
                g_ApplyFailLogged = true;
                LogWarn("SuitSlot: VirtualProtect failed at %p (err %lu), slot list "
                    "unchanged", target, GetLastError());
            }
            return false;
        }

        std::memcpy(target, src, len);

        DWORD restored = 0;
        VirtualProtect(target, len, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, len);
        return true;
    }

    static bool Apply(bool on)
    {
        if (g_Applied == on) return true;

        const std::uintptr_t rva = gAddr.CharaSlotSelectionNumGate;
        if (!rva)
        {
            if (!g_ApplyFailLogged)
            {
                g_ApplyFailLogged = true;
                LogError("SuitSlot: CharaSlotSelectionNumGate address missing for this "
                    "build, Ocelot/Quiet keep character-only slot list");
            }
            return false;
        }

        auto* base = static_cast<std::uint8_t*>(ResolveGameAddress(rva));
        if (!base)
        {
            if (!g_ApplyFailLogged)
            {
                g_ApplyFailLogged = true;
                LogError("SuitSlot: CharaSlotSelectionNumGate 0x%llX unresolved, "
                    "Ocelot/Quiet keep character-only slot list",
                    static_cast<unsigned long long>(rva));
            }
            return false;
        }

        for (std::size_t i = 0; i < kSiteCount; ++i)
        {
            std::uint8_t*       site   = base + kSites[i].offset;
            const std::uint8_t* expect = on ? kSites[i].original : kSites[i].patched;
            if (std::memcmp(site, expect, kPatchLen) != 0)
            {
                if (!g_ApplyFailLogged)
                {
                    g_ApplyFailLogged = true;
                    LogError("SuitSlot: unexpected bytes %02X %02X at %p, slot list "
                        "unchanged", static_cast<unsigned>(site[0]),
                        static_cast<unsigned>(site[1]), static_cast<void*>(site));
                }
                return false;
            }
        }

        for (std::size_t i = 0; i < kSiteCount; ++i)
        {
            std::uint8_t* site = base + kSites[i].offset;
            if (!WriteBytes(site, on ? kSites[i].patched : kSites[i].original,
                    kPatchLen))
            {
                for (std::size_t back = 0; back < i; ++back)
                    WriteBytes(base + kSites[back].offset,
                        on ? kSites[back].original : kSites[back].patched, kPatchLen);
                return false;
            }
        }

        g_Applied = on;
        return true;
    }
}

void UniqueCharacterSuitSlot_EnforceMissionGuard()
{
    if (!g_HookInstalled) return;
    const bool want = !MissionCodeGuard::ShouldBypassHooks();
    if (g_Applied == want) return;
    Apply(want);
}

bool Install_UniqueCharacterSuitSlot()
{
    if (!InstallSlotNumHook())
        return true;
    if (MissionCodeGuard::ShouldBypassHooks()) return true;
    Apply(true);
    return true;
}

void Uninstall_UniqueCharacterSuitSlot()
{
    if (!Apply(false))
    {
        LogWarn("SuitSlot: widening NOPs not reverted, chara-slot-count hook kept");
        return;
    }
    if (g_HookInstalled)
    {
        DisableAndRemoveHook(ResolveGameAddress(gAddr.CharaSlotSelectionNum));
        g_OrigSlotNum   = nullptr;
        g_HookInstalled = false;
    }
}
