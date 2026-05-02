#include "pch.h"

#include "OutfitRegistry.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "log.h"
#include <HookUtils.h>

namespace
{
    using outfit::kCustomPartsTypeStart;
    using outfit::kCustomPartsTypeEnd;
    using outfit::kCustomSelectorStart;
    using outfit::kCustomSelectorEnd;
    using outfit::kMaxOutfits;
    using outfit::OutfitEntry;
    using outfit::OutfitDefinition;

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static std::array<OutfitEntry, kMaxOutfits> g_Entries{};
    static std::mutex                            g_Mutex;
    static GetQuarkSystemTable_t                 g_GetQuarkSystemTable = nullptr;


    static std::uint8_t g_ActiveVariant[0x40] = {};


    static std::uint16_t g_PendingDevelopId = 0;
    static std::uint16_t g_PendingHeadOptionEquipId = 0;

    static bool ResolveQuarkApi()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    static std::uint8_t* GetQuarkLiveState()
    {
        if (!ResolveQuarkApi()) return nullptr;
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return nullptr;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return nullptr;
        return *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
    }

    static bool IsCustomPartsType(std::uint8_t v)
    {
        return v >= kCustomPartsTypeStart && v <= kCustomPartsTypeEnd;
    }

    static bool IsCustomSelector(std::uint8_t v)
    {
        return v >= kCustomSelectorStart && v <= kCustomSelectorEnd;
    }


    static std::uint8_t AllocatePartsType_NoLock(std::uint8_t hint)
    {
        if (IsCustomPartsType(hint))
        {
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == hint)
                    return 0xFF;  // collision
            }
            return hint;
        }

        for (std::uint16_t v = kCustomPartsTypeStart; v <= kCustomPartsTypeEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            bool taken = false;
            for (const auto& e : g_Entries)
            {
                if (e.used && e.partsType == candidate) { taken = true; break; }
            }
            if (!taken) return candidate;
        }
        return 0xFF;
    }


    static bool IsSelectorTaken_NoLock(std::uint8_t code)
    {
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;
            if (e.selectorCode == code) return true;


            for (std::uint8_t k = 1; k < e.variantCount; ++k)
            {
                if (e.variantSelectorCodes[k] == code) return true;
            }
        }
        return false;
    }

    static std::uint8_t AllocateSelector_NoLock(std::uint8_t hint)
    {
        if (IsCustomSelector(hint))
        {
            if (IsSelectorTaken_NoLock(hint)) return 0xFF;
            return hint;
        }

        for (std::uint16_t v = kCustomSelectorStart; v <= kCustomSelectorEnd; ++v)
        {
            const auto candidate = static_cast<std::uint8_t>(v);
            if (!IsSelectorTaken_NoLock(candidate)) return candidate;
        }
        return 0xFF;
    }
}

namespace outfit
{
    bool RegisterOutfit(const OutfitDefinition& def, std::uint8_t* outAllocatedPartsType)
    {
        if (def.partsPathCode64 == 0 || def.fpkPathCode64 == 0)
        {
            Log("[OutfitRegistry] reject: missing required parts/fpk path "
                "(developId=%u flowIndex=%u)\n",
                static_cast<unsigned>(def.developId),
                static_cast<unsigned>(def.flowIndex));
            return false;
        }

        if (def.developId == 0 || def.flowIndex == 0)
        {
            Log("[OutfitRegistry] reject: developId/flowIndex must be non-zero "
                "(developId=%u flowIndex=%u)\n",
                static_cast<unsigned>(def.developId),
                static_cast<unsigned>(def.flowIndex));
            return false;
        }


        constexpr std::uint16_t kEdcRowCapacity = 0x400;
        if (def.flowIndex >= kEdcRowCapacity)
        {
            Log("[OutfitRegistry] reject: flowIndex=%u is out of EDC row "
                "capacity (max 0x3FF = 1023). Custom outfits must use "
                "flowIndex in 922..1023. Either omit flowIndex (the "
                "framework auto-allocates from 922+) or pass a value "
                "in range. (developId=%u key=%s)\n",
                static_cast<unsigned>(def.flowIndex),
                static_cast<unsigned>(def.developId),
                def.key ? def.key : "(unkeyed)");
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);


        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;

            const bool sameDevelopId = (e.developId == def.developId);
            const bool sameFlowIndex = (e.flowIndex == def.flowIndex);
            if (!sameDevelopId && !sameFlowIndex) continue;

            const bool sameOutfit =
                sameDevelopId
             && sameFlowIndex
             && e.playerType      == def.playerType
             && e.partsPathCode64 == def.partsPathCode64
             && e.fpkPathCode64   == def.fpkPathCode64;

            if (sameOutfit)
            {
                Log("[OutfitRegistry] re-registration of same outfit "
                    "developId=%u flowIndex=%u partsType=0x%02X "
                    "selector=0x%02X — returning existing entry "
                    "(idempotent)\n",
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(e.flowIndex),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.selectorCode));
                if (outAllocatedPartsType) *outAllocatedPartsType = e.partsType;
                return true;
            }


