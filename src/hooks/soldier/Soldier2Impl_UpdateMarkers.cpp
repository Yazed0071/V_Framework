#include "pch.h"
#include "Soldier2Impl_UpdateMarkers.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HeadMarkMarkerEvCall_SetIconSubType.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kSoldierIndexMask = 0x1FFu;
    constexpr std::uint32_t kSoldierSlotCount = 0x200u;
    constexpr std::uint32_t kSlotsPerWord     = 64u;
    constexpr std::uint32_t kNamedWordCount   = kSoldierSlotCount / kSlotsPerWord;
    constexpr std::uint16_t kSoldierIdBand    = 0x0400u;

    constexpr std::size_t kImpl_SoldierCount = 0x28;
    constexpr std::size_t kImpl_Presence     = 0x38;
    constexpr std::size_t kImpl_Life         = 0x58;
    constexpr std::size_t kImpl_States       = 0xB8;
    constexpr std::size_t kImpl_Markers      = 0xF8;
    constexpr std::size_t kImpl_Flags        = 0x130;
    constexpr std::size_t kImpl_Systems      = 0x670;

    constexpr std::size_t kQuark_Systems              = 0x98;
    constexpr std::size_t kSystems_Ui                 = 0x40;
    constexpr std::size_t kSystems_MotherBase         = 0x110;
    constexpr std::size_t kUi_HudSystem               = 0x18;
    constexpr std::size_t kHud_GetMarkerStaffNameInfo = 0x508;
    constexpr std::size_t kMotherBase_StaffController = 0xAC0;

    constexpr std::size_t kPresence_Active   = 0;
    constexpr std::size_t kPresence_Realized = 1;

    constexpr std::size_t  kMarkers_Records     = 0x10;
    constexpr std::size_t  kMarkers_FirstIndex  = 0x18;
    constexpr std::size_t  kMarkers_Count       = 0x1C;
    constexpr std::size_t  kMarker_Stride       = 0x30;
    constexpr std::size_t  kMarker_GameObjectId = 0x1C;
    constexpr std::size_t  kMarker_Flags        = 0x1F;
    constexpr std::uint8_t kMarkerFlag_Marked   = 0x02;

    constexpr std::size_t   kLife_Records   = 0x08;
    constexpr std::size_t   kLife_Stride    = 0x40;
    constexpr std::size_t   kLife_Status    = 0x12;
    constexpr std::size_t   kLife_Death     = 0x3E;
    constexpr std::uint16_t kLifeStatusMask = 0x0003u;
    constexpr std::uint8_t  kLifeDeath_Dead = 0x02;

    constexpr std::size_t   kList_Records             = 0x08;
    constexpr std::size_t   kList_Count               = 0x10;
    constexpr std::size_t   kFlag_Stride              = 0x28;
    constexpr std::size_t   kFlag_Modes               = 0x10;
    constexpr std::uint64_t kFlagMode_MotherBaseNames = 1ull << 45;
    constexpr std::size_t   kState_Stride             = 0x70;
    constexpr std::size_t   kState_StaffId            = 0x00;
    constexpr std::size_t   kState_FaceKind           = 0x34;
    constexpr std::size_t   kState_FaceId             = 0x4A;

    constexpr std::uint16_t kFaceIdLimit    = 900u;
    constexpr unsigned      kFaceIdShift    = 20u;
    constexpr std::uint32_t kFaceKindMask   = 0x700u;
    constexpr std::uint32_t kFaceKindNoFace = 0x300u;
    constexpr std::uint32_t kNoFaceField    = 0x27600000u;
    constexpr unsigned      kSeedShift      = 21u;
    constexpr std::uint64_t kSeedMask       = 0xFFFFF800ull;
    constexpr std::uint64_t kHeaderKeptBits = 0xC00FFFFFull;

    constexpr std::uint32_t kNameEntryCount    = 0x1E;
    constexpr std::size_t   kNameEntry_Stride  = 0x48;
    constexpr std::size_t   kNameEntry_StaffId = 0x08;
    constexpr std::uint16_t kNameEntryFree     = 0xFFFFu;

    constexpr std::uint16_t kSoldierIdBandMask    = 0xFE00u;
    constexpr std::size_t   kEvCall_StaffNames    = 0xE8;
    constexpr std::size_t   kMarker_NameText      = 0xE1;
    constexpr std::size_t   kMarker_NameTextSize  = 0x36;
    constexpr std::uint8_t  kUtf8ContinuationMask = 0xC0;
    constexpr std::uint8_t  kUtf8Continuation     = 0x80;

    constexpr std::size_t kPrologueCanaryOperand    = 0x10;
    constexpr std::size_t kPrologueCanaryOperandEnd = 0x14;

    const std::uint8_t kPrologue[] = {
        0x4C, 0x8B, 0xDC,
        0x57,
        0x41, 0x55,
        0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x33, 0xC4,
        0x48, 0x89, 0x84, 0x24, 0xE0, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0xB9, 0xF8, 0x00, 0x00, 0x00,
        0x4C, 0x8B, 0xE9,
        0x48, 0x89, 0x4C, 0x24, 0x28,
        0x48, 0x85, 0xFF,
        0x0F, 0x84, 0xFF, 0x09, 0x00, 0x00,
        0x8B, 0x47, 0x18,
        0x49, 0x89, 0x5B, 0x10,
        0x49, 0x89, 0x6B, 0x18,
        0x48, 0x8D, 0x1C, 0x40,
        0x48, 0x8B, 0x81, 0x70, 0x06, 0x00, 0x00,
        0x4D, 0x89, 0x63, 0xE8,
        0x48, 0x8B, 0x48, 0x40,
        0x48, 0xC1, 0xE3, 0x04,
        0x4D, 0x89, 0x73, 0xE0,
        0x48, 0x8B, 0x49, 0x18,
        0x48, 0x03, 0x5F, 0x10,
        0x48, 0x8B, 0x01,
        0xFF, 0x90, 0x08, 0x05, 0x00, 0x00,
        0xBA, 0x1E, 0x00, 0x00, 0x00,
    };

    using UpdateMarkers_t          = void(__fastcall*)(void* soldier2Impl);
    using GetMarkerStaffNameInfo_t = std::uint8_t*(__fastcall*)(void* hudSystem);
    using GetQuarkSystemTable_t    = std::uint8_t*(__fastcall*)();
    using LangIdToKey_t            = void*(__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t            = const char*(__fastcall*)(std::uint64_t langKey);

    struct SoldierLists
    {
        const std::uint8_t* activeBits;
        const std::uint8_t* realizedBits;
        const std::uint8_t* markers;
        const std::uint8_t* flags;
        const std::uint8_t* states;
        const std::uint8_t* lifeRecords;
        std::uint8_t*       staffNames;
        std::uint32_t       soldierCount;
        std::uint32_t       markerCount;
        std::uint32_t       flagCount;
        std::uint32_t       stateCount;
    };

    enum class NameEntry
    {
        Written,
        AlreadyPresent,
        NoFreeEntry,
        Unwritable,
    };

    enum class LifeStatus
    {
        Normal,
        Neutralized,
        Unreadable,
    };

    std::atomic<std::uint64_t> g_NamedSoldiers[kNamedWordCount]{};
    std::atomic<std::uint64_t> g_NeutralizedNamedSoldiers[kNamedWordCount]{};
    std::atomic<std::uint64_t> g_NameLangKeys[kSoldierSlotCount]{};

    UpdateMarkers_t       g_OrigUpdateMarkers       = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable     = nullptr;
    void*                 g_UpdateMarkersTarget     = nullptr;
    bool                  g_NameListRefusalLogged   = false;
    bool                  g_NoStaffControllerLogged = false;
    bool                  g_LifeUnreadableLogged    = false;

    bool g_LangLookupMissingLogged   = false;
    bool g_NameTextHookMissingLogged = false;
    bool g_LangTextMissingLogged     = false;
    bool g_NameTextUnwritableLogged  = false;

    template <typename T>
    T Read(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(base + offset);
    }

    std::uint64_t NamedBit(std::uint32_t soldierIndex)
    {
        return 1ull << (soldierIndex % kSlotsPerWord);
    }

    std::atomic<std::uint64_t>& NamedWord(std::uint32_t soldierIndex)
    {
        return g_NamedSoldiers[soldierIndex / kSlotsPerWord];
    }

    bool IsSoldierNamed(std::uint32_t soldierIndex)
    {
        return (NamedWord(soldierIndex).load(std::memory_order_relaxed) & NamedBit(soldierIndex)) != 0;
    }

    std::atomic<std::uint64_t>& NeutralizedNamedWord(std::uint32_t soldierIndex)
    {
        return g_NeutralizedNamedSoldiers[soldierIndex / kSlotsPerWord];
    }

    bool IsNameShownWhenNeutralized(std::uint32_t soldierIndex)
    {
        return (NeutralizedNamedWord(soldierIndex).load(std::memory_order_relaxed) & NamedBit(soldierIndex)) != 0;
    }

    bool AnySoldierNamed()
    {
        for (const std::atomic<std::uint64_t>& word : g_NamedSoldiers)
        {
            if (word.load(std::memory_order_relaxed) != 0)
                return true;
        }
        return false;
    }

    std::uint16_t SoldierGameObjectId(std::uint32_t soldierIndex)
    {
        return static_cast<std::uint16_t>(kSoldierIdBand | soldierIndex);
    }

    bool IsBitSet(const std::uint8_t* bits, std::uint32_t index)
    {
        return (bits[index >> 3] & (1u << (index & 7u))) != 0;
    }

    std::uint64_t WithFaceField(std::uint64_t staffId, std::uint32_t faceField)
    {
        const std::uint64_t seed   = ((staffId >> kSeedShift) & kSeedMask) << kSeedShift;
        const std::uint64_t header = staffId & kHeaderKeptBits;
        return seed | header | faceField;
    }

    std::uint64_t MarkerStaffId(std::uint64_t staffId, std::uint16_t faceId, std::uint32_t faceKind)
    {
        if (faceId < kFaceIdLimit)
            staffId = WithFaceField(staffId, static_cast<std::uint32_t>(faceId) << kFaceIdShift);
        if ((faceKind & kFaceKindMask) == kFaceKindNoFace)
            staffId = WithFaceField(staffId, kNoFaceField);
        return staffId;
    }

    std::uint8_t* MarkerStaffNameInfo(const std::uint8_t* impl)
    {
        const auto systems = Read<const std::uint8_t*>(impl, kImpl_Systems);
        if (!systems)
            return nullptr;

        const auto ui = Read<const std::uint8_t*>(systems, kSystems_Ui);
        if (!ui)
            return nullptr;

        const auto hudSystem = Read<std::uint8_t*>(ui, kUi_HudSystem);
        if (!hudSystem)
            return nullptr;

        const auto vtbl = Read<void* const*>(hudSystem, 0);
        if (!vtbl)
            return nullptr;

        const auto getInfo = reinterpret_cast<GetMarkerStaffNameInfo_t>(
            vtbl[kHud_GetMarkerStaffNameInfo / sizeof(void*)]);
        return getInfo ? getInfo(hudSystem) : nullptr;
    }

    bool ReadSoldierListsSeh(const std::uint8_t* impl, SoldierLists& out)
    {
        __try
        {
            const auto markerBlock = Read<const std::uint8_t*>(impl, kImpl_Markers);
            const auto presence    = Read<const std::uint8_t* const*>(impl, kImpl_Presence);
            const auto flagList    = Read<const std::uint8_t*>(impl, kImpl_Flags);
            const auto stateList   = Read<const std::uint8_t*>(impl, kImpl_States);
            const auto lifeBlock   = Read<const std::uint8_t*>(impl, kImpl_Life);
            if (!markerBlock || !presence || !flagList || !stateList)
                return false;

            const auto markerRecords = Read<const std::uint8_t*>(markerBlock, kMarkers_Records);
            if (!markerRecords)
                return false;

            const std::size_t firstMarker = Read<std::uint32_t>(markerBlock, kMarkers_FirstIndex);

            out.activeBits   = presence[kPresence_Active];
            out.realizedBits = presence[kPresence_Realized];
            out.markers      = markerRecords + firstMarker * kMarker_Stride;
            out.flags        = Read<const std::uint8_t*>(flagList, kList_Records);
            out.states       = Read<const std::uint8_t*>(stateList, kList_Records);
            out.lifeRecords  = lifeBlock ? Read<const std::uint8_t*>(lifeBlock, kLife_Records) : nullptr;
            out.staffNames   = MarkerStaffNameInfo(impl);
            out.soldierCount = Read<std::uint32_t>(impl, kImpl_SoldierCount);
            out.markerCount  = Read<std::uint32_t>(markerBlock, kMarkers_Count);
            out.flagCount    = Read<std::uint32_t>(flagList, kList_Count);
            out.stateCount   = Read<std::uint32_t>(stateList, kList_Count);

            return out.activeBits && out.realizedBits && out.flags && out.states && out.staffNames;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t SoldiersToScan(const SoldierLists& lists)
    {
        return (std::min)({ lists.soldierCount, lists.markerCount, lists.flagCount,
                            lists.stateCount, kSoldierSlotCount });
    }

    bool ReadMarkedSoldierStaffIdSeh(const SoldierLists& lists, std::uint32_t index,
                                     std::uint64_t& outStaffId)
    {
        __try
        {
            if (!IsBitSet(lists.activeBits, index) || !IsBitSet(lists.realizedBits, index))
                return false;

            const std::uint8_t* marker = lists.markers + index * kMarker_Stride;
            if ((Read<std::uint8_t>(marker, kMarker_Flags) & kMarkerFlag_Marked) == 0)
                return false;
            if (Read<std::uint16_t>(marker, kMarker_GameObjectId) != SoldierGameObjectId(index))
                return false;

            const std::uint8_t* flag = lists.flags + index * kFlag_Stride;
            if ((Read<std::uint64_t>(flag, kFlag_Modes) & kFlagMode_MotherBaseNames) != 0)
                return false;

            const std::uint8_t* state = lists.states + index * kState_Stride;
            outStaffId = MarkerStaffId(Read<std::uint64_t>(state, kState_StaffId),
                                       Read<std::uint16_t>(state, kState_FaceId),
                                       Read<std::uint32_t>(state, kState_FaceKind));
            return outStaffId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    LifeStatus ReadLifeStatusSeh(const SoldierLists& lists, std::uint32_t index)
    {
        __try
        {
            if (!lists.lifeRecords)
                return LifeStatus::Unreadable;

            const std::uint8_t* life = lists.lifeRecords + index * kLife_Stride;
            if (Read<std::uint8_t>(life, kLife_Death) == kLifeDeath_Dead)
                return LifeStatus::Neutralized;
            if ((Read<std::uint16_t>(life, kLife_Status) & kLifeStatusMask) != 0)
                return LifeStatus::Neutralized;
            return LifeStatus::Normal;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return LifeStatus::Unreadable;
        }
    }

    NameEntry AddStaffNameSeh(std::uint8_t* staffNames, std::uint16_t gameObjectId,
                              std::uint64_t staffId)
    {
        __try
        {
            std::uint8_t* freeEntry = nullptr;
            for (std::uint32_t i = 0; i < kNameEntryCount; ++i)
            {
                std::uint8_t* entry = staffNames + i * kNameEntry_Stride;
                const std::uint16_t entryId = Read<std::uint16_t>(entry, 0);
                if (entryId == gameObjectId)
                    return NameEntry::AlreadyPresent;
                if (entryId == kNameEntryFree && !freeEntry)
                    freeEntry = entry;
            }

            if (!freeEntry)
                return NameEntry::NoFreeEntry;

            *reinterpret_cast<std::uint64_t*>(freeEntry + kNameEntry_StaffId) = staffId;
            *reinterpret_cast<std::uint16_t*>(freeEntry) = gameObjectId;
            return NameEntry::Written;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return NameEntry::Unwritable;
        }
    }

    bool StaffControllerPresentSeh()
    {
        __try
        {
            const auto quark = g_GetQuarkSystemTable();
            if (!quark)
                return false;

            const auto systems = Read<const std::uint8_t*>(quark, kQuark_Systems);
            if (!systems)
                return false;

            const auto motherBase = Read<const std::uint8_t*>(systems, kSystems_MotherBase);
            return motherBase && Read<const void*>(motherBase, kMotherBase_StaffController) != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WarnNameListRefusedOnce(NameEntry entry, std::uint32_t index)
    {
        if (g_NameListRefusalLogged)
            return;
        g_NameListRefusalLogged = true;

        if (entry == NameEntry::NoFreeEntry)
            LogWarn("ShowStaffName: head-mark name list full (30 names), soldier %u onward keeps the distance text", index);
        else
            LogWarn("ShowStaffName: head-mark name list unwritable, named soldiers keep the distance text");
    }

    void WarnNoStaffControllerOnce()
    {
        if (g_NoStaffControllerLogged)
            return;
        g_NoStaffControllerLogged = true;
        LogWarn("ShowStaffName: StaffController missing, named soldiers keep the distance text");
    }

    void WarnLifeStatusUnreadableOnce()
    {
        if (g_LifeUnreadableLogged)
            return;
        g_LifeUnreadableLogged = true;
        LogWarn("ShowStaffName: soldier life status unreadable, named soldiers without showOnNeutralized keep the distance text");
    }

    bool IsNameAllowedByLifeStatus(const SoldierLists& lists, std::uint32_t index)
    {
        if (IsNameShownWhenNeutralized(index))
            return true;

        const LifeStatus status = ReadLifeStatusSeh(lists, index);
        if (status == LifeStatus::Unreadable)
            WarnLifeStatusUnreadableOnce();
        return status == LifeStatus::Normal;
    }

    void PublishNamedSoldiers(const std::uint8_t* impl)
    {
        SoldierLists lists{};
        if (!ReadSoldierListsSeh(impl, lists))
            return;

        if (!StaffControllerPresentSeh())
        {
            WarnNoStaffControllerOnce();
            return;
        }

        const std::uint32_t count = SoldiersToScan(lists);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (!IsSoldierNamed(index))
                continue;

            std::uint64_t staffId = 0;
            if (!ReadMarkedSoldierStaffIdSeh(lists, index, staffId))
                continue;

            if (!IsNameAllowedByLifeStatus(lists, index))
                continue;

            const NameEntry entry = AddStaffNameSeh(lists.staffNames, SoldierGameObjectId(index), staffId);
            if (entry == NameEntry::NoFreeEntry || entry == NameEntry::Unwritable)
            {
                WarnNameListRefusedOnce(entry, index);
                return;
            }
        }
    }

    void __fastcall hkUpdateMarkers(void* soldier2Impl)
    {
        if (g_OrigUpdateMarkers)
            g_OrigUpdateMarkers(soldier2Impl);

        if (!soldier2Impl || !AnySoldierNamed() || MissionCodeGuard::ShouldBypassHooks())
            return;

        PublishNamedSoldiers(static_cast<const std::uint8_t*>(soldier2Impl));
    }

    bool IsCanaryOperand(std::size_t offset)
    {
        return offset >= kPrologueCanaryOperand && offset < kPrologueCanaryOperandEnd;
    }

    bool PrologueMatchesSeh(const std::uint8_t* entry)
    {
        __try
        {
            for (std::size_t i = 0; i < sizeof(kPrologue); ++i)
            {
                if (!IsCanaryOperand(i) && entry[i] != kPrologue[i])
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool LangIdToKeySeh(LangIdToKey_t toKey, const char* langId, std::uint64_t& outKey)
    {
        __try
        {
            toKey(&outKey, langId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* GetLangTextSeh(GetLangText_t getText, std::uint64_t langKey)
    {
        __try
        {
            return getText(langKey);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void WarnLangLookupMissingOnce(const char* langId)
    {
        if (g_LangLookupMissingLogged)
            return;
        g_LangLookupMissingLogged = true;
        LogWarn("ShowStaffName: lang text lookup address missing for this build, langId \"%s\" ignored, head-marks keep the real staff name", langId);
    }

    void WarnNameTextHookMissingOnce(const char* langId)
    {
        if (g_NameTextHookMissingLogged)
            return;
        g_NameTextHookMissingLogged = true;
        LogWarn("ShowStaffName: head-mark SetIconSubType hook not installed, langId \"%s\" ignored, head-marks keep the real staff name", langId);
    }

    void WarnLangTextMissingOnce(std::uint16_t markerId)
    {
        if (g_LangTextMissingLogged)
            return;
        g_LangTextMissingLogged = true;
        LogWarn("ShowStaffName: langId of soldier 0x%04X has no text in the loaded lang files, his head-mark keeps the real staff name",
                static_cast<unsigned>(markerId));
    }

    void WarnNameTextUnwritableOnce()
    {
        if (g_NameTextUnwritableLogged)
            return;
        g_NameTextUnwritableLogged = true;
        LogWarn("ShowStaffName: head-mark name text unwritable, soldiers with a langId keep the real staff name");
    }

    std::uint64_t NameLangKey(const char* langId)
    {
        if (!langId || langId[0] == '\0')
            return 0;

        const auto toKey = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
        if (!toKey || !gAddr.Ui_GetLangText)
        {
            WarnLangLookupMissingOnce(langId);
            return 0;
        }

        if (!HeadMarkMarkerEvCall_SetIconSubType_Installed())
        {
            WarnNameTextHookMissingOnce(langId);
            return 0;
        }

        std::uint64_t langKey = 0;
        if (!LangIdToKeySeh(toKey, langId, langKey) || langKey == 0)
        {
            LogWarn("ShowStaffName: langId \"%s\" could not be hashed, the head-mark keeps the real staff name", langId);
            return 0;
        }
        return langKey;
    }

    const char* NameLangText(std::uint64_t langKey)
    {
        const auto getText = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
        if (!getText)
            return nullptr;

        const char* text = GetLangTextSeh(getText, langKey);
        return text && text[0] != '\0' ? text : nullptr;
    }

    bool IsStaffNameShownSeh(const std::uint8_t* headMarkEvCall, std::uint16_t markerId)
    {
        __try
        {
            const auto staffNames = Read<const std::uint8_t*>(headMarkEvCall, kEvCall_StaffNames);
            if (!staffNames)
                return false;

            for (std::uint32_t i = 0; i < kNameEntryCount; ++i)
            {
                const std::uint8_t* entry = staffNames + i * kNameEntry_Stride;
                if (Read<std::uint16_t>(entry, 0) == markerId)
                    return Read<std::uint64_t>(entry, kNameEntry_StaffId) != 0;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::size_t Utf8PrefixLength(const char* text, std::size_t limit)
    {
        std::size_t length = 0;
        while (length < limit && text[length] != '\0')
            ++length;

        while (length > 0 && text[length] != '\0' &&
               (static_cast<std::uint8_t>(text[length]) & kUtf8ContinuationMask) == kUtf8Continuation)
            --length;
        return length;
    }

    bool WriteMarkerNameTextSeh(std::uint8_t* marker, const char* text)
    {
        __try
        {
            char* buffer = reinterpret_cast<char*>(marker + kMarker_NameText);
            const std::size_t length = Utf8PrefixLength(text, kMarker_NameTextSize - 1);
            std::memcpy(buffer, text, length);
            buffer[length] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void Set_HeadMarkStaffName(std::uint32_t gameObjectId, bool enable, bool showOnNeutralized,
                           const char* langId)
{
    const std::uint32_t soldierIndex = gameObjectId & kSoldierIndexMask;
    g_NameLangKeys[soldierIndex].store(enable ? NameLangKey(langId) : 0, std::memory_order_relaxed);

    if (enable && showOnNeutralized)
        NeutralizedNamedWord(soldierIndex).fetch_or(NamedBit(soldierIndex), std::memory_order_relaxed);
    else
        NeutralizedNamedWord(soldierIndex).fetch_and(~NamedBit(soldierIndex), std::memory_order_relaxed);

    if (enable)
        NamedWord(soldierIndex).fetch_or(NamedBit(soldierIndex), std::memory_order_relaxed);
    else
        NamedWord(soldierIndex).fetch_and(~NamedBit(soldierIndex), std::memory_order_relaxed);
}

void Clear_AllHeadMarkStaffNames()
{
    for (std::atomic<std::uint64_t>& word : g_NamedSoldiers)
        word.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& word : g_NeutralizedNamedSoldiers)
        word.store(0, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>& langKey : g_NameLangKeys)
        langKey.store(0, std::memory_order_relaxed);
}

void Apply_HeadMarkStaffNameLangText(void* headMarkEvCall, void* marker, std::uint16_t markerId)
{
    if (!headMarkEvCall || !marker || (markerId & kSoldierIdBandMask) != kSoldierIdBand)
        return;

    const std::uint64_t langKey =
        g_NameLangKeys[markerId & kSoldierIndexMask].load(std::memory_order_relaxed);
    if (langKey == 0 || !IsStaffNameShownSeh(static_cast<const std::uint8_t*>(headMarkEvCall), markerId))
        return;

    const char* text = NameLangText(langKey);
    if (!text)
    {
        WarnLangTextMissingOnce(markerId);
        return;
    }

    if (!WriteMarkerNameTextSeh(static_cast<std::uint8_t*>(marker), text))
        WarnNameTextUnwritableOnce();
}

bool Install_Soldier2Impl_UpdateMarkers_Hook()
{
    if (g_UpdateMarkersTarget)
        return true;

    auto entry = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Soldier2Impl_UpdateMarkers));
    if (!entry)
    {
        LogError("ShowStaffName: UpdateMarkers address missing for this build, ShowStaffName has no effect");
        return false;
    }

    if (!PrologueMatchesSeh(entry))
    {
        LogError("ShowStaffName: UpdateMarkers prologue mismatch, ShowStaffName has no effect");
        return false;
    }

    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
        ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!g_GetQuarkSystemTable)
    {
        LogError("ShowStaffName: GetQuarkSystemTable address missing, ShowStaffName has no effect");
        return false;
    }

    if (!CreateAndEnableHook(entry, reinterpret_cast<void*>(&hkUpdateMarkers),
                             reinterpret_cast<void**>(&g_OrigUpdateMarkers)))
    {
        LogError("ShowStaffName: UpdateMarkers hook failed, ShowStaffName has no effect");
        return false;
    }

    g_UpdateMarkersTarget = entry;
    return true;
}

bool Uninstall_Soldier2Impl_UpdateMarkers_Hook()
{
    if (g_UpdateMarkersTarget)
        DisableAndRemoveHook(g_UpdateMarkersTarget);

    g_UpdateMarkersTarget = nullptr;
    g_OrigUpdateMarkers   = nullptr;
    g_GetQuarkSystemTable = nullptr;
    Clear_AllHeadMarkStaffNames();
    return true;
}
