#include <pch.h>

#include "CustomHeadRegistry.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../equip/DevelopArrayGrow.h"

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "../../core/V_FrameWorkState.h"
#include "EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"
#include "OutfitRegistry.h"
#include "../../core/FoxHashes.h"

namespace outfit
{
    namespace
    {
        std::deque<CustomHeadEntry>                   g_Heads;
        std::mutex                                    g_Mutex;


        struct PendingHead
        {
            char           name[64]                    = { 0 };
            std::uint16_t  faceIds[kPlayerTypeMax]     = {};
            std::uint64_t  faceFv2Code[kPlayerTypeMax] = {};
            std::uint64_t  faceFpkCode[kPlayerTypeMax] = {};
            std::uint8_t   faceLayerFlags[kPlayerTypeMax] = {};
            HeadLayerPaths layerPaths[kPlayerTypeMax] = {};
            bool           showInDevelopMenu           = false;
            bool           conflictLogged              = false;
            std::uint8_t   slotByte                    = 0;
        };
        std::vector<PendingHead>  g_PendingHeads;
        std::atomic<bool>         g_HasPendingHeads{ false };
        std::atomic<bool>         g_HasProvisionalHeads{ false };

        constexpr const char* kHeadPersistSpace = "CUSTOMHEAD";

        std::int32_t ReadPersistedHeadEquipId(const char* name)
        {
            const std::int32_t v =
                V_FrameWorkState::GetPersistedConstant(kHeadPersistSpace, name);
            return (v > 0) ? (v & 0xFFFF) : 0;
        }

        std::uint8_t ReadPersistedHeadSlot(const char* name)
        {
            const std::int32_t v =
                V_FrameWorkState::GetPersistedConstant(kHeadPersistSpace, name);
            return (v > 0) ? static_cast<std::uint8_t>((v >> 16) & 0xFF)
                           : std::uint8_t{0};
        }

        void PersistHeadRegistration(CustomHeadEntry& e)
        {
            e.savedSlot = e.slotByte;
            V_FrameWorkState::SetPersistedConstant(
                kHeadPersistSpace, e.name,
                static_cast<std::int32_t>(
                    (static_cast<std::uint32_t>(e.slotByte) << 16) | e.equipId));
        }


        struct SnakeFaceStages
        {
            std::uint64_t fv2[kSnakeFaceStageCount] = {};
            std::uint64_t fpk[kSnakeFaceStageCount] = {};
        };
        std::unordered_map<std::string, SnakeFaceStages> g_SnakeStages;
        std::unordered_set<std::string> g_InfiniteAmmoHeads;
        std::unordered_set<std::string> g_StealthAssistHeads;
        std::unordered_set<std::string> g_ChickCapHeads;
        std::unordered_map<std::string, std::string> g_HeadSaluteVoices;
        std::unordered_map<std::string, std::array<CustomHeadHat, kPlayerTypeMax>> g_HeadHats;
        std::unordered_map<std::string, std::uint8_t> g_HeadBranchMasks;
        std::unordered_map<std::string, std::array<std::shared_ptr<const HeadColorSet>, kPlayerTypeMax>> g_HeadColors;

        std::string SnakeStageKey(const char* name)
        {
            std::string key(name);
            if (key.size() > 63)
                key.resize(63);
            return key;
        }

        const SnakeFaceStages* FindSnakeStagesUnlocked(const char* name)
        {
            if (!name || !name[0]) return nullptr;
            const auto it = g_SnakeStages.find(SnakeStageKey(name));
            return it != g_SnakeStages.end() ? &it->second : nullptr;
        }

        using GetQuarkSystemTable_t = void* (__fastcall*)();
        static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

