#include "pch.h"
#include "EquipDevelop_SetEquipUndeveloped.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaApi.h"
#include "V_FrameWorkState.h"
#include "EquipDevelop_AddToEquipDevelopTable.h"
#include "../outfit/EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"

namespace
{
    constexpr std::size_t kQuark_AppOffset     = 0x98;
    constexpr std::size_t kApp_Field110Offset  = 0x110;
    constexpr std::size_t kField110_CtrlOffset = 0xAC8;

    constexpr std::uint16_t kInvalidDevelopIndex = 0x400;

    constexpr std::size_t kDevelopedBitsOffset = 0x1e008;
    constexpr std::uint8_t kNewBitUndeveloped = 0x2;
    constexpr std::uint8_t kNewBitDeveloped   = 0x8;
    constexpr std::uint8_t kAllNewBits        = kNewBitUndeveloped | kNewBitDeveloped;

    using GetQuarkSystemTable_t  = void* (__fastcall*)();
    using GetEquipDevelopIndex_t = std::uint16_t (__fastcall*)(void* controller, std::uint32_t developId);
    using SetEquipUndeveloped_t  = void (__fastcall*)(void* controller, std::uint16_t index, char notify);
    using SetEquipDeveloped_t    = void (__fastcall*)(void* controller, std::uint16_t index);
    using SetEnableDevelop_t     = void (__fastcall*)(void* controller, std::uint16_t index, char enable);

    static SetEquipDeveloped_t   g_OrigSetEquipDeveloped   = nullptr;
    static SetEquipUndeveloped_t g_OrigSetEquipUndeveloped = nullptr;
    static bool g_DevelopSyncArmed = false;

    std::mutex                                       g_DevelopParentMutex;
    std::unordered_map<std::uint32_t, std::uint32_t> g_DevelopParent;

    static GetEquipDevelopIndex_t NativeGetIndex()
    {
        return reinterpret_cast<GetEquipDevelopIndex_t>(
            ResolveGameAddress(gAddr.EquipDevelopCtrl_GetEquipDevelopIndex));
    }
    static SetEquipDeveloped_t NativeDevelop()
    {
        return g_OrigSetEquipDeveloped
            ? g_OrigSetEquipDeveloped
            : reinterpret_cast<SetEquipDeveloped_t>(
                  ResolveGameAddress(gAddr.EquipDevelopCtrl_SetEquipDeveloped));
    }
    static SetEquipUndeveloped_t NativeUndevelop()
    {
        return g_OrigSetEquipUndeveloped
            ? g_OrigSetEquipUndeveloped
            : reinterpret_cast<SetEquipUndeveloped_t>(
                  ResolveGameAddress(gAddr.EquipDevelopCtrl_SetEquipUndeveloped));
    }
    static SetEnableDevelop_t NativeSetEnable()
    {
        return reinterpret_cast<SetEnableDevelop_t>(
            ResolveGameAddress(gAddr.EquipDevelopCtrl_SetEnableDevelop));
    }

    static void AnnounceNewlyDevelopable(void* controller, GetEquipDevelopIndex_t getIndex);

