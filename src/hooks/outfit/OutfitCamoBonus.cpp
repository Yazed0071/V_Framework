#include "pch.h"

#include "OutfitCamoBonus.h"
#include "OutfitRegistry.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"


namespace
{
    using ExecSuitCorrect_t = void (__fastcall*)(void* self, void* info);
    static ExecSuitCorrect_t g_OrigExecSuitCorrect = nullptr;
    static bool              g_Installed           = false;


    constexpr std::size_t kInfoCamoBufferPtrOffset    = 0x50;
    constexpr std::size_t kControllerSlotIndexOffset  = 0x78;


    static std::atomic<bool> g_FirstOverrideLogged{ false };


    static std::uint8_t* TryApplyPin_SEH(void* self, void* info,
                                         std::uint8_t* outSaved)
    {
        if (!self || !info || !outSaved) return nullptr;


        const std::uint8_t livePT = outfit::ReadLivePartsType();
        if (livePT < outfit::kCustomPartsTypeStart
            || livePT > outfit::kCustomPartsTypeEnd)
            return nullptr;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(livePT, &entry) || !entry)
            return nullptr;


        const std::uint8_t pin = entry->camoBonusType;
        const bool isVanillaPin =
            (pin <= outfit::kVanillaCamoTypeMax);
        const bool isVirtualPin =
            (pin >= outfit::kCamoVirtualIdStart
             && pin <= outfit::kCamoVirtualIdEnd);
        if (!isVanillaPin && !isVirtualPin)
            return nullptr;


        std::uint8_t* writtenSlot = nullptr;
        __try
        {
            auto* selfBytes = reinterpret_cast<std::uint8_t*>(self);
            auto* infoBytes = reinterpret_cast<std::uint8_t*>(info);

            const std::uint32_t slotIdx =
                *reinterpret_cast<std::uint32_t*>(
                    selfBytes + kControllerSlotIndexOffset);

            auto* byteBuf =
                *reinterpret_cast<std::uint8_t**>(
                    infoBytes + kInfoCamoBufferPtrOffset);

            if (byteBuf)
            {
                writtenSlot = &byteBuf[slotIdx];
                *outSaved = *writtenSlot;
                *writtenSlot = pin;

                if (!g_FirstOverrideLogged.exchange(true))
                {
                    Log("[OutfitCamoBonus] FIRST OVERRIDE: livePT=0x%02X "
                        "slot=%u byteBuf=%p saved=%u pinned=%u "
                        "(developId=%u flowIndex=%u)\n",
                        static_cast<unsigned>(livePT),
                        static_cast<unsigned>(slotIdx),
                        byteBuf,
                        static_cast<unsigned>(*outSaved),
                        static_cast<unsigned>(pin),
                        static_cast<unsigned>(entry->developId),
                        static_cast<unsigned>(entry->flowIndex));
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_FirstOverrideLogged.exchange(true))
            {
                Log("[OutfitCamoBonus] SEH fault during peek — hook "
                    "no-op'd; check Info+0x%zx / Controller+0x%zx "
                    "offsets for build drift\n",
                    kInfoCamoBufferPtrOffset,
                    kControllerSlotIndexOffset);
            }
            writtenSlot = nullptr;
        }
        return writtenSlot;
    }


    static void RestorePin_SEH(std::uint8_t* writtenSlot, std::uint8_t saved)
    {
        if (!writtenSlot) return;
        __try { *writtenSlot = saved; }
        __except (EXCEPTION_EXECUTE_HANDLER) {  }
    }

    static void __fastcall hkExecSuitCorrect(void* self, void* info)
    {


        std::uint8_t saved = 0;
        std::uint8_t* writtenSlot = TryApplyPin_SEH(self, info, &saved);

        if (g_OrigExecSuitCorrect)
            g_OrigExecSuitCorrect(self, info);

        RestorePin_SEH(writtenSlot, saved);
    }
}

namespace outfit
{
    bool Install_OutfitCamoBonus_Hook()
    {
        if (g_Installed) return true;

        void* target = ResolveGameAddress(gAddr.CamouflageController_ExecSuitCorrect);
        if (!target)
        {
            Log("[OutfitCamoBonus] target unresolved; module disabled "
                "(camoBonusType pinning will be inactive)\n");
            return false;
        }

        g_Installed = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkExecSuitCorrect),
            reinterpret_cast<void**>(&g_OrigExecSuitCorrect));

        Log("[OutfitCamoBonus] installed: %s (target=%p)\n",
            g_Installed ? "OK" : "FAIL", target);
        return g_Installed;
    }

    void Uninstall_OutfitCamoBonus_Hook()
    {
        if (!g_Installed) return;
        if (void* t = ResolveGameAddress(gAddr.CamouflageController_ExecSuitCorrect))
            DisableAndRemoveHook(t);
        g_OrigExecSuitCorrect = nullptr;
        g_Installed           = false;
        Log("[OutfitCamoBonus] removed\n");
    }
}