        std::uint8_t ReadWornHeadSlot()   // vars+0xFE: the worn head's slot when it is at most 0xFF
        {
            if (!g_GetQuarkSystemTable)
            {
                g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable));
                if (!g_GetQuarkSystemTable) return 0;
            }
            std::uint16_t face = 0;
            __try
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (!qt) return 0;
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (!q98) return 0;
                auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                if (!state) return 0;
                face = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
            return face <= 0xFF ? static_cast<std::uint8_t>(face) : std::uint8_t{0};
        }

        std::int32_t FindByNameUnlocked(const char* name)
        {
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                if (g_Heads[i].used && std::strcmp(g_Heads[i].name, name) == 0)
                    return static_cast<std::int32_t>(i);
            }
            return -1;
        }

        std::int32_t AllocateUnlocked()
        {
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                if (!g_Heads[i].used)
                    return static_cast<std::int32_t>(i);
            }
            g_Heads.emplace_back();
            return static_cast<std::int32_t>(g_Heads.size() - 1);
        }

        bool IsSlotTakenUnlocked(std::uint8_t slotByte, const char* ownerName = nullptr)
        {
            for (const CustomHeadEntry& e : g_Heads)
            {
                if (e.used && e.slotByte == slotByte)
                    return true;
            }
            for (const PendingHead& p : g_PendingHeads)
            {
                if (p.slotByte == slotByte
                    && (!ownerName || std::strcmp(p.name, ownerName) != 0))
                    return true;
            }
            return false;
        }

        std::uint8_t AllocateSlotUnlocked()
        {
            for (std::uint32_t s = kCustomHeadSlotBase; s < 0x100u; ++s)
            {
                if (!IsSlotTakenUnlocked(static_cast<std::uint8_t>(s)))
                    return static_cast<std::uint8_t>(s);
            }
            return 0;
        }

        constexpr ULONGLONG kPreviewHoldMs = 2000;   // a slot used this recently is not taken for another head's preview

        void KeepSlotSavedUnlocked(CustomHeadEntry& e)   // the slot the game saves for this head is the one the state file names
        {
            if (e.slotByte < kCustomHeadSlotBase || e.savedSlot == e.slotByte)
                return;
            for (CustomHeadEntry& other : g_Heads)
            {
                if (&other == &e || !other.used || other.savedSlot != e.slotByte)
                    continue;
                other.savedSlot = 0;
                V_FrameWorkState::SetPersistedConstant(kHeadPersistSpace, other.name,
                                                       static_cast<std::int32_t>(other.equipId));
            }
            PersistHeadRegistration(e);
        }

        std::uint8_t BorrowSlotUnlocked(CustomHeadEntry& e, std::uint8_t worn, bool wearing)
        {
            const ULONGLONG now = GetTickCount64();
            if (e.slotByte < kCustomHeadSlotBase)
            {
                std::uint8_t slot = AllocateSlotUnlocked();
                if (slot < kCustomHeadSlotBase)
                {
                    CustomHeadEntry* oldest = nullptr;
                    for (CustomHeadEntry& h : g_Heads)
                    {
                        if (!h.used || h.slotByte < kCustomHeadSlotBase || h.slotByte == worn)
                            continue;
                        if (!wearing && now - h.lastUse < kPreviewHoldMs)
                            continue;
                        if (!oldest || h.lastUse < oldest->lastUse)
                            oldest = &h;
                    }
                    if (!oldest)
                        return 0;
                    slot = oldest->slotByte;
                    oldest->slotByte = 0;
                }
                e.slotByte = slot;
            }
            e.lastUse = now;
            if (wearing || e.slotByte == worn)
                KeepSlotSavedUnlocked(e);
            return e.slotByte;
        }


        static void* WalkToEquipDevelopController()
        {
            if (!g_GetQuarkSystemTable)
            {
                g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable));
                if (!g_GetQuarkSystemTable) return nullptr;
            }

            __try
            {
                auto* qst = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (!qst) return nullptr;
                auto* app = *reinterpret_cast<std::uint8_t**>(qst + 0x98);
                if (!app) return nullptr;
                auto* base = *reinterpret_cast<std::uint8_t**>(app + 0x110);
                if (!base) return nullptr;
                return *reinterpret_cast<void**>(base + 0xac8);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }


        static std::int32_t TranslateDevelopIdToRowIndex(std::uint32_t developId)
        {
            void* edc = WalkToEquipDevelopController();
            if (!edc) return -1;

            __try
            {
                using GetIdx_t = std::uint16_t (__fastcall*)(void*, std::uint16_t);
                void** vt = *reinterpret_cast<void***>(edc);
                if (!vt) return -1;
                auto getIdx = reinterpret_cast<GetIdx_t>(vt[0xE0 / 8]);
                if (!getIdx) return -1;
                const std::uint16_t row = getIdx(edc, static_cast<std::uint16_t>(developId));
                if (row == 0 || row == 0xFFFF) return -1;
                return static_cast<std::int32_t>(row);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }


        static bool IsResolvedRowValidUnlocked(std::int32_t row)
        {
            if (row <= 0 || row >= 0xFFFF) return false;
            if (row == static_cast<std::int32_t>(kHeadOption_None)) return false;
            for (const CustomHeadEntry& e : g_Heads)
                if (e.used && e.equipId == static_cast<std::uint16_t>(row))
                    return false;
            return true;
        }

        static std::uint16_t CompleteHeadRegistrationUnlocked(
            const char* name, const std::uint16_t* faceIds,
            const std::uint64_t* faceFv2Codes, const std::uint64_t* faceFpkCodes,
            const std::uint8_t* faceLayerFlags,
            const HeadLayerPaths* layerPaths,
            std::uint16_t developId, std::uint16_t rowIndex,
            bool showInDevelopMenu, bool provisional)
        {
            const std::int32_t idx = AllocateUnlocked();

            std::uint8_t slotByte = ReadPersistedHeadSlot(name);
            if (slotByte < kCustomHeadSlotBase || IsSlotTakenUnlocked(slotByte, name))
                slotByte = AllocateSlotUnlocked();   // 0 past 250 heads: it borrows a slot when worn or previewed

            CustomHeadEntry& e = g_Heads[idx];
            e.used            = true;
            e.provisional     = provisional;
            e.hiddenInDevelop = !showInDevelopMenu;
            e.rehomeBlockLogged = false;
            e.equipId      = rowIndex;
            e.developId    = developId;
            e.flowIndex    = 0;
            e.slotByte     = slotByte;
            e.lastUse      = 0;
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
                e.TppEnemyFaceId[i] = faceIds[i];
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
            {
                e.faceFv2Code[i]    = faceFv2Codes ? faceFv2Codes[i] : 0;
                e.faceFpkCode[i]    = faceFpkCodes ? faceFpkCodes[i] : 0;
                e.faceLayerFlags[i] = faceLayerFlags ? faceLayerFlags[i] : std::uint8_t{0};
                e.layerPaths[i] = layerPaths ? layerPaths[i] : HeadLayerPaths{};
            }

            const std::size_t nameLen = std::strlen(name);
            const std::size_t copyLen = nameLen < (sizeof(e.name) - 1)
                ? nameLen : (sizeof(e.name) - 1);
            std::memcpy(e.name, name, copyLen);
            e.name[copyLen] = '\0';

            LogInfo("RegisterHeadOption %s equipId:%u developId:%u slot:0x%02X",
                    e.name, static_cast<unsigned>(e.equipId),
                    static_cast<unsigned>(e.developId),
                    static_cast<unsigned>(e.slotByte));

            if (!showInDevelopMenu)
            {
                SetDevelopHidden(e.equipId);
                equip::SilenceDevelopRowAnnounce(
                    static_cast<std::int32_t>(e.developId));
            }

            if (provisional)
                g_HasProvisionalHeads.store(true, std::memory_order_release);

            PersistHeadRegistration(e);
            ResolvePendingHeadName(FoxHashes::StrCode64(e.name), e.equipId);
            return e.equipId;
        }

        static void StorePendingHeadUnlocked(
            const char* name, const std::uint16_t* faceIds,
            const std::uint64_t* faceFv2Codes, const std::uint64_t* faceFpkCodes,
            const std::uint8_t* faceLayerFlags, const HeadLayerPaths* layerPaths,
            bool showInDevelopMenu)
        {
            for (PendingHead& p : g_PendingHeads)
            {
                if (std::strcmp(p.name, name) == 0)
                {
                    for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
                    {
                        p.faceIds[i]        = faceIds[i];
                        p.faceFv2Code[i]    = faceFv2Codes ? faceFv2Codes[i] : 0;
                        p.faceFpkCode[i]    = faceFpkCodes ? faceFpkCodes[i] : 0;
                        p.faceLayerFlags[i] = faceLayerFlags ? faceLayerFlags[i] : std::uint8_t{0};
                        p.layerPaths[i] = layerPaths ? layerPaths[i] : HeadLayerPaths{};
                    }
                    p.showInDevelopMenu = showInDevelopMenu;
                    return;
                }
            }
            PendingHead p{};
            const std::size_t nameLen = std::strlen(name);
            const std::size_t copyLen = nameLen < (sizeof(p.name) - 1)
                ? nameLen : (sizeof(p.name) - 1);
            std::memcpy(p.name, name, copyLen);
            p.name[copyLen] = '\0';
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
            {
                p.faceIds[i]        = faceIds[i];
                p.faceFv2Code[i]    = faceFv2Codes ? faceFv2Codes[i] : 0;
                p.faceFpkCode[i]    = faceFpkCodes ? faceFpkCodes[i] : 0;
                p.faceLayerFlags[i] = faceLayerFlags ? faceLayerFlags[i] : std::uint8_t{0};
                p.layerPaths[i] = layerPaths ? layerPaths[i] : HeadLayerPaths{};
            }
            p.showInDevelopMenu = showInDevelopMenu;
            p.slotByte = ReadPersistedHeadSlot(name);
            g_PendingHeads.push_back(p);
            g_HasPendingHeads.store(true, std::memory_order_release);
        }

        struct ProvisionalRehome
        {
            CustomHeadEntry* head       = nullptr;
            std::uint16_t    developRow = 0;
            bool             blocked    = false;
        };

        bool IsLeavingItsRow(const CustomHeadEntry& holder,
                             const std::vector<ProvisionalRehome>& rehomes)
        {
            for (const ProvisionalRehome& rehome : rehomes)
            {
                if (rehome.head == &holder)
                    return !rehome.blocked;
            }
            return false;
        }

        bool IsRowWantedByAnotherRehome(const ProvisionalRehome& rehome,
                                        const std::vector<ProvisionalRehome>& rehomes)
        {
            for (const ProvisionalRehome& other : rehomes)
            {
                if (&other != &rehome && other.developRow == rehome.developRow)
                    return true;
            }
            return false;
        }

        bool IsRowHeldByStayingHeadUnlocked(const ProvisionalRehome& rehome,
                                            const std::vector<ProvisionalRehome>& rehomes)
        {
            for (const CustomHeadEntry& holder : g_Heads)
            {
                if (holder.used && &holder != rehome.head
                    && holder.equipId == rehome.developRow
                    && !IsLeavingItsRow(holder, rehomes))
                    return true;
            }
            return false;
        }

        void BlockUnreachableRehomesUnlocked(std::vector<ProvisionalRehome>& rehomes)
        {
            bool blockedAny = true;
            while (blockedAny)
            {
                blockedAny = false;
                for (ProvisionalRehome& rehome : rehomes)
                {
                    if (rehome.blocked)
                        continue;
                    if (!IsRowWantedByAnotherRehome(rehome, rehomes)
                        && !IsRowHeldByStayingHeadUnlocked(rehome, rehomes))
                        continue;
                    rehome.blocked = true;
                    blockedAny     = true;
                    if (!rehome.head->rehomeBlockLogged)
                    {
                        rehome.head->rehomeBlockLogged = true;
                        LogWarn("CustomHead: '%s' cannot move to develop row %u, "
                                "keeps equipId %u",
                                rehome.head->name,
                                static_cast<unsigned>(rehome.developRow),
                                static_cast<unsigned>(rehome.head->equipId));
                    }
                }
            }
        }

        void ReleaseHiddenRowUnlocked(std::uint16_t row, bool& refreshGates)
        {
            refreshGates = true;
            if (EquipDevelopAdd::IsGatedFlowIndex(row))
                return;
            std::uint32_t occupant = 0;
            const bool keepHidden =
                equip::TryReadRowDevelopId(row, occupant)
                && IsStaticDevelopHidden(static_cast<unsigned short>(occupant));
            SetDevelopHidden(row, keepHidden);
        }

        void ApplyRehomesUnlocked(const std::vector<ProvisionalRehome>& rehomes,
                                  bool& refreshGates)
        {
            std::vector<CustomHeadEntry*> movedHeads;
            std::vector<HeadOptionRemap>  remaps;
            for (const ProvisionalRehome& rehome : rehomes)
            {
                if (rehome.blocked)
                    continue;
                HeadOptionRemap remap;
                remap.oldEquipId = rehome.head->equipId;
                remap.newEquipId = rehome.developRow;
                remaps.push_back(remap);
                movedHeads.push_back(rehome.head);
            }
            if (remaps.empty())
                return;

            for (std::size_t i = 0; i < movedHeads.size(); ++i)
            {
                if (movedHeads[i]->hiddenInDevelop)
                    ReleaseHiddenRowUnlocked(remaps[i].oldEquipId, refreshGates);
            }

            for (std::size_t i = 0; i < movedHeads.size(); ++i)
            {
                CustomHeadEntry& head = *movedHeads[i];
                head.equipId     = remaps[i].newEquipId;
                head.provisional = false;
                head.rehomeBlockLogged = false;
                if (head.hiddenInDevelop)
                    SetDevelopHidden(head.equipId, true);
                PersistHeadRegistration(head);
            }

            RemapHeadOptionEquipIds(remaps.data(), remaps.size());
        }

        bool IsDevelopRowValid(std::int32_t row)
        {
            return row > 0 && row < 0xFFFF
                && row != static_cast<std::int32_t>(kHeadOption_None);
        }

        enum class RowlessCause
        {
            NoDevelopRequest,
            PagedOut,
            NotInjectedYet,
        };

        void EvictRowlessHeadUnlocked(CustomHeadEntry& holder, const char* newOwner,
                                      RowlessCause cause,
                                      std::vector<PendingHead>& parked,
                                      bool& refreshGates)
        {
            const std::uint16_t staleRow = holder.equipId;
            UnresolveHeadOption(staleRow, FoxHashes::StrCode64(holder.name));

            if (holder.hiddenInDevelop)
                ReleaseHiddenRowUnlocked(staleRow, refreshGates);

            PendingHead p{};
            std::memcpy(p.name, holder.name, sizeof(p.name));
            p.name[sizeof(p.name) - 1] = '\0';
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
            {
                p.faceIds[i]        = holder.TppEnemyFaceId[i];
                p.faceFv2Code[i]    = holder.faceFv2Code[i];
                p.faceFpkCode[i]    = holder.faceFpkCode[i];
                p.faceLayerFlags[i] = holder.faceLayerFlags[i];
                p.layerPaths[i] = holder.layerPaths[i];
            }
            p.showInDevelopMenu = !holder.hiddenInDevelop;
            p.slotByte          = holder.slotByte;
            parked.push_back(p);

            V_FrameWorkState::SetPersistedConstant(
                kHeadPersistSpace, holder.name,
                static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(holder.slotByte) << 16));

            if (cause == RowlessCause::NoDevelopRequest)
                LogWarn("CustomHead: '%s' has no AddToEquipDevelopTable this session, "
                        "develop row %u goes to %s; head unlisted",
                        holder.name, static_cast<unsigned>(staleRow),
                        newOwner ? newOwner : "?");

            holder.equipId = 0;
        }
    }

    std::uint16_t RegisterHeadOption(
        const char* name,
        const std::uint16_t* TppEnemyFaceIdsPerPt,
        const std::uint64_t* faceFv2CodesPerPt,
        const std::uint64_t* faceFpkCodesPerPt,
        bool showInDevelopMenu,
        const std::uint8_t* faceLayerFlagsPerPt,
        const HeadLayerPaths* layerPathsPerPt)
    {
        static_assert(kPlayerTypeMax >= 4,
                      "head registration needs the vanilla player types");

        if (!name || !name[0])
        {
            LogWarn("RegisterHeadOption: missing name, head not registered");
            return 0;
        }

        if (std::strlen(name) >= sizeof(CustomHeadEntry::name))
        {
            LogWarn("RegisterHeadOption '%.40s...': key over %zu characters, not "
                    "registered", name, sizeof(CustomHeadEntry::name) - 1);
            return 0;
        }

        std::uint16_t faceIds[kPlayerTypeMax];
        for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
        {
            const std::uint16_t v = TppEnemyFaceIdsPerPt ? TppEnemyFaceIdsPerPt[i] : 0;
            const std::uint16_t fallback = (i == kPlayerType_DDMale)
                ? kDefaultTppEnemyFaceIdMale : kDefaultTppEnemyFaceId;
            faceIds[i] = (v != 0) ? v : fallback;
        }

        std::unique_lock<std::mutex> lock(g_Mutex);

        if (const std::int32_t existing = FindByNameUnlocked(name); existing >= 0)
        {
            CustomHeadEntry& e = g_Heads[existing];
            for (std::size_t i = 0; i < kPlayerTypeMax; ++i)
            {
                e.TppEnemyFaceId[i] = faceIds[i];
                e.faceFv2Code[i]    = faceFv2CodesPerPt ? faceFv2CodesPerPt[i] : 0;
                e.faceFpkCode[i]    = faceFpkCodesPerPt ? faceFpkCodesPerPt[i] : 0;
                e.faceLayerFlags[i] = faceLayerFlagsPerPt
                    ? faceLayerFlagsPerPt[i] : std::uint8_t{0};
                e.layerPaths[i] = layerPathsPerPt ? layerPathsPerPt[i] : HeadLayerPaths{};
            }

            const bool hide = !showInDevelopMenu;
            if (hide && !e.hiddenInDevelop)
            {
                SetDevelopHidden(e.equipId, true);
                equip::SilenceDevelopRowAnnounce(
                    static_cast<std::int32_t>(e.developId));
            }
            bool refreshGates = false;
            if (!hide && e.hiddenInDevelop)
                ReleaseHiddenRowUnlocked(e.equipId, refreshGates);
            e.hiddenInDevelop = hide;
            const std::uint16_t existingEquipId = e.equipId;
            if (refreshGates)
            {
                lock.unlock();
                EquipDevelopAdd::MaybeRefreshDynamicGates();
            }
            return existingEquipId;
        }

        std::int32_t developId = 0;
        if (!V_FrameWorkState::ResolveOrCreateDevelopId(name, 0, developId)
            || developId <= 0 || developId > 0xFFFF)
        {
            LogError("RegisterHeadOption '%s': developId allocation failed (%d), "
                     "not registered", name, developId);
            return 0;
        }
        V_FrameWorkState::SetRowKind(name, V_FrameWorkState::kRowKindHeadOption);

        const std::int32_t rowIndex =
            TranslateDevelopIdToRowIndex(static_cast<std::uint32_t>(developId));
        std::uint16_t equipId = 0;
        if (IsResolvedRowValidUnlocked(rowIndex))
        {
            equipId = CompleteHeadRegistrationUnlocked(
                name, faceIds, faceFv2CodesPerPt, faceFpkCodesPerPt,
                faceLayerFlagsPerPt, layerPathsPerPt,
                static_cast<std::uint16_t>(developId),
                static_cast<std::uint16_t>(rowIndex), showInDevelopMenu, false);
            if (equipId == 0)
                return 0;
        }
        else
        {
            std::int32_t provisionalRow = ReadPersistedHeadEquipId(name);
            if (!IsResolvedRowValidUnlocked(provisionalRow))
                provisionalRow = V_FrameWorkState::GetPersistedFlowIndex(name);
            if (IsResolvedRowValidUnlocked(provisionalRow))
            {
                equipId = CompleteHeadRegistrationUnlocked(
                    name, faceIds, faceFv2CodesPerPt, faceFpkCodesPerPt,
                    faceLayerFlagsPerPt, layerPathsPerPt,
                    static_cast<std::uint16_t>(developId),
                    static_cast<std::uint16_t>(provisionalRow), showInDevelopMenu,
                    true);
            }
        }
        if (equipId != 0)
        {
            for (auto it = g_PendingHeads.begin(); it != g_PendingHeads.end(); ++it)
            {
                if (std::strcmp(it->name, name) == 0)
                {
                    g_PendingHeads.erase(it);
                    break;
                }
            }
            if (g_PendingHeads.empty())
                g_HasPendingHeads.store(false, std::memory_order_release);
            return equipId;
        }

        StorePendingHeadUnlocked(name, faceIds, faceFv2CodesPerPt,
                                 faceFpkCodesPerPt, faceLayerFlagsPerPt,
                                 layerPathsPerPt, showInDevelopMenu);
        return 0;
    }

    void SetCustomHeadSnakeFaceStages(const char* name,
                                      const std::uint64_t* fv2ByStage,
                                      const std::uint64_t* fpkByStage)
    {
        if (!name || !name[0]) return;

        std::lock_guard<std::mutex> lock(g_Mutex);
        SnakeFaceStages& slot = g_SnakeStages[SnakeStageKey(name)];
        for (std::uint8_t i = 0; i < kSnakeFaceStageCount; ++i)
        {
            slot.fv2[i] = fv2ByStage ? fv2ByStage[i] : 0;
            slot.fpk[i] = fpkByStage ? fpkByStage[i] : 0;
        }
    }

    std::uint64_t GetCustomHeadSnakeStageFv2(const char* name,
                                             std::uint32_t stage)
    {
        if (stage >= kSnakeFaceStageCount) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const SnakeFaceStages* s = FindSnakeStagesUnlocked(name);
        return s ? s->fv2[stage] : 0;
    }

    std::uint64_t GetCustomHeadSnakeStageFpk(const char* name,
                                             std::uint32_t stage)
    {
        if (stage >= kSnakeFaceStageCount) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const SnakeFaceStages* s = FindSnakeStagesUnlocked(name);
        return s ? s->fpk[stage] : 0;
    }

    void SetCustomHeadInfiniteAmmo(const char* name, bool enable)
    {
        if (!name || !name[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (enable)
            g_InfiniteAmmoHeads.insert(SnakeStageKey(name));
        else
            g_InfiniteAmmoHeads.erase(SnakeStageKey(name));
    }

    bool GetCustomHeadInfiniteAmmo(const char* name)
    {
        if (!name || !name[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_InfiniteAmmoHeads.count(SnakeStageKey(name)) != 0;
    }

    void SetCustomHeadMissionAbilities(const char* name, bool stealthAssist, bool chickCap)
    {
        if (!name || !name[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::string key = SnakeStageKey(name);
        if (stealthAssist) g_StealthAssistHeads.insert(key);
        else               g_StealthAssistHeads.erase(key);
        if (chickCap) g_ChickCapHeads.insert(key);
        else          g_ChickCapHeads.erase(key);
    }

    bool GetCustomHeadStealthAssist(const char* name)
    {
        if (!name || !name[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_StealthAssistHeads.count(SnakeStageKey(name)) != 0;
    }

    bool GetCustomHeadChickCap(const char* name)
    {
        if (!name || !name[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_ChickCapHeads.count(SnakeStageKey(name)) != 0;
    }

    void SetCustomHeadSaluteVoice(const char* name, const char* line)
    {
        if (!name || !name[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (line && line[0])
            g_HeadSaluteVoices[SnakeStageKey(name)] = line;
        else
            g_HeadSaluteVoices.erase(SnakeStageKey(name));
    }

    std::string GetCustomHeadSaluteVoice(const char* name)
    {
        if (!name || !name[0]) return std::string();
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_HeadSaluteVoices.find(SnakeStageKey(name));
        return it != g_HeadSaluteVoices.end() ? it->second : std::string();
    }

    void SetCustomHeadHat(const char* name, std::uint8_t playerType, const CustomHeadHat& hat)
    {
        if (!name || !name[0] || playerType >= kPlayerTypeMax) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_HeadHats[SnakeStageKey(name)][playerType] = hat;
    }

    bool GetCustomHeadHat(const char* name, std::uint8_t playerType, CustomHeadHat* out)
    {
        if (!name || !name[0] || playerType >= kPlayerTypeMax || !out) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_HeadHats.find(SnakeStageKey(name));
        if (it == g_HeadHats.end()) return false;
        const CustomHeadHat& hat = it->second[playerType];
        if (hat.fpk == 0 || hat.fv2 == 0) return false;
        *out = hat;
        return true;
    }

    void SetCustomHeadBranchMask(const char* name, std::uint8_t mask)
    {
        if (!name || !name[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_HeadBranchMasks[SnakeStageKey(name)] = mask;
    }

    std::uint8_t GetCustomHeadBranchMask(const char* name)
    {
        if (!name || !name[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_HeadBranchMasks.find(SnakeStageKey(name));
        return it != g_HeadBranchMasks.end() ? it->second : std::uint8_t{0};
    }

    void SetCustomHeadColors(const char* name, const HeadColorSet (&perPlayerType)[kPlayerTypeMax])
    {
        if (!name || !name[0]) return;
        std::array<std::shared_ptr<const HeadColorSet>, kPlayerTypeMax> sets{};
        bool any = false;
        for (std::uint8_t pt = 0; pt < kPlayerTypeMax; ++pt)
        {
            bool has = false;
            for (const OutfitColorList& list : perPlayerType[pt].colors)
                has = has || !list.empty();
            if (!has)
                continue;
            sets[pt] = std::make_shared<const HeadColorSet>(perPlayerType[pt]);
            any = true;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (any)
            g_HeadColors[SnakeStageKey(name)] = std::move(sets);
        else
            g_HeadColors.erase(SnakeStageKey(name));
    }

    std::shared_ptr<const HeadColorSet> GetCustomHeadColors(const char* name, std::uint8_t playerType)
    {
        if (!name || !name[0] || playerType >= kPlayerTypeMax) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_HeadColors.find(SnakeStageKey(name));
        return it != g_HeadColors.end() ? it->second[playerType] : nullptr;
    }

    static std::atomic<DWORD> g_PendingHeadsRetryTick{ 0 };
    static std::atomic<bool>  g_DevelopRowsMoved{ false };

    bool HasPendingCustomHeads()
    {
        return g_HasPendingHeads.load(std::memory_order_acquire);
    }

    void NoteDevelopRowsMoved()
    {
        if (!g_DevelopRowsMoved.exchange(true, std::memory_order_acq_rel))
            g_PendingHeadsRetryTick.store(0, std::memory_order_relaxed);
    }

    bool IsCustomHeadSlotReserved(std::uint8_t slotByte)
    {
        if (slotByte < kCustomHeadSlotBase)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return IsSlotTakenUnlocked(slotByte);
    }

    int DrainPendingHeads()
    {
        if (!g_HasPendingHeads.load(std::memory_order_acquire)
            && !g_HasProvisionalHeads.load(std::memory_order_acquire)
            && !g_DevelopRowsMoved.load(std::memory_order_acquire))
            return 0;

        const DWORD lastFail =
            g_PendingHeadsRetryTick.load(std::memory_order_relaxed);
        if (lastFail != 0 && (GetTickCount() - lastFail) < 250)
            return 0;

        int  resolved     = 0;
        bool refreshGates = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_DevelopRowsMoved.store(false, std::memory_order_release);

            std::vector<std::int32_t> developRows(g_Heads.size(), -1);
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                developRows[i] = g_Heads[i].used
                    ? TranslateDevelopIdToRowIndex(
                          static_cast<std::uint32_t>(g_Heads[i].developId))
                    : -1;
            }

            std::vector<std::int32_t> pendingDevelopIds;
            std::vector<std::int32_t> pendingRows;
            pendingDevelopIds.reserve(g_PendingHeads.size());
            pendingRows.reserve(g_PendingHeads.size());
            for (const PendingHead& p : g_PendingHeads)
            {
                const std::int32_t developId =
                    V_FrameWorkState::GetDevelopIdByKey(p.name);
                pendingDevelopIds.push_back(developId);
                pendingRows.push_back((developId > 0 && developId <= 0xFFFF)
                    ? TranslateDevelopIdToRowIndex(
                          static_cast<std::uint32_t>(developId))
                    : -1);
            }

            std::vector<PendingHead>      parked;
            std::vector<CustomHeadEntry*> evicted;
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                CustomHeadEntry& e = g_Heads[i];
                if (!e.used || e.equipId == 0 || IsDevelopRowValid(developRows[i]))
                    continue;
                const std::int32_t heldRow = static_cast<std::int32_t>(e.equipId);
                const char* newOwner = nullptr;
                for (std::size_t j = 0; j < g_Heads.size() && !newOwner; ++j)
                {
                    if (j != i && g_Heads[j].used && developRows[j] == heldRow)
                        newOwner = g_Heads[j].name;
                }
                for (std::size_t k = 0; k < g_PendingHeads.size() && !newOwner; ++k)
                {
                    if (pendingRows[k] == heldRow)
                        newOwner = g_PendingHeads[k].name;
                }
                char ownerText[96] = { 0 };
                std::uint32_t occupant = 0;
                if (newOwner)
                {
                    std::snprintf(ownerText, sizeof(ownerText), "'%s'", newOwner);
                }
                else if (equip::TryReadRowDevelopId(e.equipId, occupant)
                         && occupant != 0 && occupant != e.developId)
                {
                    std::snprintf(ownerText, sizeof(ownerText), "developId %u",
                        static_cast<unsigned>(occupant));
                }
                else
                {
                    continue;
                }
                const RowlessCause cause =
                    !EquipDevelopAdd::HasDevelopRequest(e.name)
                        ? RowlessCause::NoDevelopRequest
                        : (EquipDevelopAdd::IsDevelopKeyParked(e.name)
                           || EquipDevelopAdd::IsDevelopIdParked(e.developId)
                               ? RowlessCause::PagedOut
                               : RowlessCause::NotInjectedYet);
                if (!newOwner && cause == RowlessCause::NotInjectedYet)
                    continue;
                EvictRowlessHeadUnlocked(e, ownerText, cause, parked, refreshGates);
                evicted.push_back(&e);
            }

            bool provisionalLeft = false;
            std::vector<ProvisionalRehome> rehomes;
            for (std::size_t i = 0; i < g_Heads.size(); ++i)
            {
                CustomHeadEntry& e = g_Heads[i];
                if (!e.used || e.equipId == 0)
                    continue;
                if (!IsDevelopRowValid(developRows[i]))
                {
                    if (e.provisional)
                        provisionalLeft = true;
                    continue;
                }
                if (developRows[i] == static_cast<std::int32_t>(e.equipId))
                {
                    e.provisional = false;
                    continue;
                }
                ProvisionalRehome rehome;
                rehome.head       = &e;
                rehome.developRow = static_cast<std::uint16_t>(developRows[i]);
                rehomes.push_back(rehome);
            }
            BlockUnreachableRehomesUnlocked(rehomes);
            for (const ProvisionalRehome& rehome : rehomes)
            {
                if (rehome.blocked)
                    provisionalLeft = true;
            }
            ApplyRehomesUnlocked(rehomes, refreshGates);

            std::size_t k = 0;
            for (auto it = g_PendingHeads.begin(); it != g_PendingHeads.end(); ++k)
            {
                const std::int32_t rowIndex = pendingRows[k];
                if (IsResolvedRowValidUnlocked(rowIndex))
                {
                    CompleteHeadRegistrationUnlocked(
                        it->name, it->faceIds, it->faceFv2Code, it->faceFpkCode,
                        it->faceLayerFlags, it->layerPaths,
                        static_cast<std::uint16_t>(pendingDevelopIds[k]),
                        static_cast<std::uint16_t>(rowIndex),
                        it->showInDevelopMenu, false);
                    it = g_PendingHeads.erase(it);
                    ++resolved;
                    continue;
                }
                if (IsDevelopRowValid(rowIndex) && !it->conflictLogged)
                {
                    const char* holderName = "?";
                    for (const CustomHeadEntry& e : g_Heads)
                    {
                        if (e.used && e.equipId == static_cast<std::uint16_t>(rowIndex))
                        {
                            holderName = e.name;
                            break;
                        }
                    }
                    it->conflictLogged = true;
                    LogWarn("CustomHead: '%s' unlisted, develop row %d held by '%s'",
                            it->name, rowIndex, holderName);
                }
                ++it;
            }

            for (CustomHeadEntry* e : evicted)
                *e = CustomHeadEntry{};
            if (!parked.empty())
            {
                for (const PendingHead& p : parked)
                    g_PendingHeads.push_back(p);
                g_HasPendingHeads.store(true, std::memory_order_release);
            }
            g_HasProvisionalHeads.store(provisionalLeft, std::memory_order_release);

            if (g_PendingHeads.empty())
                g_HasPendingHeads.store(false, std::memory_order_release);

            if (g_PendingHeads.empty() && !provisionalLeft)
                g_PendingHeadsRetryTick.store(0, std::memory_order_relaxed);
            else
                g_PendingHeadsRetryTick.store(GetTickCount(),
                                              std::memory_order_relaxed);
            if (g_DevelopRowsMoved.load(std::memory_order_acquire))
                g_PendingHeadsRetryTick.store(0, std::memory_order_relaxed);
        }
        if (refreshGates)
            EquipDevelopAdd::MaybeRefreshDynamicGates();
        return resolved;
    }

    const CustomHeadEntry* TryGetCustomHeadByName(const char* name)
    {
        if (!name || !name[0]) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const std::int32_t idx = FindByNameUnlocked(name);
        return (idx >= 0) ? &g_Heads[idx] : nullptr;
    }

    const CustomHeadEntry* TryGetCustomHeadByEquipId(std::uint16_t equipId)
    {
        if (equipId == 0) return nullptr;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const CustomHeadEntry& e : g_Heads)
        {
            if (e.used && e.equipId == equipId)
                return &e;
        }
        return nullptr;
    }

    bool IsFv2PathCode(std::uint64_t pathCode64Ext)
    {
        static const std::uint64_t kFv2Ext =
            FoxHashes::PathCode64Ext("/Assets/tpp/fova/probe.fv2") >> 51;
        return pathCode64Ext != 0 && (pathCode64Ext >> 51) == kFv2Ext;
    }

    bool IsFpkPathCode(std::uint64_t pathCode64Ext)
    {
        static const std::uint64_t kFpkExt =
            FoxHashes::PathCode64Ext("/Assets/tpp/pack/probe.fpk") >> 51;
        static const std::uint64_t kFpkdExt =
            FoxHashes::PathCode64Ext("/Assets/tpp/pack/probe.fpkd") >> 51;
        const std::uint64_t ext = pathCode64Ext >> 51;
        return pathCode64Ext != 0 && (ext == kFpkExt || ext == kFpkdExt);
    }

    bool TryGetDdHeadBranch(std::uint8_t slotByte, std::uint8_t playerType,
                            DdHeadLayers* out)
    {
        if (!out || slotByte < kCustomHeadSlotBase || !IsDdPlayerType(playerType))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const CustomHeadEntry& e : g_Heads)
        {
            if (!e.used || e.slotByte != slotByte)
                continue;
            out->flags       = e.faceLayerFlags[playerType];
            out->enemyFaceId = e.TppEnemyFaceId[playerType];
            out->branchFv2   = e.faceFv2Code[playerType];
            out->branchFpk   = e.faceFpkCode[playerType];
            out->name        = e.name;
            out->paths       = e.layerPaths[playerType];
            return true;
        }
        return false;
    }

    bool TryGetDdHeadLayers(std::uint8_t slotByte, std::uint8_t playerType,
                            DdHeadLayers* out)
    {
        return TryGetDdHeadBranch(slotByte, playerType, out)
            && (out->flags & kHeadLayerDeclared) != 0;
    }

    const CustomHeadEntry* TryGetCustomHeadBySlot(std::uint8_t slotByte)
    {
        if (slotByte < kCustomHeadSlotBase) return nullptr;
        const std::uint8_t worn = ReadWornHeadSlot();
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (CustomHeadEntry& e : g_Heads)
        {
            if (!e.used || e.slotByte != slotByte)
                continue;
            e.lastUse = GetTickCount64();
            if (slotByte == worn)
                KeepSlotSavedUnlocked(e);
            return &e;
        }
        return nullptr;
    }

    std::uint8_t BorrowCustomHeadSlot(std::uint16_t equipId, bool wearing)
    {
        if (equipId == 0) return 0;
        const std::uint8_t worn = ReadWornHeadSlot();
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (CustomHeadEntry& e : g_Heads)
        {
            if (e.used && e.equipId == equipId)
                return BorrowSlotUnlocked(e, worn, wearing);
        }
        return 0;
    }

    std::vector<const CustomHeadEntry*> ListCustomHeads()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::vector<const CustomHeadEntry*> heads;
        for (const CustomHeadEntry& e : g_Heads)
        {
            if (e.used && e.name[0])
                heads.push_back(&e);
        }
        return heads;
    }

    std::uint16_t GetCurrentWornHeadEquipId()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!g_GetQuarkSystemTable) return 0;
        }

        std::uint16_t faceEquipId = 0;
        __try
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!qt) return 0;
            auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
            if (!q98) return 0;
            auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
            if (!state) return 0;
            faceEquipId = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }

        if (faceEquipId > 0xFF)
            return 0;
        if (const CustomHeadEntry* head =
                TryGetCustomHeadBySlot(static_cast<std::uint8_t>(faceEquipId)))
            return head->equipId;
        return 0;
    }

    bool GetCurrentEquippedSuitBytes(std::uint8_t* outPartsType,
                                     std::uint8_t* outSelector)
    {
        if (outPartsType) *outPartsType = 0;
        if (outSelector)  *outSelector  = 0;

        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!g_GetQuarkSystemTable) return false;
        }

        std::uint8_t pt  = 0;
        std::uint8_t sel = 0;
        __try
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (!qt) return false;
            auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
            if (!q98) return false;
            auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
            if (!state) return false;

            pt  = state[0xF8];
            sel = state[0xF9];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (outPartsType) *outPartsType = pt;
        if (outSelector)  *outSelector  = sel;
        return true;
    }
}