    using HudGetInstance_t   = void* (__fastcall*)();
    using LangIdToKey_t      = void* (__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t      = const char* (__fastcall*)(std::uint64_t key);
    using GetAnnounceLogSE_t = std::uint32_t (__fastcall*)(void* cdm, std::uint32_t keyLow);
    using AnnounceLogView_t  = void (__fastcall*)(void* cdm, const char* text,
                                                  std::uint8_t type, std::uint8_t se,
                                                  bool important);

    constexpr const char* kDevReqMetLangId = "announce_dev_requirements_met";
    constexpr std::uint8_t kDevReqMetType  = 5;

    static void* SafeHudGetInstance(HudGetInstance_t fn)
    {
        __try { return fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static const char* SafeGetLangText(GetLangText_t fn, std::uint64_t key)
    {
        __try { return fn(key); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static bool SafeLangIdToKey(LangIdToKey_t fn, const char* langId, std::uint64_t* out)
    {
        __try { fn(out, langId); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static std::uint8_t SafeGetAnnounceLogSE(GetAnnounceLogSE_t fn, void* cdm, std::uint32_t keyLow)
    {
        __try { return static_cast<std::uint8_t>(fn(cdm, keyLow)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    static bool SafeAnnounceLogView(AnnounceLogView_t fn, void* cdm, const char* text,
                                    std::uint8_t type, std::uint8_t se, bool important)
    {
        __try { fn(cdm, text, type, se, important); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void* ResolveHudCdm()
    {
        auto getInstance = reinterpret_cast<HudGetInstance_t>(
            ResolveGameAddress(gAddr.HudCommonDataManager_GetInstance));
        return getInstance ? SafeHudGetInstance(getInstance) : nullptr;
    }

    static std::uint64_t LangKeyFromString(const char* langId)
    {
        auto toKey = reinterpret_cast<LangIdToKey_t>(
            ResolveGameAddress(gAddr.Ui_LangIdToKey));
        if (!toKey || !langId)
            return 0;
        std::uint64_t key = 0;
        return SafeLangIdToKey(toKey, langId, &key) ? key : 0;
    }

    static const char* LangText(std::uint64_t key)
    {
        if (!key)
            return nullptr;
        auto getLangText = reinterpret_cast<GetLangText_t>(
            ResolveGameAddress(gAddr.Ui_GetLangText));
        return getLangText ? SafeGetLangText(getLangText, key) : nullptr;
    }

    static std::string SubstituteName(const char* fmt, const char* name)
    {
        std::string out;
        bool done = false;
        for (const char* p = fmt; *p; )
        {
            if (!done && p[0] == '%' && p[1] == 's')
            {
                if (name)
                    out += name;
                p += 2;
                done = true;
            }
            else
            {
                out += *p++;
            }
        }
        return out;
    }

    static std::string ResolveDevelopDisplayName(std::int32_t developId)
    {
        bool hasHash = false;
        std::uint32_t hash = 0;
        std::string langIdStr;
        if (!EquipDevelopAdd::GetDevelopNameLangId(developId, hasHash, hash, langIdStr))
            return std::string();

        const std::uint64_t key = hasHash
            ? static_cast<std::uint64_t>(hash)
            : LangKeyFromString(langIdStr.c_str());

        if (const char* t = LangText(key))
            if (*t)
                return std::string(t);

        return langIdStr;
    }

    static void FireDevRequirementsMet(void* cdm, std::int32_t developId)
    {
        if (!cdm)
            return;

        auto announce = reinterpret_cast<AnnounceLogView_t>(
            ResolveGameAddress(gAddr.HudCommonDataManager_AnnounceLogView));
        if (!announce)
            return;

        const std::uint64_t fmtKey = LangKeyFromString(kDevReqMetLangId);
        const char* fmt = LangText(fmtKey);
        if (!fmt || !*fmt)
            return;

        const std::string name = ResolveDevelopDisplayName(developId);
        if (name.empty())
            return;

        const std::string text = SubstituteName(fmt, name.c_str());

        std::uint8_t se = 0;
        if (auto getSE = reinterpret_cast<GetAnnounceLogSE_t>(
                ResolveGameAddress(gAddr.Hud_GetAnnounceLogSE)))
            se = SafeGetAnnounceLogSE(getSE, cdm, static_cast<std::uint32_t>(fmtKey));

        const bool ok =
            SafeAnnounceLogView(announce, cdm, text.c_str(), kDevReqMetType, se, true);

        if (!ok)
            Log("[EquipDevelop] announce FAILED: \"%s\" (developId=%d)\n",
                text.c_str(), developId);
    }

    static std::uint16_t SafeGetIndex(GetEquipDevelopIndex_t getIndex,
                                      void* controller, std::uint32_t developId)
    {
        __try { return getIndex(controller, developId); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return kInvalidDevelopIndex; }
    }

    static std::uint8_t SafeReadDevRecordByte(void* controller,
                                              std::uint16_t index,
                                              std::uint32_t byteOffset)
    {
        __try
        {
            return *(reinterpret_cast<const std::uint8_t*>(controller)
                     + static_cast<std::size_t>(index) * 0x68 + byteOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xEE; }
    }

    static void SyncFlagFromNativeDevelop(void* controller, std::uint16_t index, bool developed)
    {
        if (!controller || index >= kInvalidDevelopIndex)
            return;
        auto getIndex = NativeGetIndex();
        if (!getIndex)
            return;

        std::vector<std::int32_t> ids;
        V_FrameWorkState::ForEachManagedDevelop(
            [&](std::int32_t id, bool, bool) { ids.push_back(id); });

        for (std::int32_t id : ids)
        {
            if (SafeGetIndex(getIndex, controller, static_cast<std::uint32_t>(id)) == index)
            {
                V_FrameWorkState::SetDevelopedByDevelopId(id, developed);
                return;
            }
        }
    }

    static bool IsDevelopIdDeveloped(void* controller, GetEquipDevelopIndex_t getIndex,
                                     std::uint32_t developId)
    {
        if (developId == 0)
            return false;
        if (V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
            return V_FrameWorkState::GetDevelopedByDevelopId(static_cast<std::int32_t>(developId));
        if (!controller || !getIndex)
            return false;
        __try
        {
            const std::uint16_t index = getIndex(controller, developId);
            if (index >= kInvalidDevelopIndex)
                return false;
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (!bits)
                return false;
            return (bits[index] & 1u) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void SafeSetEnableForDevelopId(GetEquipDevelopIndex_t getIndex,
                                          SetEnableDevelop_t setEnable,
                                          void* controller,
                                          std::uint32_t childDevelopId,
                                          char enable)
    {
        __try
        {
            const std::uint16_t childIndex = getIndex(controller, childDevelopId);
            if (childIndex < kInvalidDevelopIndex)
                setEnable(controller, childIndex, enable);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void SyncDevelopPrereqGates(void* controller, GetEquipDevelopIndex_t getIndex)
    {
        if (!controller || !getIndex)
            return;
        auto setEnable = NativeSetEnable();
        if (!setEnable)
            return;

        std::unordered_map<std::uint32_t, std::uint32_t> parents;
        {
            std::lock_guard<std::mutex> lock(g_DevelopParentMutex);
            parents = g_DevelopParent;
        }

        std::vector<std::uint32_t> managedIds;
        V_FrameWorkState::ForEachManagedDevelop(
            [&](std::int32_t id, bool, bool)
            {
                managedIds.push_back(static_cast<std::uint32_t>(id));
            });

        for (std::uint32_t childDevelopId : managedIds)
        {
            const auto it = parents.find(childDevelopId);
            const bool enable = (it == parents.end() || it->second == 0)
                ? true
                : IsDevelopIdDeveloped(controller, getIndex, it->second);

            SafeSetEnableForDevelopId(getIndex, setEnable, controller,
                                      childDevelopId,
                                      enable ? static_cast<char>(1)
                                             : static_cast<char>(0));
        }
    }

    static void __fastcall hkSetEquipDeveloped(void* controller, std::uint16_t index)
    {
        if (g_OrigSetEquipDeveloped)
            g_OrigSetEquipDeveloped(controller, index);
        if (g_DevelopSyncArmed)
        {
            SyncFlagFromNativeDevelop(controller, index, true);
            SyncDevelopPrereqGates(controller, NativeGetIndex());
            AnnounceNewlyDevelopable(controller, NativeGetIndex());
        }
    }

    static void __fastcall hkSetEquipUndeveloped(void* controller, std::uint16_t index, char notify)
    {
        if (g_OrigSetEquipUndeveloped)
            g_OrigSetEquipUndeveloped(controller, index, notify);
        if (g_DevelopSyncArmed)
        {
            SyncFlagFromNativeDevelop(controller, index, false);
            SyncDevelopPrereqGates(controller, NativeGetIndex());
        }
    }

    static void* ResolveController()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;

        __try
        {
            std::uint8_t* quark = static_cast<std::uint8_t*>(getQuark());
            if (!quark) return nullptr;
            void* app = *reinterpret_cast<void**>(quark + kQuark_AppOffset);
            if (!app) return nullptr;
            void* field110 = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(app) + kApp_Field110Offset);
            if (!field110) return nullptr;
            return *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(field110) + kField110_CtrlOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool CallNativeUndevelop(void* controller, std::uint32_t developId,
                                    std::uint16_t& outIndex)
    {
        outIndex = kInvalidDevelopIndex;

        auto getIndex = NativeGetIndex();
        auto setUndeveloped = NativeUndevelop();
        if (!getIndex || !setUndeveloped)
        {
            Log("[EquipUndevelop] native fn address not set for this build - skipped\n");
            return false;
        }

        __try
        {
            const std::uint16_t index = getIndex(controller, developId);
            outIndex = index;
            if (index >= kInvalidDevelopIndex)
                return false;

            setUndeveloped(controller, index, 1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool PokeDevelopedBit(void* controller, std::uint32_t developId, bool developed)
    {
        auto getIndex = reinterpret_cast<GetEquipDevelopIndex_t>(
            ResolveGameAddress(gAddr.EquipDevelopCtrl_GetEquipDevelopIndex));
        if (!getIndex)
            return false;

        __try
        {
            const std::uint16_t index = getIndex(controller, developId);
            if (index >= kInvalidDevelopIndex)
                return false;
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (!bits)
                return false;
            if (developed)
                bits[index] |= 1u;
            else
                bits[index] &= static_cast<std::uint8_t>(~1u);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool PokeNewBit(void* controller, GetEquipDevelopIndex_t getIndex,
                           std::uint32_t developId, bool isNew, bool developed)
    {
        if (!getIndex)
            return false;
        __try
        {
            const std::uint16_t index = getIndex(controller, developId);
            if (index >= kInvalidDevelopIndex)
                return false;
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (!bits)
                return false;
            if (isNew)
            {
                const std::uint8_t show  = developed ? kNewBitDeveloped : kNewBitUndeveloped;
                const std::uint8_t other = developed ? kNewBitUndeveloped : kNewBitDeveloped;
                bits[index] |= show;
                bits[index] &= static_cast<std::uint8_t>(~other);
            }
            else
            {
                bits[index] &= static_cast<std::uint8_t>(~kAllNewBits);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    using IsEquipDevelopable_t = std::uint8_t (__fastcall*)(void* controller, std::uint16_t index);

    static bool IsDevelopRequirementsMet(void* controller, std::uint16_t index)
    {
        if (!controller || index >= kInvalidDevelopIndex)
            return false;
        auto fn = reinterpret_cast<IsEquipDevelopable_t>(
            ResolveGameAddress(gAddr.EquipDevCtrl_IsEquipDevelopable));
        if (!fn)
            return false;
        __try { return fn(controller, index) != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void AnnounceNewlyDevelopable(void* controller, GetEquipDevelopIndex_t getIndex)
    {
        if (!controller || !getIndex)
            return;
        void* hudCdm = ResolveHudCdm();
        if (!hudCdm)
            return;

        std::vector<std::int32_t> candidates;
        V_FrameWorkState::ForEachManagedDevelop(
            [&](std::int32_t developId, bool developed, bool isNew)
            {
                if (isNew && !developed)
                    candidates.push_back(developId);
            });

        for (std::int32_t developId : candidates)
        {
            const std::uint16_t index =
                SafeGetIndex(getIndex, controller, static_cast<std::uint32_t>(developId));
            if (index >= kInvalidDevelopIndex)
                continue;
            if (outfit::IsDevelopHidden(index))
                continue;
            if (!IsDevelopRequirementsMet(controller, index))
                continue;
            FireDevRequirementsMet(hudCdm, developId);
            V_FrameWorkState::SetNewByDevelopId(developId, false);
        }
    }

    static int ReconcileOneManaged(
        void* controller,
        GetEquipDevelopIndex_t getIndex,
        SetEquipUndeveloped_t setUndeveloped,
        std::uint32_t developId,
        bool wantDeveloped,
        std::uint16_t* outIndex,
        int* outBitBefore)
    {
        *outIndex = 0xFFFF;
        *outBitBefore = -1;
        __try
        {
            const std::uint16_t index = getIndex(controller, developId);
            *outIndex = index;
            if (index >= kInvalidDevelopIndex)
                return 0;
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (!bits)
                return 0;
            const int bit = (bits[index] & 1u) ? 1 : 0;
            *outBitBefore = bit;

            if (wantDeveloped)
            {
                if (bit == 0)
                {
                    bits[index] |= 1u;
                    return 3;
                }
                return 1;
            }

            if (bit == 1)
            {
                if (setUndeveloped)
                    setUndeveloped(controller, index,  1);
                else
                    bits[index] &= static_cast<std::uint8_t>(~1u);
                return 2;
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }
}

bool EquipDevelop_UndevelopByDevelopId(std::uint32_t developId)
{
    if (developId == 0)
        return false;

    void* controller = ResolveController();
    if (!controller)
    {
        Log("[EquipUndevelop] controller not resolved (game not booted?); developId=%u skipped\n",
            developId);
        return false;
    }

    if (V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
    {
        V_FrameWorkState::SetDevelopedByDevelopId(static_cast<std::int32_t>(developId), false);
        return PokeDevelopedBit(controller, developId, false);
    }

    std::uint16_t index = kInvalidDevelopIndex;
    const bool ok = CallNativeUndevelop(controller, developId, index);
    if (index >= kInvalidDevelopIndex)
        return false;

    if (!ok)
        Log("[EquipUndevelop] developId=%u index=%u -> FAILED (native, persistent)\n",
            developId, index);
    return ok;
}

bool EquipDevelop_DevelopByDevelopId(std::uint32_t developId)
{
    if (developId == 0)
        return false;

    void* controller = ResolveController();
    if (!controller)
    {
        Log("[EquipDevelop] controller not resolved (game not booted?); developId=%u skipped\n",
            developId);
        return false;
    }

    if (V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
    {
        V_FrameWorkState::SetDevelopedByDevelopId(static_cast<std::int32_t>(developId), true);
        return PokeDevelopedBit(controller, developId, true);
    }

    auto getIndex = NativeGetIndex();
    auto setDeveloped = NativeDevelop();
    if (!getIndex || !setDeveloped)
    {
        Log("[EquipDevelop] native fn address not set for this build - skipped\n");
        return false;
    }

    std::uint16_t index = kInvalidDevelopIndex;
    bool ok = false;
    __try
    {
        index = getIndex(controller, developId);
        if (index < kInvalidDevelopIndex)
        {
            setDeveloped(controller, index);
            ok = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    if (index >= kInvalidDevelopIndex)
        return false;

    if (!ok)
        Log("[EquipDevelop] developId=%u index=%u -> FAILED (native, persistent)\n",
            developId, index);
    return ok;
}

void EquipDevelop_DrainPendingUndevelops()
{
    std::vector<std::int32_t> ids = V_FrameWorkState::TakePendingDevelopedResets();
    for (std::int32_t id : ids)
    {
        if (V_FrameWorkState::IsManagedDevelopId(id))
            continue;
        EquipDevelop_UndevelopByDevelopId(static_cast<std::uint32_t>(id));
    }

    void* controller = ResolveController();
    if (!controller)
    {
        Log("[EquipDevelop] reconcile: controller not resolved - skipped\n");
        return;
    }

    auto getIndex = NativeGetIndex();
    auto setUndeveloped = NativeUndevelop();
    if (!getIndex)
    {
        Log("[EquipDevelop] reconcile: GetEquipDevelopIndex addr unset - skipped\n");
        return;
    }

    struct ManagedRow { std::int32_t developId; bool developed; bool isNew; };
    std::vector<ManagedRow> managed;
    V_FrameWorkState::ForEachManagedDevelop(
        [&](std::int32_t developId, bool developed, bool isNew)
        {
            managed.push_back({ developId, developed, isNew });
        });

    for (const auto& row : managed)
    {
        std::uint16_t index = 0xFFFF;
        int bitBefore = -1;
        ReconcileOneManaged(
            controller, getIndex, setUndeveloped,
            static_cast<std::uint32_t>(row.developId), row.developed, &index, &bitBefore);

        PokeNewBit(controller, getIndex, static_cast<std::uint32_t>(row.developId),
                   row.isNew, row.developed);
    }

    SyncDevelopPrereqGates(controller, getIndex);
    AnnounceNewlyDevelopable(controller, getIndex);

#ifdef _DEBUG
    for (const auto& row : managed)
    {
        const std::uint16_t idx = SafeGetIndex(getIndex, controller,
            static_cast<std::uint32_t>(row.developId));
        if (idx >= kInvalidDevelopIndex)
        {
            Log("[FobDiag] developId=%d -> index UNRESOLVED\n", row.developId);
            continue;
        }
        const std::uint8_t fobByte = SafeReadDevRecordByte(controller, idx, 0x58);
        Log("[FobDiag] developId=%d index=%u recByte58=0x%02X fobBit7=%d\n",
            row.developId, static_cast<unsigned>(idx),
            static_cast<unsigned>(fobByte), (fobByte >> 7) & 1);
    }
#endif

    g_DevelopSyncArmed = true;
}

void EquipDevelop_SetDevelopParent(std::uint32_t developId, std::uint32_t baseDevelopId)
{
    if (developId == 0)
        return;
    std::lock_guard<std::mutex> lock(g_DevelopParentMutex);
    if (baseDevelopId == 0)
        g_DevelopParent.erase(developId);
    else
        g_DevelopParent[developId] = baseDevelopId;
}

void EquipDevelop_InstallDevelopSyncHooks()
{
    void* dev   = ResolveGameAddress(gAddr.EquipDevelopCtrl_SetEquipDeveloped);
    void* undev = ResolveGameAddress(gAddr.EquipDevelopCtrl_SetEquipUndeveloped);

    bool okDev = false, okUndev = false;
    if (dev)
        okDev = CreateAndEnableHook(
            dev, reinterpret_cast<void*>(&hkSetEquipDeveloped),
            reinterpret_cast<void**>(&g_OrigSetEquipDeveloped));
    if (undev)
        okUndev = CreateAndEnableHook(
            undev, reinterpret_cast<void*>(&hkSetEquipUndeveloped),
            reinterpret_cast<void**>(&g_OrigSetEquipUndeveloped));

    if (!okDev || !okUndev)
        Log("[EquipDevelop] develop-sync hooks FAILED: SetEquipDeveloped=%s SetEquipUndeveloped=%s\n",
            okDev ? "OK" : "FAIL", okUndev ? "OK" : "FAIL");
}

bool EquipDevelop_IsDevelopedByDevelopId(std::uint32_t developId)
{
    if (developId == 0)
        return false;

    if (V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
        return V_FrameWorkState::GetDevelopedByDevelopId(static_cast<std::int32_t>(developId));

    void* controller = ResolveController();
    if (!controller)
        return false;

    auto getIndex = reinterpret_cast<GetEquipDevelopIndex_t>(
        ResolveGameAddress(gAddr.EquipDevelopCtrl_GetEquipDevelopIndex));
    if (!getIndex)
        return false;

    __try
    {
        const std::uint16_t index = getIndex(controller, developId);
        if (index >= kInvalidDevelopIndex)
            return false;

        std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
            reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
        if (!bits)
            return false;
        return (bits[index] & 1u) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int __cdecl l_SetEquipUndeveloped(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;
    PushLuaBool(L, developId != 0 && EquipDevelop_UndevelopByDevelopId(developId));
    return 1;
}

int __cdecl l_SetEquipDeveloped(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;
    PushLuaBool(L, developId != 0 && EquipDevelop_DevelopByDevelopId(developId));
    return 1;
}

int __cdecl l_IsEquipDeveloped(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;
    PushLuaBool(L, developId != 0 && EquipDevelop_IsDevelopedByDevelopId(developId));
    return 1;
}

bool EquipDevelop_SetNewByDevelopId(std::uint32_t developId, bool isNew)
{
    if (developId == 0)
        return false;
    if (!V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
        return false;

    V_FrameWorkState::SetNewByDevelopId(static_cast<std::int32_t>(developId), isNew);

    void* controller = ResolveController();
    if (controller)
        PokeNewBit(controller, NativeGetIndex(), developId, isNew,
                   V_FrameWorkState::GetDevelopedByDevelopId(static_cast<std::int32_t>(developId)));

    return true;
}

bool EquipDevelop_IsNewByDevelopId(std::uint32_t developId)
{
    if (developId == 0)
        return false;
    return V_FrameWorkState::GetNewByDevelopId(static_cast<std::int32_t>(developId));
}

bool EquipDevelop_SetVisibleByDevelopId(std::uint32_t developId, bool visible)
{
    if (developId == 0)
        return false;
    if (!V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
        return false;

    std::uint16_t flowIndex = 0;
    if (!EquipDevelopAdd::TryGetFlowIndexForDevelopId(
            static_cast<std::uint16_t>(developId), flowIndex))
        return false;

    outfit::SetDevelopHidden(flowIndex, !visible);

    if (visible)
    {
        void* controller = ResolveController();
        if (controller)
            AnnounceNewlyDevelopable(controller, NativeGetIndex());
    }

    return true;
}

void EquipDevelop_TriggerRequirementsMetAnnounce()
{
    void* controller = ResolveController();
    if (controller)
        AnnounceNewlyDevelopable(controller, NativeGetIndex());
}

int __cdecl l_SetEquipNew(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;

    const bool isNew = (LuaType(L, 2) == 1  ) ? GetLuaBool(L, 2) : true;
    PushLuaBool(L, developId != 0 && EquipDevelop_SetNewByDevelopId(developId, isNew));
    return 1;
}

int __cdecl l_SetEquipDevelopVisible(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;

    const bool visible = (LuaType(L, 2) == 1) ? GetLuaBool(L, 2) : true;
    PushLuaBool(L, developId != 0 && EquipDevelop_SetVisibleByDevelopId(developId, visible));
    return 1;
}

int __cdecl l_IsEquipNew(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;
    PushLuaBool(L, developId != 0 && EquipDevelop_IsNewByDevelopId(developId));
    return 1;
}

bool EquipDevelop_IsDevelopableByDevelopId(std::uint32_t developId)
{
    if (developId == 0)
        return false;

    if (V_FrameWorkState::IsManagedDevelopId(static_cast<std::int32_t>(developId)))
        return !V_FrameWorkState::GetDevelopedByDevelopId(static_cast<std::int32_t>(developId));


    void* controller = ResolveController();
    if (!controller)
        return false;
    auto getIndex = NativeGetIndex();
    if (!getIndex)
        return false;
    __try
    {
        const std::uint16_t index = getIndex(controller, developId);
        if (index >= kInvalidDevelopIndex)
            return false;
        std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
            reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
        if (!bits)
            return false;
        return (bits[index] & 1u) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int __cdecl l_IsEquipDevelopable(lua_State* L)
{
    const std::uint32_t developId =
        LuaIsNumber(L, 1) ? static_cast<std::uint32_t>(GetLuaInt64(L, 1)) : 0u;
    PushLuaBool(L, developId != 0 && EquipDevelop_IsDevelopableByDevelopId(developId));
    return 1;
}

namespace
{
    struct FobSuppressEntry
    {
        std::uint16_t index;
        std::uint8_t  savedBits;
    };
    static std::vector<FobSuppressEntry> g_FobSuppressSaved;
    static bool g_FobSuppressActive = false;

    static bool SafeSuppressOne(void* controller, std::uint16_t index,
                                std::uint8_t* outSaved)
    {
        __try
        {
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (!bits)
                return false;
            *outSaved = bits[index];
            if ((bits[index] & 1u) == 0)
                return false;
            bits[index] = static_cast<std::uint8_t>(bits[index] & ~1u);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void SafeRestoreOne(void* controller, std::uint16_t index,
                               std::uint8_t saved)
    {
        __try
        {
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller) + kDevelopedBitsOffset);
            if (bits)
                bits[index] = saved;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void* EquipDevelop_ResolveDevelopController()
{
    return ResolveController();
}

int EquipDevelop_BeginFobListSuppress()
{
    if (g_FobSuppressActive)
        return static_cast<int>(g_FobSuppressSaved.size());

    void* controller = ResolveController();
    auto getIndex = NativeGetIndex();
    if (!controller || !getIndex)
        return 0;

    std::vector<std::int32_t> ids;
    V_FrameWorkState::ForEachManagedDevelop(
        [&](std::int32_t id, bool, bool) { ids.push_back(id); });

    g_FobSuppressSaved.clear();
    for (std::int32_t id : ids)
    {
        const std::uint16_t index =
            SafeGetIndex(getIndex, controller, static_cast<std::uint32_t>(id));
        if (index >= kInvalidDevelopIndex)
            continue;
        std::uint8_t saved = 0;
        if (SafeSuppressOne(controller, index, &saved))
            g_FobSuppressSaved.push_back({ index, saved });
    }

    g_FobSuppressActive = true;
    return static_cast<int>(g_FobSuppressSaved.size());
}

void EquipDevelop_EndFobListSuppress()
{
    if (!g_FobSuppressActive)
        return;

    if (void* controller = ResolveController())
        for (const FobSuppressEntry& e : g_FobSuppressSaved)
            SafeRestoreOne(controller, e.index, e.savedBits);

    g_FobSuppressSaved.clear();
    g_FobSuppressActive = false;
}