            if (sameDevelopId)
            {
                Log("[OutfitRegistry] reject: developId %u already registered "
                    "by a DIFFERENT outfit (existing partsType=0x%02X "
                    "playerType=%u, attempted playerType=%u)\n",
                    static_cast<unsigned>(def.developId),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.playerType),
                    static_cast<unsigned>(def.playerType));
            }
            else
            {
                Log("[OutfitRegistry] reject: flowIndex %u already registered "
                    "by a DIFFERENT outfit (existing partsType=0x%02X "
                    "developId=%u, attempted developId=%u)\n",
                    static_cast<unsigned>(def.flowIndex),
                    static_cast<unsigned>(e.partsType),
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(def.developId));
            }
            return false;
        }

        const std::uint8_t partsType = AllocatePartsType_NoLock(def.partsTypeHint);
        if (partsType == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom partsType slot\n");
            return false;
        }

        const std::uint8_t selector = AllocateSelector_NoLock(def.selectorCodeHint);
        if (selector == 0xFF)
        {
            Log("[OutfitRegistry] reject: no free custom selector slot\n");
            return false;
        }


        const std::uint8_t variantSlots =
            (def.variantCount > outfit::kMaxVariantsPerOutfit)
                ? static_cast<std::uint8_t>(outfit::kMaxVariantsPerOutfit)
                : def.variantCount;

        std::uint8_t variantSelectors[outfit::kMaxVariantsPerOutfit] = {};
        for (auto& v : variantSelectors) v = 0xFF;
        variantSelectors[0] = selector;
        for (std::uint8_t vi = 1; vi < variantSlots; ++vi)
        {


            std::uint8_t alloc = 0xFF;
            for (std::uint16_t cand = kCustomSelectorStart;
                 cand <= kCustomSelectorEnd; ++cand)
            {
                const auto c = static_cast<std::uint8_t>(cand);
                if (IsSelectorTaken_NoLock(c)) continue;


                bool taken = false;
                for (std::uint8_t k = 0; k < vi; ++k)
                {
                    if (variantSelectors[k] == c) { taken = true; break; }
                }
                if (!taken) { alloc = c; break; }
            }
            if (alloc == 0xFF)
            {
                Log("[OutfitRegistry] reject: ran out of selector codes "
                    "while reserving variant %u of %u (variantCount=%u)\n",
                    static_cast<unsigned>(vi),
                    static_cast<unsigned>(variantSlots),
                    static_cast<unsigned>(def.variantCount));
                return false;
            }
            variantSelectors[vi] = alloc;
        }


        OutfitEntry* slot = nullptr;
        for (auto& e : g_Entries)
        {
            if (!e.used) { slot = &e; break; }
        }
        if (!slot)
        {
            Log("[OutfitRegistry] reject: registry is full (kMaxOutfits=%zu)\n",
                kMaxOutfits);
            return false;
        }

        *slot = OutfitEntry{};
        slot->used            = true;
        slot->developId       = def.developId;
        slot->flowIndex       = def.flowIndex;
        slot->playerType      = def.playerType;
        slot->partsType       = partsType;
        slot->selectorCode    = selector;
        slot->partsPathCode64 = def.partsPathCode64;
        slot->fpkPathCode64   = def.fpkPathCode64;
        slot->camoFpk         = def.camoFpk;
        slot->faceFpk         = def.faceFpk;
        slot->skinFv2         = def.skinFv2;
        slot->diamondFpk      = def.diamondFpk;
        slot->enableArm       = def.enableArm;


        slot->camoFv2             = def.camoFv2;
        slot->diamondFv2          = def.diamondFv2;
        slot->supportsHeadOptions = def.supportsHeadOptions;
        slot->headOptionCount     =
            def.headOptionCount > outfit::kMaxHeadOptionsPerOutfit
                ? static_cast<std::uint8_t>(outfit::kMaxHeadOptionsPerOutfit)
                : def.headOptionCount;
        for (std::size_t i = 0; i < slot->headOptionCount; ++i)
            slot->headOptionEquipIds[i] = def.headOptionEquipIds[i];

        slot->variantCount =
            def.variantCount > outfit::kMaxVariantsPerOutfit
                ? static_cast<std::uint8_t>(outfit::kMaxVariantsPerOutfit)
                : def.variantCount;
        for (std::size_t i = 0; i < slot->variantCount; ++i)
            slot->variants[i] = def.variants[i];


        for (std::size_t i = 0; i < outfit::kMaxVariantsPerOutfit; ++i)
            slot->variantSelectorCodes[i] = variantSelectors[i];


        slot->variantDisplayNameHashes[0] = def.baseDisplayNameHash;
        for (std::uint8_t vi = 1; vi < slot->variantCount; ++vi)
        {
            slot->variantDisplayNameHashes[vi] = def.variants[vi].displayNameHash;
        }

        slot->enableHead           = def.enableHead;
        slot->defaultSoldierFaceId = def.defaultSoldierFaceId;
        slot->langEquipNameHash    = def.langEquipNameHash;
        slot->camoBonusType        = def.camoBonusType;


        if (def.hasCamoBonusValues)
        {


            std::uint8_t virtualId = 0xFF;
            for (std::uint16_t v = kCamoVirtualIdStart;
                 v <= kCamoVirtualIdEnd; ++v)
            {
                bool taken = false;
                for (const auto& e : g_Entries)
                {
                    if (!e.used) continue;
                    if (e.hasCamoBonusValues
                        && e.camoBonusType == static_cast<std::uint8_t>(v))
                    {
                        taken = true;
                        break;
                    }
                }
                if (!taken)
                {
                    virtualId = static_cast<std::uint8_t>(v);
                    break;
                }
            }

            if (virtualId == 0xFF)
            {
                Log("[OutfitRegistry] camo-virtual-id pool exhausted "
                    "(range 0x%02X..0x%02X full); outfit '%s' will run "
                    "without per-outfit camo values\n",
                    static_cast<unsigned>(kCamoVirtualIdStart),
                    static_cast<unsigned>(kCamoVirtualIdEnd),
                    def.key ? def.key : "(unkeyed)");
                slot->hasCamoBonusValues = false;
            }
            else
            {
                slot->camoBonusType = virtualId;
                slot->hasCamoBonusValues = true;
                std::memcpy(slot->camoBonusValues,
                            def.camoBonusValues,
                            sizeof(slot->camoBonusValues));
            }
        }

        if (outAllocatedPartsType) *outAllocatedPartsType = partsType;


        char variantBuf[16 * 5 + 1] = {};
        {
            std::size_t pos = 0;
            for (std::size_t i = 0;
                 i < outfit::kMaxVariantsPerOutfit && pos + 5 < sizeof(variantBuf);
                 ++i)
            {
                pos += static_cast<std::size_t>(std::snprintf(
                    variantBuf + pos, sizeof(variantBuf) - pos,
                    (i == 0) ? "0x%02X" : ",0x%02X",
                    static_cast<unsigned>(slot->variantSelectorCodes[i])));
            }
        }


        char dispNameBuf[16 * 12 + 1] = {};
        {
            std::size_t pos = 0;
            for (std::size_t i = 0;
                 i < outfit::kMaxVariantsPerOutfit && pos + 12 < sizeof(dispNameBuf);
                 ++i)
            {
                const std::uint64_t h = slot->variantDisplayNameHashes[i];
                if (h == 0)
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        dispNameBuf + pos, sizeof(dispNameBuf) - pos,
                        (i == 0) ? "(none)" : ",(none)"));
                }
                else
                {
                    pos += static_cast<std::size_t>(std::snprintf(
                        dispNameBuf + pos, sizeof(dispNameBuf) - pos,
                        (i == 0) ? "0x%llX" : ",0x%llX",
                        static_cast<unsigned long long>(h)));
                }
            }
        }


        char camoBuf[32] = {};
        if (slot->camoBonusType == 0xFF)
            std::snprintf(camoBuf, sizeof(camoBuf), "(unset)");
        else if (slot->hasCamoBonusValues)
            std::snprintf(camoBuf, sizeof(camoBuf), "%u[values]",
                static_cast<unsigned>(slot->camoBonusType));
        else
            std::snprintf(camoBuf, sizeof(camoBuf), "%u",
                static_cast<unsigned>(slot->camoBonusType));

        Log("[OutfitRegistry] registered key=%s developId=%u flowIndex=%u "
            "playerType=%u partsType=0x%02X selector=0x%02X "
            "enableHead=%d defaultSoldierFaceId=%u headOptions=%u(supports=%d) "
            "langEquipNameHash=0x%016llX camoBonusType=%s "
            "variantCount=%u variantSelectors=[%s] "
            "variantDisplayNameHashes=[%s] "
            "parts=0x%016llX fpk=0x%016llX\n",
            def.key ? def.key : "(unkeyed)",
            static_cast<unsigned>(def.developId),
            static_cast<unsigned>(def.flowIndex),
            static_cast<unsigned>(def.playerType),
            static_cast<unsigned>(partsType),
            static_cast<unsigned>(selector),
            def.enableHead ? 1 : 0,
            static_cast<unsigned>(def.defaultSoldierFaceId),
            static_cast<unsigned>(def.headOptionCount),
            def.supportsHeadOptions ? 1 : 0,
            static_cast<unsigned long long>(def.langEquipNameHash),
            camoBuf,
            static_cast<unsigned>(slot->variantCount),
            variantBuf,
            dispNameBuf,
            static_cast<unsigned long long>(def.partsPathCode64),
            static_cast<unsigned long long>(def.fpkPathCode64));

        return true;
    }

    void ClearAllOutfits()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto& e : g_Entries) e = OutfitEntry{};
    }

    bool TryGetOutfitByPartsType(std::uint8_t partsType, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitBySelectorCode(std::uint8_t selectorCode, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;


            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    return true;
                }
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndex(std::uint16_t flowIndex, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByVariantSelector(std::uint8_t selectorCode,
                                       const OutfitEntry** outEntry,
                                       std::uint8_t* outVariantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (!e.used) continue;

            if (e.selectorCode == selectorCode)
            {
                if (outEntry) *outEntry = &e;
                if (outVariantIndex) *outVariantIndex = 0;
                return true;
            }


            for (std::uint8_t vi = 1; vi < e.variantCount; ++vi)
            {
                if (e.variantSelectorCodes[vi] == selectorCode)
                {
                    if (outEntry) *outEntry = &e;
                    if (outVariantIndex) *outVariantIndex = vi;
                    return true;
                }
            }
        }
        return false;
    }

    std::uint64_t GetVariantDisplayNameHash(std::uint8_t partsType,
                                            std::uint8_t variantIndex)
    {
        if (variantIndex >= outfit::kMaxVariantsPerOutfit) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
                return e.variantDisplayNameHashes[variantIndex];
        }
        return 0;
    }

    bool TryGetOutfitByDevelopId(std::uint16_t developId, const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByCamoVirtualId(std::uint8_t virtualId,
                                     const OutfitEntry** outEntry)
    {


        if (virtualId < kCamoVirtualIdStart || virtualId > kCamoVirtualIdEnd)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used
                && e.hasCamoBonusValues
                && e.camoBonusType == virtualId)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByFlowIndexForPlayerType(
            std::uint16_t flowIndex, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.flowIndex == flowIndex && e.playerType == playerType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    bool TryGetOutfitByDevelopIdForPlayerType(
            std::uint16_t developId, std::uint8_t playerType,
            const OutfitEntry** outEntry)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.developId == developId && e.playerType == playerType)
            {
                if (outEntry) *outEntry = &e;
                return true;
            }
        }
        return false;
    }

    std::size_t GetAllOutfits(const OutfitEntry** outEntries, std::size_t maxEntries)
    {
        if (!outEntries || maxEntries == 0) return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        std::size_t count = 0;
        for (const auto& e : g_Entries)
        {
            if (count >= maxEntries) break;
            if (e.used) outEntries[count++] = &e;
        }
        return count;
    }


    std::uint64_t OutfitEntry::GetVariantPartsPath(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return partsPathCode64;
        return variants[idx].partsPathCode64 != 0
             ? variants[idx].partsPathCode64
             : partsPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantFpkPath(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return fpkPathCode64;
        return variants[idx].fpkPathCode64 != 0
             ? variants[idx].fpkPathCode64
             : fpkPathCode64;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFpk(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return camoFpk;
        const auto v = variants[idx].camoFpk;
        return v == kSubAssetUseVanilla ? camoFpk : v;
    }

    std::uint64_t OutfitEntry::GetVariantCamoFv2(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return camoFv2;
        const auto v = variants[idx].camoFv2;
        return v == kSubAssetUseVanilla ? camoFv2 : v;
    }

    std::uint64_t OutfitEntry::GetVariantDiamondFpk(std::uint8_t idx) const
    {
        if (idx == 0 || idx >= variantCount || !variants[idx].used) return diamondFpk;
        return variants[idx].diamondFpk != kSubAssetDisabled
             ? variants[idx].diamondFpk
             : diamondFpk;
    }

    std::uint8_t ReadLivePartsType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF8] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLiveSelectorCode()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xF9] : static_cast<std::uint8_t>(0xFF);
    }

    std::uint8_t ReadLivePlayerType()
    {
        auto* state = GetQuarkLiveState();
        return state ? state[0xFB] : static_cast<std::uint8_t>(0xFF);
    }

    bool WriteLivePlayerOutfit(std::uint8_t partsType,
                               std::uint8_t selectorCode,
                               std::uint8_t playerType)
    {
        auto* state = GetQuarkLiveState();
        if (!state) return false;

        __try
        {
            state[0xF8] = partsType;     // playerPartsType
            state[0xF9] = selectorCode;  // playerCamoType
            state[0xFB] = playerType;    // playerType
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }


    void SetActiveVariant(std::uint8_t partsType, std::uint8_t variantIndex)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;

        std::uint8_t clamped = variantIndex;


        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& e : g_Entries)
        {
            if (e.used && e.partsType == partsType)
            {
                if (e.variantCount == 0) clamped = 0;
                else if (clamped >= e.variantCount)
                    clamped = static_cast<std::uint8_t>(e.variantCount - 1);
                break;
            }
        }
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = clamped;
    }

    std::uint8_t GetActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_ActiveVariant[partsType - kCustomPartsTypeStart];
    }

    void ClearActiveVariant(std::uint8_t partsType)
    {
        if (partsType < kCustomPartsTypeStart || partsType > kCustomPartsTypeEnd)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_ActiveVariant[partsType - kCustomPartsTypeStart] = 0;
    }


    void SetPendingOutfitDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = developId;
    }

    std::uint16_t GetPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingDevelopId;
    }

    void ClearPendingOutfitDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingDevelopId = 0;
    }

    void SetPendingHeadOptionEquipId(std::uint16_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = equipId;
    }

    std::uint16_t GetPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingHeadOptionEquipId;
    }

    void ClearPendingHeadOptionEquipId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingHeadOptionEquipId = 0;
    }

    static bool g_SupplyDropClickLatch = false;

    void SetSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SupplyDropClickLatch = true;
    }

    bool ConsumeSupplyDropClickLatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const bool was = g_SupplyDropClickLatch;
        g_SupplyDropClickLatch = false;
        return was;
    }

    static std::uint16_t g_PendingSupplyDropDevelopId = 0;

    void SetPendingSupplyDropDevelopId(std::uint16_t developId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropDevelopId = developId;
    }

    std::uint16_t ConsumePendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint16_t was = g_PendingSupplyDropDevelopId;
        g_PendingSupplyDropDevelopId = 0;
        return was;
    }

    std::uint16_t PeekPendingSupplyDropDevelopId()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_PendingSupplyDropDevelopId;
    }

    static std::uint8_t g_PendingSupplyDropVariantIdx = 0;

    void SetPendingSupplyDropVariantIdx(std::uint8_t variantIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_PendingSupplyDropVariantIdx = variantIndex;
    }

    std::uint8_t ConsumePendingSupplyDropVariantIdx()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::uint8_t was = g_PendingSupplyDropVariantIdx;
        g_PendingSupplyDropVariantIdx = 0;
        return was;
    }
}
