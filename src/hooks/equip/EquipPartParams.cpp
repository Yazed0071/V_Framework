#include "pch.h"

#include "../outfit/AdditionalMotionTable_GetMtarPathId.h"
#include "../player/AnimControl_GetAnimFileFromGaniPath.h"
#include "../shell/RemoteMissile.h"
#include "DeployGuard.h"
#include "EquipAliasLease.h"
#include "EquipPartParams.h"
#include "CustomScopeUi.h"
#include "LaserSight_UpdatePrim.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "EquipIdCompression.h"
#include "MotionLoaderImpl_GetMtarIds.h"
#include "MtarFile_DidLoadFile.h"
#include "FoxHashes.h"
#include "../../core/FoxPathInternal.h"
#include "GunBasicInject.h"
#include "PartIdWiden.h"
#include "WideMagazine.h"
#include "RealizedEquipObjectImpl_PostPutMotionExecute.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaApi.h"
#include "BulletLockOn.h"
#include "../../lua/V_TppEquipLib.h"
#include "BulletMultiShot.h"
#include "SupportWeaponRegistry.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "TppEquipConstRegistry.h"
#include "../../core/V_FrameWorkState.h"
#include "../buddy/EquipControllerImpl_InitCurrentSlot.h"
#include "SupportStockLease.h"

namespace
{
    struct PartBuffer
    {
        const char* name;
        std::ptrdiff_t implOffset;
        int stride;
        int stockCount;
        int maxId;
        std::vector<std::uint8_t> shadow;
        bool active;
        int nextId;
        std::map<std::string, int> nameToId;
        int space = -1;
        std::uint8_t* origBuf = nullptr;
        bool persistLoaded = false;
        std::set<int> persistReserved;
    };

    static const char* PartPersistTag(const char* poolName)
    {
        if (!poolName)                                     return nullptr;
        if (std::strcmp(poolName, "magazine") == 0)        return "AM";
        if (std::strcmp(poolName, "stock") == 0)           return "SK";
        if (std::strcmp(poolName, "muzzleOption") == 0)    return "MO";
        if (std::strcmp(poolName, "sight") == 0)           return "ST";
        if (std::strcmp(poolName, "barrel") == 0)          return "BA";
        if (std::strcmp(poolName, "underBarrel") == 0)     return "UB";
        if (std::strcmp(poolName, "option") == 0)          return "LTLS";
        if (std::strcmp(poolName, "bullet") == 0)          return "BL";
        if (std::strcmp(poolName, "receiverBuffer") == 0)  return "RC";
        return nullptr;
    }

    static PartBuffer* g_PartPersistLoadTarget = nullptr;

    static void PartPersistReserveCb(const char* name, std::int32_t value)
    {
        static_cast<void>(name);
        if (g_PartPersistLoadTarget && value > 0)
            g_PartPersistLoadTarget->persistReserved.insert(static_cast<int>(value));
    }

    static void EnsurePartPersistLoaded(PartBuffer& pb)
    {
        if (pb.persistLoaded)
            return;
        pb.persistLoaded = true;
        if (const char* tag = PartPersistTag(pb.name))
        {
            g_PartPersistLoadTarget = &pb;
            V_FrameWorkState::ForEachPersistedConstant(tag, &PartPersistReserveCb);
            g_PartPersistLoadTarget = nullptr;
        }
    }

    static PartBuffer g_Magazine = { "magazine", 0x20, 8, 191, 255, {}, false, 192, {}, kVanillaSpace_Magazine };
    static PartBuffer g_Stock    = { "stock",    0x40, 2, 42,  255, {}, false, 43,  {}, kVanillaSpace_Stock };
    static PartBuffer g_Muzzle   = { "muzzleOption", 0x28, 3, 39, 255, {}, false, 40, {}, kVanillaSpace_MuzzleOption };
    static PartBuffer g_Sight    = { "sight",    0x38, 5, 24,  255, {}, false, 25,  {}, kVanillaSpace_Sight };
    static PartBuffer g_Barrel   = { "barrel",   0x18, 2, 114, 255, {}, false, 115, {}, kVanillaSpace_Barrel };
    static PartBuffer g_UnderBarrel = { "underBarrel", 0x48, 3, 22, 255, {}, false, 23, {}, kVanillaSpace_UnderBarrel };
    static std::map<int, int> g_UnderBarrelMagazine;
    static std::set<int> g_SharedMagazineLanes;
    static std::set<int> g_LooseMagazineLanes;
    static PartBuffer g_Option   = { "option",   0x30, 1, 9,  255, {}, false, 10,  {}, kVanillaSpace_Option };
    static PartBuffer g_Bullet   = { "bullet",   0x50, 14, 112, 255, {}, false, 113, {}, -1 };

    static std::recursive_mutex g_Mutex;

    static void** PartPtrLoc(const PartBuffer& pb)
    {
        auto* impl = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance));
        if (!impl)
            return nullptr;
        return reinterpret_cast<void**>(impl + pb.implOffset);
    }

    static std::uint8_t* ReadPtrSEH(void** loc)
    {
        if (!loc)
            return nullptr;
        __try
        {
            return static_cast<std::uint8_t*>(*loc);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static int InitShadowSEH(void** loc, std::uint8_t* shadow, size_t copyBytes)
    {
        __try
        {
            std::uint8_t* stock = static_cast<std::uint8_t*>(*loc);
            if (!stock)
                return 0;
            std::memcpy(shadow, stock, copyBytes);
            *loc = shadow;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void EnsurePartIdWidenArmed();

    static bool EnsurePartShadow(PartBuffer& pb)
    {
        if (pb.active)
            return true;

        EnsurePartIdWidenArmed();
        void** loc = PartPtrLoc(pb);
        if (!loc)
            return false;

        std::uint8_t* stockBuf = ReadPtrSEH(loc);

        if (pb.shadow.empty())
            pb.shadow.assign(static_cast<size_t>(pb.maxId) * pb.stride, 0);

        if (InitShadowSEH(loc, pb.shadow.data(),
                          static_cast<size_t>(pb.stockCount) * pb.stride) != 1)
        {
            pb.shadow.clear();
            return false;
        }

        pb.origBuf = stockBuf;
        pb.active = true;
        return true;
    }

    static std::uint8_t* PartCurrentBuf(PartBuffer& pb)
    {
        return ReadPtrSEH(PartPtrLoc(pb));
    }

    static constexpr std::size_t kSupplyScanWindow = 0x400;

    static int ResolveSupplyObjectsSEH(void* getQuarkFn, std::uint8_t** outApp,
                                       std::uint8_t** outEquipSys,
                                       std::uint8_t** outParamObj)
    {
        *outApp = nullptr;
        *outEquipSys = nullptr;
        *outParamObj = nullptr;
        __try
        {
            using GetQuark_t = std::uint8_t* (__fastcall*)();
            std::uint8_t* quark = reinterpret_cast<GetQuark_t>(getQuarkFn)();
            if (!quark)
                return 0;
            std::uint8_t* app = *reinterpret_cast<std::uint8_t**>(quark + 0x98);
            if (!app)
                return 0;
            *outApp = app;
            std::uint8_t* es = *reinterpret_cast<std::uint8_t**>(app + 0x1e8);
            *outEquipSys = es;
            if (es)
                *outParamObj = *reinterpret_cast<std::uint8_t**>(es + 0x10);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int ScanRepointPtrSEH(std::uint8_t* base, std::size_t window,
                                 std::uint8_t* from, std::uint8_t* to, int* nHits)
    {
        __try
        {
            for (std::size_t off = 0; off + sizeof(void*) <= window;
                 off += sizeof(void*))
            {
                void** slot = reinterpret_cast<void**>(base + off);
                if (*slot != from)
                    continue;
                *slot = to;
                ++(*nHits);
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::atomic<bool> g_SupplyMagHealed{ false };

    static void HealSupplyAmmoCountTable()
    {
        if (g_SupplyMagHealed.load(std::memory_order_relaxed))
            return;
        void* getQuark = ResolveGameAddress(gAddr.GetQuarkSystemTable);
        if (!getQuark)
            return;

        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        std::uint8_t* to = g_Magazine.shadow.empty()
            ? nullptr : g_Magazine.shadow.data();
        std::uint8_t* from = g_Magazine.origBuf;
        if (!g_Magazine.active || !to || !from || from == to)
            return;

        std::uint8_t* app = nullptr;
        std::uint8_t* es  = nullptr;
        std::uint8_t* po  = nullptr;
        if (ResolveSupplyObjectsSEH(getQuark, &app, &es, &po) != 1)
            return;

        int nHits = 0;
        if (app)
            ScanRepointPtrSEH(app, kSupplyScanWindow, from, to, &nHits);
        if (es)
            ScanRepointPtrSEH(es,  kSupplyScanWindow, from, to, &nHits);
        if (po)
            ScanRepointPtrSEH(po,  kSupplyScanWindow, from, to, &nHits);

        if (nHits > 0)
            g_SupplyMagHealed.store(true, std::memory_order_relaxed);
    }

    struct WidePartState
    {
        std::vector<std::uint8_t> row;
        bool rowSet = false;
        int alias = 0;
        int pendingUbType = 0;
        int motionFrom = 0;
        int motionType = -1;
        int magazineDonor = 0;
        bool looseDonor = false;
    };
    static const int kWideIdBase = 0x100;
    static const int kAliasReserve = 16;
    static const int kRcvAliasReserve = 12;
    static std::map<int, WidePartState> g_WideParts[kVanillaSpace_Count];
    static int g_WideNext[kVanillaSpace_Count];
    static int ResolvePartByteLocked(int space, int id);
    static WidePartState* WideStateFor(int space, int id);

    static int AllocateWidePartId(PartBuffer& pb, const char* name)
    {
        if (pb.space < 0 || pb.space >= kVanillaSpace_Count)
            return 0;
        if (g_WideNext[pb.space] < kWideIdBase)
            g_WideNext[pb.space] = kWideIdBase;
        const int id = g_WideNext[pb.space]++;
        pb.nameToId[name] = id;
        g_WideParts[pb.space][id];
        return id;
    }

    static void EnsurePartIdWidenArmed()
    {
        static std::once_flag once;
        std::call_once(once, []
        {
            if (!PartIdWiden_Plan())
                return;
            EquipParam_EnableWidePartIds(65535);
            WideMagazine_Plan();
        });
    }

    static bool WideMagazinesPlanned()
    {
        return WideMagazine_IsPlanned() && PartIdWiden_IsPlanned();
    }

    static bool PartIdTakenThisSession(const PartBuffer& pb, int id)
    {
        for (const auto& kv : pb.nameToId)
            if (kv.second == id)
                return true;
        return false;
    }

    static bool PartIdUsable(const PartBuffer& pb, int id)
    {
        if (id <= pb.stockCount || id > pb.maxId)
            return false;
        if (pb.space >= 0 && pb.space < kVanillaSpace_Count
            && g_WideParts[pb.space].count(id) != 0)
            return false;
        return !PartIdTakenThisSession(pb, id);
    }

    static int AllocatePartSlot(PartBuffer& pb, const char* name)
    {
        if (!name || !name[0])
            return 0;

        EnsurePartIdWidenArmed();

        auto it = pb.nameToId.find(name);
        if (it != pb.nameToId.end())
            return it->second;

        if (!EnsurePartShadow(pb))
            return 0;

        EnsurePartPersistLoaded(pb);
        const char* const persistTag = PartPersistTag(pb.name);
        if (persistTag)
        {
            const int persisted =
                V_FrameWorkState::GetPersistedConstant(persistTag, name);
            if (persisted > 0 && PartIdUsable(pb, persisted))
            {
                pb.persistReserved.erase(persisted);
                pb.nameToId[name] = persisted;
                if (persisted >= kWideIdBase && pb.space >= 0
                    && pb.space < kVanillaSpace_Count)
                    g_WideParts[pb.space][persisted];
                return persisted;
            }
            if (persisted > 0)
                LogWarn("EquipParam: saved %s id %d for '%s' is taken, reallocating",
                    pb.name, persisted, name);
        }

        if (pb.nextId <= pb.stockCount)
            pb.nextId = pb.stockCount + 1;

        if (!PartIdWiden_IsPlanned()
            && pb.nextId > pb.maxId - kAliasReserve && pb.space >= 0)
        {
            const int wide = AllocateWidePartId(pb, name);
            if (wide > 0 && persistTag)
                V_FrameWorkState::SetPersistedConstant(persistTag, name, wide);
            return wide;
        }

        while (pb.nextId <= pb.maxId)
        {
            if (pb.space >= 0 && pb.space < kVanillaSpace_Count
                && g_WideParts[pb.space].count(pb.nextId) != 0)
            {
                ++pb.nextId;
                continue;
            }
            if (pb.persistReserved.count(pb.nextId) != 0
                || PartIdTakenThisSession(pb, pb.nextId)
                || (&pb == &g_Bullet && SupportStockLease::IsReservedStockByte(pb.nextId)))
            {
                ++pb.nextId;
                continue;
            }
            break;
        }

        if (pb.nextId > pb.maxId)
        {
            LogError("EquipParam: %s ids %d..%d exhausted, '%s' not allocated",
                pb.name, pb.stockCount + 1, pb.maxId, name);
            return 0;
        }

        const int id = pb.nextId++;
        pb.nameToId[name] = id;
        if (id >= kWideIdBase && pb.space >= 0 && pb.space < kVanillaSpace_Count)
            g_WideParts[pb.space][id];
        if (persistTag)
            V_FrameWorkState::SetPersistedConstant(persistTag, name, id);
        return id;
    }

    static WidePartState* WideStateFor(int space, int id)
    {
        if (space < 0 || space >= kVanillaSpace_Count || id < kWideIdBase)
            return nullptr;
        auto it = g_WideParts[space].find(id);
        return (it != g_WideParts[space].end()) ? &it->second : nullptr;
    }

    static std::uint8_t* WideRowFor(PartBuffer& pb, int id, int& writeId)
    {
        if (pb.space < 0)
            return nullptr;
        auto& w = g_WideParts[pb.space][id];
        if (w.row.size() != static_cast<size_t>(pb.stride))
            w.row.assign(static_cast<size_t>(pb.stride), 0);
        w.rowSet = true;
        writeId = 1;
        return w.row.data();
    }

    static void SyncWideRowToAlias(PartBuffer& pb, int id)
    {
        WidePartState* w = WideStateFor(pb.space, id);
        if (!w || !w->rowSet || w->row.size() != static_cast<size_t>(pb.stride))
            return;
        std::uint8_t* buf = PartCurrentBuf(pb);
        if (!buf)
            return;
        if (PartIdWiden_IsPlanned() && id >= kWideIdBase && id <= pb.maxId)
            std::memcpy(buf + static_cast<size_t>(id - 1) * pb.stride,
                        w->row.data(), static_cast<size_t>(pb.stride));
        if (w->alias > 0 && w->alias <= pb.maxId)
            std::memcpy(buf + static_cast<size_t>(w->alias - 1) * pb.stride,
                        w->row.data(), static_cast<size_t>(pb.stride));
    }

    static int Clamp(int v, int lo, int hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static bool ReadNamedInt(lua_State* L, int tableIdx, const char* name, int& out)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const bool ok = g_lua_isnumber(L, -1) != 0;
        if (ok)
            out = static_cast<int>(g_lua_tointeger(L, -1));
        g_lua_settop(L, -2);
        return ok;
    }

    static bool ReadNamedIntAlias(lua_State* L, int tableIdx, const char* name,
                                  const char* legacy, int& out)
    {
        if (ReadNamedInt(L, tableIdx, name, out))
            return true;
        return ReadNamedInt(L, tableIdx, legacy, out);
    }

    static bool ReadNamedFloat(lua_State* L, int tableIdx, const char* name, double& out)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const bool ok = g_lua_isnumber(L, -1) != 0;
        if (ok)
            out = static_cast<double>(g_lua_tonumber(L, -1));
        g_lua_settop(L, -2);
        return ok;
    }

    static int ReadNamedBoolTri(lua_State* L, int tableIdx, const char* name)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const int t = g_lua_type(L, -1);
        int v = -1;
        if (t == LUA_TBOOLEAN)
            v = g_lua_toboolean(L, -1) != 0 ? 1 : 0;
        else if (t == LUA_TNUMBER)
            v = g_lua_tointeger(L, -1) != 0 ? 1 : 0;
        g_lua_settop(L, -2);
        return v;
    }

    static int ScaleByte(double v, double scale)
    {
        return Clamp(static_cast<int>(v * scale + 0.5), 0, 255);
    }

    constexpr int kBulletTypeShell = 3;

    static const char* const kBulletBaseNames[] = {
        "tranqNear", "tranqFar", "tranqResidual",
        "damageNear", "damageFar", "damageResidual",
        "impactNear", "impactFar", "impactResidual",
        "penNear", "penFar", "penSwitchDistance", nullptr };
    static const char* const kBarrelBaseNames[] = {
        "fireRateMult", "unk2", "gunAimAdjustMult", "rangeMult",
        "rangeUIMult", "spreadMaxMult", "percentOverride", nullptr };
    static const char* const kRcvBaseNames[] = {
        "fireRate", "aimAssistDist", "gunAimAdjust", "effectiveRange",
        "effectiveRangeUI", "adsZoom", "adsFov", "reloadSpeed", nullptr };
    static const char* const kRcvWobNames[] = {
        "spreadPerShot", "unk2", "spreadRecovery", "spreadMin",
        "spreadMax", "shotKick", "shotKick2", nullptr };
    static const char* const kRcvSysNames[] = {
        "eqpType", "reticleUiId", "triggerId",
        "showMagazineMesh", "plusOneChamber", "missileMeshVariant",
        "modelDedupExclude", "flag5", "sightMountMesh",
        "railMountMesh", "railMountMesh2", "altMagazineSocket", nullptr };

    static int ReadNumberOrFloatTable(lua_State* L, int tableIdx, const char* field,
                                      int& num, double* vals, int maxN, int& count,
                                      const char* const* names = nullptr)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(field));
        const int t = g_lua_type(L, -1);
        if (t == LUA_TNUMBER)
        {
            num = static_cast<int>(g_lua_tointeger(L, -1));
            g_lua_settop(L, -2);
            return 1;
        }
        if (t == LUA_TTABLE)
        {
            count = 0;
            const int n = static_cast<int>(g_lua_objlen(L, -1));
            for (int i = 1; i <= n && count < maxN; ++i)
            {
                g_lua_rawgeti(L, -1, i);
                if (g_lua_isnumber(L, -1))
                    vals[count++] = static_cast<double>(g_lua_tonumber(L, -1));
                g_lua_settop(L, -2);
            }
            if (names)
            {
                for (int i = 0; i < maxN && names[i]; ++i)
                {
                    g_lua_getfield(L, -1, const_cast<char*>(names[i]));
                    if (g_lua_isnumber(L, -1))
                    {
                        vals[i] = static_cast<double>(g_lua_tonumber(L, -1));
                        if (count < i + 1) count = i + 1;
                    }
                    g_lua_settop(L, -2);
                }
            }
            g_lua_settop(L, -2);
            return 2;
        }
        g_lua_settop(L, -2);
        return 0;
    }

    static std::string ReadNamedStringOrTable1(lua_State* L, int tableIdx, const char* name)
    {
        std::string out;
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const int t = g_lua_type(L, -1);
        if (t == LUA_TSTRING)
        {
            size_t len = 0;
            const char* s = g_lua_tolstring(L, -1, &len);
            if (s) out.assign(s, len);
        }
        else if (t == LUA_TTABLE)
        {
            g_lua_rawgeti(L, -1, 1);
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                size_t len = 0;
                const char* s = g_lua_tolstring(L, -1, &len);
                if (s) out.assign(s, len);
            }
            g_lua_settop(L, -2);
        }
        g_lua_settop(L, -2);
        return out;
    }

    static std::vector<std::uint64_t> g_TrailShadow;
    static bool g_TrailShadowActive = false;

    static int ScanTrailListSEH(std::uint64_t** basePtr, std::uint32_t* countPtr,
                                std::uint64_t*& base, std::uint32_t& count,
                                std::uint64_t pathHash, int& found)
    {
        __try
        {
            base  = *basePtr;
            count = *countPtr;
            found = -1;
            if (!base || count == 0 || count > 0x1000)
                return 0;
            for (std::uint32_t i = 0; i < count; ++i)
                if (base[i] == pathHash) { found = static_cast<int>(i); break; }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void RedirectTrailListSEH(std::uint64_t** basePtr, std::uint32_t* countPtr,
                                     std::uint64_t* newBase, std::uint32_t newCount)
    {
        __try
        {
            *basePtr  = newBase;
            *countPtr = newCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static int ResolveTrailPathIndex(std::uint64_t pathHash)
    {
        if (pathHash == 0)
            return -1;
        auto* impl = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance));
        if (!impl)
            return -1;

        auto** basePtr  = reinterpret_cast<std::uint64_t**>(impl + 0x88);
        auto*  countPtr = reinterpret_cast<std::uint32_t*>(impl + 0x90);

        std::uint64_t* base = nullptr;
        std::uint32_t  count = 0;
        int found = -1;
        if (ScanTrailListSEH(basePtr, countPtr, base, count, pathHash, found) != 1)
            return -1;
        if (found >= 0)
            return found;

        if (!g_TrailShadowActive)
        {
            g_TrailShadow.assign(base, base + count);
            g_TrailShadow.reserve(count + 64);
            g_TrailShadowActive = true;
        }
        g_TrailShadow.push_back(pathHash);
        const int idx = static_cast<int>(g_TrailShadow.size() - 1);
        RedirectTrailListSEH(basePtr, countPtr, g_TrailShadow.data(),
                             static_cast<std::uint32_t>(g_TrailShadow.size()));
        return idx;
    }

    static void WriteMagazineSEH(std::uint8_t* buf, int ammoId,
                                 int eqpAmmoId, int magCapacity,
                                 int totalCarry, int bulletId)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(ammoId - 1) * 8;
            *reinterpret_cast<std::uint16_t*>(p + 0) =
                static_cast<std::uint16_t>(eqpAmmoId & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 2) =
                static_cast<std::uint16_t>(Clamp(magCapacity, 0, 0x3FF));
            *reinterpret_cast<std::uint16_t*>(p + 4) =
                static_cast<std::uint16_t>(Clamp(totalCarry <= 0 ? 0x3FFF : totalCarry, 0, 0x3FFF));
            *reinterpret_cast<std::uint16_t*>(p + 6) =
                static_cast<std::uint16_t>(bulletId & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteStockSEH(std::uint8_t* buf, int stockId, int mod2, int mod3)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(stockId - 1) * 2;
            p[0] = static_cast<std::uint8_t>(mod2);
            p[1] = static_cast<std::uint8_t>(mod3);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteMuzzleSEH(std::uint8_t* buf, int muzzleOptionId,
                               int grouping, int durability, int suppressor)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(muzzleOptionId - 1) * 3;
            p[0] = static_cast<std::uint8_t>(grouping);
            p[1] = static_cast<std::uint8_t>(durability & 0xFF);
            p[2] = static_cast<std::uint8_t>(suppressor & 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteOptionSEH(std::uint8_t* buf, int optionId, int isLight, int isLaser)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(optionId - 1);
            p[0] = static_cast<std::uint8_t>((p[0] & 0xFC) | (isLight ? 1 : 0) | (isLaser ? 2 : 0));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteUnderBarrelSEH(std::uint8_t* buf, int underBarrelId,
                                    int receiverId, int magazineId, int weaponGrade)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(underBarrelId - 1) * 3;
            p[0] = static_cast<std::uint8_t>(receiverId & 0xFF);
            p[1] = static_cast<std::uint8_t>(magazineId & 0xFF);
            p[2] = static_cast<std::uint8_t>(weaponGrade & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteBarrelSEH(std::uint8_t* buf, int barrelId, int baseIdx,
                               int barrelLength, int scopeMount, int unkBit6, int sideMount,
                               int underMount)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(barrelId - 1) * 2;
            std::uint8_t b0 = static_cast<std::uint8_t>(barrelLength & 0x0f);
            if (underMount) b0 |= 0x10;
            if (scopeMount) b0 |= 0x20;
            if (unkBit6)    b0 |= 0x40;
            if (sideMount)  b0 |= 0x80;
            p[0] = b0;
            p[1] = static_cast<std::uint8_t>(baseIdx & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteBulletRowSEH(std::uint8_t* buf, int bulletId,
                                  const int u16v[3], const int u8v[7],
                                  int flagD, int typeD)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(bulletId - 1) * 14;
            for (int k = 0; k < 3; ++k)
                *reinterpret_cast<std::uint16_t*>(p + k * 2) =
                    static_cast<std::uint16_t>(u16v[k] & 0xFFFF);
            p[0x06] = static_cast<std::uint8_t>(u8v[0] & 0xFF);
            p[0x07] = static_cast<std::uint8_t>(u8v[1] & 0xFF);
            p[0x08] = static_cast<std::uint8_t>(u8v[2] & 0xFF);
            p[0x09] = static_cast<std::uint8_t>(u8v[3] & 0xFF);
            p[0x0a] = static_cast<std::uint8_t>(u8v[4] & 0xFF);
            p[0x0b] = static_cast<std::uint8_t>(u8v[5] & 0xFF);
            p[0x0c] = static_cast<std::uint8_t>(u8v[6] & 0xFF);
            p[0x0d] = static_cast<std::uint8_t>((flagD ? 1 : 0) | ((typeD & 0x1f) << 1));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteBarrelBaseRowSEH(std::uint8_t* pool, int idx, const double* v, int n)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 7;
            for (int k = 0; k < 7; ++k) p[k] = 0;
            for (int k = 0; k < 7 && k < n; ++k)
                p[k] = static_cast<std::uint8_t>(Clamp(static_cast<int>(v[k] * 100.0 + 0.5), 0, 255));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteBulletBaseRowSEH(std::uint8_t* pool, int idx, const double* v, int n)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 22;
            for (int k = 0; k < 22; ++k) p[k] = 0;
            if (n >  0) *reinterpret_cast<std::uint16_t*>(p + 0x00) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[0]  * 10.0  + 0.5), 0, 65535));
            if (n >  1) *reinterpret_cast<std::uint16_t*>(p + 0x02) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[1]  * 10.0  + 0.5), 0, 65535));
            if (n >  2) *reinterpret_cast<std::uint16_t*>(p + 0x04) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[2]  * 100.0 + 0.5), 0, 65535));
            if (n >  3) *reinterpret_cast<std::uint16_t*>(p + 0x06) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[3]  * 10.0  + 0.5), 0, 65535));
            if (n >  4) *reinterpret_cast<std::uint16_t*>(p + 0x08) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[4]  * 10.0  + 0.5), 0, 65535));
            if (n >  5) *reinterpret_cast<std::uint16_t*>(p + 0x0a) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[5]  * 100.0 + 0.5), 0, 65535));
            if (n >  6) *reinterpret_cast<std::uint16_t*>(p + 0x0c) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[6]  * 10.0  + 0.5), 0, 65535));
            if (n >  7) *reinterpret_cast<std::uint16_t*>(p + 0x0e) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[7]  * 10.0  + 0.5), 0, 65535));
            if (n >  8) *reinterpret_cast<std::uint16_t*>(p + 0x10) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[8]  * 100.0 + 0.5), 0, 65535));
            if (n >  9) p[0x12] = static_cast<std::uint8_t>(Clamp(static_cast<int>(v[9]  + 0.5), 0, 255));
            if (n > 10) p[0x13] = static_cast<std::uint8_t>(Clamp(static_cast<int>(v[10] + 0.5), 0, 255));
            if (n > 11) *reinterpret_cast<std::uint16_t*>(p + 0x14) = static_cast<std::uint16_t>(Clamp(static_cast<int>(v[11] * 10.0  + 0.5), 0, 65535));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteSightSEH(std::uint8_t* buf, int scopeId,
                             int zoom1, int zoom2, int zoom3, int scopeUiId, int flags)
    {
        __try
        {
            std::uint8_t* p = buf + static_cast<size_t>(scopeId - 1) * 5;
            p[0] = static_cast<std::uint8_t>(zoom1);
            p[1] = static_cast<std::uint8_t>(zoom2);
            p[2] = static_cast<std::uint8_t>(zoom3);
            p[3] = static_cast<std::uint8_t>(scopeUiId);
            p[4] = static_cast<std::uint8_t>(flags & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static const int kReceiverImplOffset = 0x10;
    static const int kReceiverStride     = 6;
    static const int kReceiverCapacity   = 233;
    static const int kReceiverMaxId      = 255;
    static PartBuffer g_RcvIdBuf = { "receiverBuffer", 0x10, 6, 233, 255, {}, false, 234, {}, kVanillaSpace_Receiver };

    struct RcvPool
    {
        PartBuffer pb;
        int rowByteOffset;
        bool counted;
    };
    static RcvPool g_Base = { { "receiverParamSetsBase",     0x58, 12, 0, 65536, {}, false, 0, {} }, 2, false };
    static RcvPool g_Wob  = { { "receiverParamSetsWobbling", 0x60, 14, 0, 65536, {}, false, 0, {} }, 3, false };
    static RcvPool g_Sys  = { { "receiverParamSetsSystem",   0x68,  3, 0, 65536, {}, false, 0, {} }, 4, false };
    static RcvPool g_Snd  = { { "receiverParamSetsSound",    0x70,  8, 0, 65536, {}, false, 0, {} }, 5, false };

    enum RcvPoolKind { POOL_BASE, POOL_WOB, POOL_SYS, POOL_SND };

    static RcvPool& PoolFor(RcvPoolKind k)
    {
        switch (k)
        {
        case POOL_WOB: return g_Wob;
        case POOL_SYS: return g_Sys;
        case POOL_SND: return g_Snd;
        default:       return g_Base;
        }
    }


    struct FireSoundSpec
    {
        std::string text;
        int mSeg = -1;
        bool isEvent = false;
        std::string supText;
        bool supIsEvent = false;
    };
    static std::mutex g_FireSoundMutex;
    static std::map<int, FireSoundSpec> g_FireSoundByRow;

    static bool ReadSndRowSEH(const std::uint8_t* pool, int idx, char out[9])
    {
        __try
        {
            const std::uint8_t* p = pool + static_cast<std::size_t>(idx) * 8;
            for (int i = 0; i < 8; ++i)
                out[i] = static_cast<char>(p[i]);
            out[8] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool ReadReceiverSndIndexSEH(const std::uint8_t* rbuf, int receiverId,
                                        int& outIdx)
    {
        __try
        {
            outIdx = rbuf[static_cast<std::size_t>(receiverId) * kReceiverStride
                          + g_Snd.rowByteOffset];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool ResolveDonorSoundRoot(int donorReceiverId, std::string& out)
    {
        out.clear();
        if (donorReceiverId <= 0 || donorReceiverId > kReceiverMaxId)
            return false;

        const std::uint8_t* rbuf = PartCurrentBuf(g_RcvIdBuf);
        const std::uint8_t* pool = PartCurrentBuf(g_Snd.pb);
        if (!rbuf || !pool)
            return false;

        int sndIdx = 0;
        if (!ReadReceiverSndIndexSEH(rbuf, donorReceiverId, sndIdx))
            return false;

        char root[9] = {};
        if (!ReadSndRowSEH(pool, sndIdx, root))
            return false;
        if (root[0] == '\0')
            return false;

        out.assign(root);
        return true;
    }

    static void RegisterFireSoundSupOverride(int idx, const std::string& text,
                                             bool isEvent, const char* field)
    {
        if (text.empty())
            return;
        if (gAddr.WeaponSystem_DefineWeaponFireSound == 0)
        {
            LogWarn("SetReceiver: %s: fire-sound hook absent on this build, vanilla sound kept", field);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_FireSoundMutex);
            FireSoundSpec& spec = g_FireSoundByRow[idx];
            spec.supText = text;
            spec.supIsEvent = isEvent;
        }
    }

    static void RegisterFireSoundOverride(int idx, const std::string& text,
                                          int mSeg, bool isEvent, const char* field)
    {
        const bool needOverride = isEvent || text.size() > 7 || mSeg >= 0;
        if (!needOverride)
            return;
        if (gAddr.WeaponSystem_DefineWeaponFireSound == 0)
        {
            if (isEvent || text.size() > 7)
                LogWarn("SetReceiver: %s '%s': fire-sound hook absent on this build, using 7-char row",
                    field, text.c_str());
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_FireSoundMutex);
            g_FireSoundByRow[idx] = FireSoundSpec{ text, mSeg, isEvent };
        }
    }

    struct RefPool
    {
        PartBuffer pb;
        int refImplOffset;
        int refStride;
        int refCapacity;
        int refByteOffset;
        bool counted;
    };
    static RefPool g_BarrelBase = { { "barrelParamSetsBase", 0x78, 7,  0, 65536, {}, false, 0, {} }, 0x18, 2, 114, 1, false };
    static RefPool g_BulletBase = { { "bulletParamSetsBase", 0x80, 22, 0, 65536, {}, false, 0, {} }, 0x50, 14, 112, 6, false };

    static void EnsureOnePoolRelocated(PartBuffer& pb)
    {
        if (!pb.active || pb.shadow.empty())
            return;
        void** loc = PartPtrLoc(pb);
        if (!loc)
            return;
        std::uint8_t* cur = ReadPtrSEH(loc);
        if (!cur || cur == pb.shadow.data())
            return;
        InitShadowSEH(loc, pb.shadow.data(),
                      static_cast<size_t>(pb.stockCount) * pb.stride);
    }

    static bool RestampRcvRowSEH(std::uint8_t* dst, const std::uint8_t* src,
                                 int stride)
    {
        __try
        {
            if (std::memcmp(dst, src, static_cast<size_t>(stride)) == 0)
                return false;
            std::memcpy(dst, src, static_cast<size_t>(stride));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ReassertWideReceiverLanes()
    {
        const int space = g_RcvIdBuf.space;
        if (space < 0 || space >= kVanillaSpace_Count)
            return;
        std::uint8_t* buf = PartCurrentBuf(g_RcvIdBuf);
        if (!buf)
            return;

        int repaired = 0;
        int firstLane = 0;
        for (auto& kv : g_WideParts[space])
        {
            WidePartState& w = kv.second;
            if (!w.alias
                || w.row.size() != static_cast<size_t>(g_RcvIdBuf.stride))
                continue;
            std::uint8_t* dst = buf
                + static_cast<size_t>(w.alias - 1) * g_RcvIdBuf.stride;
            if (!RestampRcvRowSEH(dst, w.row.data(), g_RcvIdBuf.stride))
                continue;
            ++repaired;
            if (!firstLane)
                firstLane = w.alias;
        }

        static bool s_warned = false;
        if (repaired && !s_warned)
        {
            s_warned = true;
            LogWarn("EquipParam: %d custom receiver lane(s) overwritten by a table reload (first %d), re-asserted",
                repaired, firstLane);
        }
    }

    static void EnsureShadowsRelocated()
    {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        EnsureOnePoolRelocated(g_RcvIdBuf);
        ReassertWideReceiverLanes();
        EnsureOnePoolRelocated(g_Base.pb);
        EnsureOnePoolRelocated(g_Wob.pb);
        EnsureOnePoolRelocated(g_Sys.pb);
        EnsureOnePoolRelocated(g_Snd.pb);
        EnsureOnePoolRelocated(g_Magazine);
        EnsureOnePoolRelocated(g_Bullet);
        EnsureOnePoolRelocated(g_Muzzle);
        EnsureOnePoolRelocated(g_Stock);
        EnsureOnePoolRelocated(g_Sight);
        EnsureOnePoolRelocated(g_Barrel);
        EnsureOnePoolRelocated(g_UnderBarrel);
        EnsureOnePoolRelocated(g_Option);
        EnsureOnePoolRelocated(g_BarrelBase.pb);
        EnsureOnePoolRelocated(g_BulletBase.pb);
    }

    constexpr int kPoolDirectMax     = 253;
    constexpr int kPoolScratch1      = 254;
    constexpr int kPoolScratch0      = 255;
    constexpr int kPoolOverflowStart = 256;

    struct RcvOverflowRef { int idx[4] = { -1, -1, -1, -1 }; };
    static std::map<int, RcvOverflowRef> g_RcvOverflow;
    static std::map<int, int> g_BarrelOverflow;
    static std::map<int, int> g_BulletBaseOverflow;

    struct BarrelExtraMult { double rem[6] = { 1, 1, 1, 1, 1, 1 }; };
    static std::map<int, BarrelExtraMult> g_BarrelExtraMult;

    static std::map<int, int> g_ReceiverMotionDonor;
    static std::map<int, int> g_DeclaredReceiverType;
    static std::map<int, int> g_MotionFromReceiverType;
    static std::set<int> g_ReceiverMotionUntypedLogged;
    static std::set<int> g_ReceiverWrittenUntyped;

    static std::map<std::string, int> g_RcvNameToId;
    static std::set<int> g_RcvClaimed;
    static void LaneWindow_BindForBuild(void* desc, unsigned int equipId);

    static std::uint8_t* ReceiverTypeTable()
    {
        return static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.MotionLoaderImpl_ReceiverTypeTable));
    }

    static const int kPartTypeExtCount = 65536;

    static std::uint8_t g_RcvTypeExt[kPartTypeExtCount];
    static bool g_RcvTypeExtReady = false;

    static int CopyRcvTypeExtSEH(const std::uint8_t* src)
    {
        __try
        {
            for (int i = 0; i < 240; ++i) g_RcvTypeExt[i] = src[i];
            for (int i = 240; i < kPartTypeExtCount; ++i) g_RcvTypeExt[i] = 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void EnsureRcvTypeExt()
    {
        if (g_RcvTypeExtReady)
            return;
        const std::uint8_t* src = ReceiverTypeTable();
        if (src && CopyRcvTypeExtSEH(src) == 1)
            g_RcvTypeExtReady = true;
    }

    using GetReceiverType_t = unsigned int(__fastcall*)(void* self, unsigned int receiverId);
    static GetReceiverType_t g_OrigGetReceiverType = nullptr;

    unsigned int PartCode_TapReceiverType(unsigned int receiverId, unsigned int result);

    static unsigned int __fastcall hkGetReceiverType(void* self, unsigned int receiverId)
    {
        if (!g_RcvTypeExtReady)
            EnsureRcvTypeExt();
        unsigned int r;
        if (g_RcvTypeExtReady && receiverId < kPartTypeExtCount)
            r = g_RcvTypeExt[receiverId];
        else
            r = g_OrigGetReceiverType ? g_OrigGetReceiverType(self, receiverId) : 0;
        return PartCode_TapReceiverType(receiverId, r);
    }

    static bool WriteReceiverType(int receiverId, int type)
    {
        if (receiverId < 0 || receiverId >= kPartTypeExtCount)
            return false;
        EnsureRcvTypeExt();
        if (!g_RcvTypeExtReady)
            return false;
        g_RcvTypeExt[receiverId] = static_cast<std::uint8_t>(type);
        return true;
    }

    static int ReadVanillaReceiverTypeSEH(const std::uint8_t* table, int receiverId)
    {
        __try
        {
            return table[receiverId];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int LowestVanillaReceiverOfType(int type)
    {
        const std::uint8_t* table = ReceiverTypeTable();
        if (!table || type <= 0)
            return 0;
        for (int id = 1; id <= kReceiverCapacity; ++id)
        {
            const int t = ReadVanillaReceiverTypeSEH(table, id);
            if (t < 0)
                return 0;
            if (t == type)
                return id;
        }
        return 0;
    }

    static void LogReceiverTypeConflict(int receiverId, int receiverType, int motionFromType)
    {
        LogWarn("ReceiverMotion: receiverId=%d: receiverType %d overrides motionFrom type %d",
            receiverId, receiverType, motionFromType);
    }

    static int PartRowByte(const PartBuffer& pb, int id, int byteOff);

    static std::uint8_t* UnderBarrelTypeTable()
    {
        return static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.MotionLoaderImpl_UnderBarrelTypeTable));
    }

    static std::uint8_t g_UbTypeExt[kPartTypeExtCount];
    static bool g_UbTypeExtReady = false;

    static int CopyUbTypeExtSEH(const std::uint8_t* src)
    {
        __try
        {
            for (int i = 0; i < 23; ++i) g_UbTypeExt[i] = src[i];
            for (int i = 23; i < kPartTypeExtCount; ++i) g_UbTypeExt[i] = 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void EnsureUbTypeExt()
    {
        if (g_UbTypeExtReady)
            return;
        const std::uint8_t* src = UnderBarrelTypeTable();
        if (src && CopyUbTypeExtSEH(src) == 1)
            g_UbTypeExtReady = true;
    }

    using GetUnderBarrelType_t = unsigned int(__fastcall*)(void* self, unsigned int underBarrelId);
    static GetUnderBarrelType_t g_OrigGetUnderBarrelType = nullptr;

    unsigned int PartCode_DonorPartType(int gbByte, unsigned int partId,
                                        const std::uint8_t* ext);

    static unsigned int __fastcall hkGetUnderBarrelType(void* self, unsigned int underBarrelId)
    {
        if (!g_UbTypeExtReady)
            EnsureUbTypeExt();
        if (g_UbTypeExtReady && underBarrelId < kPartTypeExtCount)
            return g_UbTypeExt[underBarrelId];
        return g_OrigGetUnderBarrelType ? g_OrigGetUnderBarrelType(self, underBarrelId) : 0;
    }

    static const int kBarrelTypeVanillaRows   = 115;
    static const int kMagazineTypeVanillaRows = 192;
    static const int kStockTypeVanillaRows    = 43;

    static std::uint8_t g_BarrelTypeExt[kPartTypeExtCount];
    static bool         g_BarrelTypeExtReady = false;
    static std::uint8_t g_MagazineTypeExt[kPartTypeExtCount];
    static bool         g_MagazineTypeExtReady = false;
    static std::uint8_t g_StockTypeExt[kPartTypeExtCount];
    static bool         g_StockTypeExtReady = false;

    static int CopyPartTypeExtSEH(std::uint8_t* dst, const std::uint8_t* src, int rows)
    {
        __try
        {
            std::memset(dst, 0, kPartTypeExtCount);
            for (int i = 0; i < rows; ++i)
                dst[i] = src[i];
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void EnsurePartTypeExt(std::uint8_t* dst, bool& ready,
                                  uintptr_t tableAddr, int rows)
    {
        if (ready)
            return;
        const auto* src = static_cast<const std::uint8_t*>(
            ResolveGameAddress(tableAddr));
        if (src && CopyPartTypeExtSEH(dst, src, rows) == 1)
            ready = true;
    }

    using GetPartType_t = unsigned int(__fastcall*)(void* self, unsigned int partId);

    static GetPartType_t g_OrigGetBarrelType   = nullptr;
    static GetPartType_t g_OrigGetMagazineType = nullptr;
    static GetPartType_t g_OrigGetStockType    = nullptr;

    static unsigned int __fastcall hkGetBarrelType(void* self, unsigned int barrelId)
    {
        EnsurePartTypeExt(g_BarrelTypeExt, g_BarrelTypeExtReady,
                          gAddr.MotionLoaderImpl_BarrelTypeTable,
                          kBarrelTypeVanillaRows);
        if (g_BarrelTypeExtReady && barrelId < kPartTypeExtCount)
        {
            const unsigned int t = g_BarrelTypeExt[barrelId];
            return t != 0 ? t
                : PartCode_DonorPartType(1, barrelId, g_BarrelTypeExt);
        }
        return g_OrigGetBarrelType ? g_OrigGetBarrelType(self, barrelId) : 0;
    }

    static unsigned int __fastcall hkGetMagazineType(void* self, unsigned int magazineId)
    {
        EnsurePartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                          gAddr.MotionLoaderImpl_MagazineTypeTable,
                          kMagazineTypeVanillaRows);
        if (g_MagazineTypeExtReady && magazineId < kPartTypeExtCount)
        {
            const unsigned int t = g_MagazineTypeExt[magazineId];
            return t != 0 ? t
                : PartCode_DonorPartType(2, magazineId, g_MagazineTypeExt);
        }
        return g_OrigGetMagazineType ? g_OrigGetMagazineType(self, magazineId) : 0;
    }

    static std::set<int> g_MagazineTypeDeclaredLane;

    static int ReadVanillaPartTypeSEH(const std::uint8_t* table, int id)
    {
        __try
        {
            return table[id];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static bool IsVanillaMagazineType(int type)
    {
        const auto* table = static_cast<const std::uint8_t*>(
            ResolveGameAddress(gAddr.MotionLoaderImpl_MagazineTypeTable));
        if (!table || type <= 0 || type > 0xFF)
            return false;
        for (int id = 1; id < kMagazineTypeVanillaRows; ++id)
        {
            const int t = ReadVanillaPartTypeSEH(table, id);
            if (t < 0)
                return false;
            if (t == type)
                return true;
        }
        return false;
    }

    static bool WriteDeclaredMagazineType(int lane, int type)
    {
        if (lane <= 0 || lane >= kPartTypeExtCount)
            return false;
        EnsurePartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                          gAddr.MotionLoaderImpl_MagazineTypeTable,
                          kMagazineTypeVanillaRows);
        if (!g_MagazineTypeExtReady)
            return false;
        g_MagazineTypeExt[lane] = static_cast<std::uint8_t>(type);
        g_MagazineTypeDeclaredLane.insert(lane);
        return true;
    }

    static unsigned int __fastcall hkGetStockType(void* self, unsigned int stockId)
    {
        EnsurePartTypeExt(g_StockTypeExt, g_StockTypeExtReady,
                          gAddr.MotionLoaderImpl_StockTypeTable,
                          kStockTypeVanillaRows);
        if (g_StockTypeExtReady && stockId < kPartTypeExtCount)
        {
            const unsigned int t = g_StockTypeExt[stockId];
            return t != 0 ? t
                : PartCode_DonorPartType(3, stockId, g_StockTypeExt);
        }
        return g_OrigGetStockType ? g_OrigGetStockType(self, stockId) : 0;
    }

    static const int kUiSightVanillaMax = 24;

    static constexpr std::uint8_t kUiSightTypePrologue[5] =
        { 0xFF, 0xCA, 0x83, 0xFA, 0x17 };

    using GetUiSightType_t = unsigned int(__fastcall*)(void* self, unsigned int sightId);
    static const std::ptrdiff_t kUiSightShownTypeOffset = 0x764;

    static const unsigned int kSubjectiveIdCount = 39;

    using SightWindowCreate_t = void(__fastcall*)(void* self, void* ctx, void* out);
    using SightWindowDelete_t = void(__fastcall*)(void* self, void* handles);

    static GetUiSightType_t   g_OrigGetUiSightTypeBySightId = nullptr;
    static SightWindowCreate_t g_OrigSightWindowCreate = nullptr;
    static SightWindowDelete_t g_OrigSightWindowDelete = nullptr;
    static std::uint8_t       g_UiSightDonor[kPartTypeExtCount];
    static volatile std::uint8_t  g_SubjectiveWindowLive[kSubjectiveIdCount];
    static volatile unsigned int  g_SubjectiveWindowNewest = 0;
    static volatile unsigned int  g_SubjectiveWindowPrev   = 0;
    static volatile unsigned int  g_VanillaAskOverlay      = 0;

    static unsigned int ReadSightFactoryIdSEH(const void* self)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(self) + 8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void __fastcall hkSightWindowCreate(void* self, void* ctx, void* out)
    {
        if (g_OrigSightWindowCreate)
            g_OrigSightWindowCreate(self, ctx, out);
        if (CustomScopeUi::IsCreating())
            return;
        const unsigned int id = ReadSightFactoryIdSEH(self);
        if (id != 0 && id < kSubjectiveIdCount && g_SubjectiveWindowNewest != id)
        {
            g_SubjectiveWindowLive[id] = 1;
            g_SubjectiveWindowPrev     = g_SubjectiveWindowNewest;
            g_SubjectiveWindowNewest   = id;
        }
    }

    static void __fastcall hkSightWindowDelete(void* self, void* handles)
    {
        const unsigned int id = ReadSightFactoryIdSEH(self);
        if (id != 0 && id < kSubjectiveIdCount)
        {
            g_SubjectiveWindowLive[id] = 0;
            if (g_SubjectiveWindowNewest == id)
            {
                g_SubjectiveWindowNewest = g_SubjectiveWindowPrev;
                g_SubjectiveWindowPrev   = 0;
            }
            else if (g_SubjectiveWindowPrev == id)
                g_SubjectiveWindowPrev = 0;
        }
        if (g_OrigSightWindowDelete)
            g_OrigSightWindowDelete(self, handles);
    }

    static bool IsSubjectiveWindowLive(unsigned int id)
    {
        return id != 0 && id < kSubjectiveIdCount && g_SubjectiveWindowLive[id] != 0;
    }

    static unsigned int PickLiveSubjectiveId(unsigned int preferred)
    {
        return IsSubjectiveWindowLive(preferred) ? preferred : 0;
    }
    static unsigned int     g_UiSightLastAsk   = 0xFFFFFFFF;
    static unsigned int     g_UiSightLastShown = 0xFFFFFFFF;

    static unsigned int ReadShownUiSightTypeSEH(const void* self)
    {
        __try
        {
            return *reinterpret_cast<const unsigned int*>(
                static_cast<const std::uint8_t*>(self) + kUiSightShownTypeOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFFFFFF;
        }
    }

    static bool ReadVanillaSightUiSEH(const std::uint8_t* buf, int scopeId,
                                      int& outScopeUi, int& outMagnification)
    {
        __try
        {
            const std::uint8_t* p = buf + static_cast<size_t>(scopeId - 1) * 5;
            int top = p[0];
            if (p[1] > top)
                top = p[1];
            if (p[2] > top)
                top = p[2];
            outMagnification = top;
            outScopeUi       = p[3];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static int PickVanillaSightForScopeUi(int scopeUiId, int magnification)
    {
        const std::uint8_t* buf = PartCurrentBuf(g_Sight);
        if (!buf)
            return 0;
        int best = 0;
        int bestDelta = 0;
        for (int id = 1; id <= kUiSightVanillaMax; ++id)
        {
            int ui = 0;
            int mag = 0;
            if (!ReadVanillaSightUiSEH(buf, id, ui, mag) || ui != scopeUiId)
                continue;
            const int delta = mag > magnification ? mag - magnification
                                                  : magnification - mag;
            if (best == 0 || delta < bestDelta)
            {
                best = id;
                bestDelta = delta;
            }
        }
        return best;
    }

    static void BindCustomSightScopeUi(int scopeId, int scopeUiId,
                                       int zoom1, int zoom2, int zoom3)
    {
        if (scopeId <= kUiSightVanillaMax || scopeId >= kPartTypeExtCount)
            return;
        int magnification = zoom1;
        if (zoom2 > magnification)
            magnification = zoom2;
        if (zoom3 > magnification)
            magnification = zoom3;
        const int donor = scopeUiId != 0
            ? PickVanillaSightForScopeUi(scopeUiId, magnification)
            : 0;
        g_UiSightDonor[scopeId] = static_cast<std::uint8_t>(donor);
        if (scopeId >= kWideIdBase)
        {
            WidePartState* w = WideStateFor(kVanillaSpace_Sight, scopeId);
            if (w && w->alias > kUiSightVanillaMax && w->alias < kPartTypeExtCount)
                g_UiSightDonor[w->alias] = static_cast<std::uint8_t>(donor);
        }
        if (scopeUiId != 0 && donor == 0)
            LogWarn("SetSight scopeId=%d: no vanilla sight has scopeUiId=%d, no scope UI",
                scopeId, scopeUiId);
    }

    static unsigned int __fastcall hkGetUiSightTypeBySightId(void* self,
                                                            unsigned int sightId)
    {
        if (!g_OrigGetUiSightTypeBySightId)
            return 0;
        unsigned int customOverlay = 0;
        if (CustomScopeUi::TryGetOverlay(sightId, customOverlay))
        {
            const bool missing = customOverlay == 0
                || (customOverlay < kSubjectiveIdCount && !IsSubjectiveWindowLive(customOverlay));
            if (missing && (sightId != g_UiSightLastAsk || customOverlay != g_UiSightLastShown))
            {
                g_UiSightLastAsk   = sightId;
                g_UiSightLastShown = customOverlay;
                LogWarn("EquipParam: custom sight %u: its windowFactoryName window is not loaded, scope draws nothing",
                    sightId);
            }
            return customOverlay;
        }
        if (sightId != 0 && sightId <= static_cast<unsigned int>(kUiSightVanillaMax))
        {
            const unsigned int vanilla = g_OrigGetUiSightTypeBySightId(self, sightId);
            g_VanillaAskOverlay = vanilla;
            return vanilla;
        }
        if (sightId > static_cast<unsigned int>(kUiSightVanillaMax)
            && sightId < kPartTypeExtCount)
        {
            const unsigned int donor = g_UiSightDonor[sightId];
            const unsigned int computed = donor != 0
                ? g_OrigGetUiSightTypeBySightId(self, donor)
                : 0;
            const unsigned int live = PickLiveSubjectiveId(computed);
            const unsigned int uiSightType = live != 0 ? live : computed;
            if (uiSightType != 0 && live == 0
                && (sightId != g_UiSightLastAsk
                    || uiSightType != g_UiSightLastShown))
            {
                g_UiSightLastAsk   = sightId;
                g_UiSightLastShown = uiSightType;
                LogWarn("EquipParam: custom sight %u: no scope overlay window for subjective id %u, scope draws nothing",
                    sightId, uiSightType);
            }
            if (uiSightType != 0)
                return uiSightType;
        }
        return g_OrigGetUiSightTypeBySightId(self, sightId);
    }

    static void InheritPartTypeExt(std::uint8_t* ext, bool& ready,
                                   uintptr_t tableAddr, int rows,
                                   int dstId, int srcId,
                                   int& eligible, int& applied)
    {
        if (dstId < rows || dstId >= kPartTypeExtCount)
            return;
        if (srcId <= 0 || srcId >= kPartTypeExtCount)
            return;
        EnsurePartTypeExt(ext, ready, tableAddr, rows);
        if (!ready)
        {
            ++eligible;
            return;
        }
        const std::uint8_t t = ext[srcId];
        if (t == 0)
            return;
        ++eligible;
        ext[dstId] = t;
        ++applied;
    }

    static bool WriteUnderBarrelType(int underBarrelId, int type)
    {
        if (underBarrelId < 0 || underBarrelId >= kPartTypeExtCount)
            return false;
        EnsureUbTypeExt();
        if (!g_UbTypeExtReady)
            return false;
        g_UbTypeExt[underBarrelId] = static_cast<std::uint8_t>(type);
        return true;
    }

    static std::uint8_t* ImplBufPtr(std::ptrdiff_t off)
    {
        auto* impl = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance));
        if (!impl)
            return nullptr;
        return ReadPtrSEH(reinterpret_cast<void**>(impl + off));
    }

    static int RcvRowIsZeroSEH(const std::uint8_t* buf, int idx0)
    {
        __try
        {
            const std::uint8_t* p = buf + static_cast<size_t>(idx0) * kReceiverStride;
            for (int k = 0; k < kReceiverStride; ++k)
                if (p[k] != 0)
                    return 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int ComputeRcvPoolStockCountSEH(const std::uint8_t* rbuf, int rowByteOffset)
    {
        __try
        {
            int mx = 0;
            for (int i = 0; i < kReceiverCapacity; ++i)
            {
                const std::uint8_t* p = rbuf + static_cast<size_t>(i) * kReceiverStride;
                bool nonzero = false;
                for (int k = 0; k < kReceiverStride; ++k)
                    if (p[k]) { nonzero = true; break; }
                if (!nonzero)
                    continue;
                if (p[rowByteOffset] > mx)
                    mx = p[rowByteOffset];
            }
            return mx;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void WriteBaseRowRawSEH(std::uint8_t* pool, int idx, const int* v, int n)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 12;
            for (int k = 0; k < 12; ++k) p[k] = 0;
            if (n > 0) *reinterpret_cast<std::uint16_t*>(p + 0) = static_cast<std::uint16_t>(v[0]);
            if (n > 1) p[2] = static_cast<std::uint8_t>(v[1]);
            if (n > 2) p[3] = static_cast<std::uint8_t>(v[2]);
            if (n > 3) p[4] = static_cast<std::uint8_t>(v[3]);
            if (n > 4) p[5] = static_cast<std::uint8_t>(v[4]);
            if (n > 5) *reinterpret_cast<std::uint16_t*>(p + 6) = static_cast<std::uint16_t>(v[5]);
            if (n > 6) *reinterpret_cast<std::uint16_t*>(p + 8) = static_cast<std::uint16_t>(v[6]);
            if (n > 7) p[0xa] = static_cast<std::uint8_t>(v[7]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteWobRowRawSEH(std::uint8_t* pool, int idx, const int* v, int n)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 14;
            for (int k = 0; k < 14; ++k) p[k] = 0;
            for (int k = 0; k < 7 && k < n; ++k)
                *reinterpret_cast<std::uint16_t*>(p + k * 2) = static_cast<std::uint16_t>(v[k]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteSysRowRawSEH(std::uint8_t* pool, int idx, int b0, int b1, int b2)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 3;
            p[0] = static_cast<std::uint8_t>(b0);
            p[1] = static_cast<std::uint8_t>(b1);
            p[2] = static_cast<std::uint8_t>(b2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteSndRowSEH(std::uint8_t* pool, int idx, const char* label)
    {
        __try
        {
            std::uint8_t* p = pool + static_cast<size_t>(idx) * 8;
            for (int k = 0; k < 8; ++k) p[k] = 0;
            for (int k = 0; k < 7 && label[k]; ++k)
                p[k] = static_cast<std::uint8_t>(label[k]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void WriteReceiverRowSEH(std::uint8_t* rbuf, int receiverId,
                                    int attackId, int baseIdx1, int wob, int sys, int snd)
    {
        __try
        {
            std::uint8_t* p = rbuf + static_cast<size_t>(receiverId - 1) * kReceiverStride;
            *reinterpret_cast<std::uint16_t*>(p + 0) =
                static_cast<std::uint16_t>(attackId & 0xFFFF);
            p[2] = static_cast<std::uint8_t>(baseIdx1 & 0xFF);
            p[3] = static_cast<std::uint8_t>(wob & 0xFF);
            p[4] = static_cast<std::uint8_t>(sys & 0xFF);
            p[5] = static_cast<std::uint8_t>(snd & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static bool EnsureRcvPoolShadow(RcvPool& rp)
    {
        if (rp.pb.active)
            return true;
        if (!rp.counted)
        {
            const std::uint8_t* rbuf = ImplBufPtr(kReceiverImplOffset);
            if (!rbuf)
                return false;
            const int sc = ComputeRcvPoolStockCountSEH(rbuf, rp.rowByteOffset);
            if (sc <= 0)
            {
                LogError("EquipParam: %s vanilla row count unreadable, custom rows off", rp.pb.name);
                return false;
            }
            rp.pb.stockCount = sc + 1;
            rp.pb.nextId = sc + 1;
            rp.counted = true;
        }
        return EnsurePartShadow(rp.pb);
    }

    static int AllocateRcvPoolRow(RcvPool& rp)
    {
        if (!EnsureRcvPoolShadow(rp))
            return -1;
        if (rp.pb.nextId == kPoolScratch1)
            rp.pb.nextId = kPoolOverflowStart;
        if (rp.pb.nextId >= rp.pb.maxId)
        {
            LogError("EquipParam: %s pool exhausted (%d rows)", rp.pb.name, rp.pb.maxId);
            return -1;
        }
        return rp.pb.nextId++;
    }

    static int ComputeRefPoolStockCountSEH(const std::uint8_t* refBuf, int refStride,
                                           int refCapacity, int refByteOffset)
    {
        __try
        {
            int mx = 0;
            for (int i = 0; i < refCapacity; ++i)
            {
                const std::uint8_t* p = refBuf + static_cast<size_t>(i) * refStride;
                bool nonzero = false;
                for (int k = 0; k < refStride; ++k)
                    if (p[k]) { nonzero = true; break; }
                if (!nonzero)
                    continue;
                if (p[refByteOffset] > mx)
                    mx = p[refByteOffset];
            }
            return mx;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool EnsureRefPoolShadow(RefPool& rp)
    {
        if (rp.pb.active)
            return true;
        if (!rp.counted)
        {
            const std::uint8_t* refBuf = ImplBufPtr(rp.refImplOffset);
            if (!refBuf)
                return false;
            const int sc = ComputeRefPoolStockCountSEH(
                refBuf, rp.refStride, rp.refCapacity, rp.refByteOffset);
            if (sc <= 0)
            {
                LogError("EquipParam: %s vanilla row count unreadable, custom rows off", rp.pb.name);
                return false;
            }
            rp.pb.stockCount = sc + 1;
            rp.pb.nextId = sc + 1;
            rp.counted = true;
        }
        return EnsurePartShadow(rp.pb);
    }

    static int AllocateRefPoolRow(RefPool& rp)
    {
        if (!EnsureRefPoolShadow(rp))
            return -1;
        if (rp.pb.nextId == kPoolScratch1)
            rp.pb.nextId = kPoolOverflowStart;
        if (rp.pb.nextId >= rp.pb.maxId)
        {
            LogError("EquipParam: %s pool exhausted (%d rows)", rp.pb.name, rp.pb.maxId);
            return -1;
        }
        return rp.pb.nextId++;
    }

    static int ResolvePoolField(lua_State* L, const char* field, RcvPoolKind kind)
    {
        g_lua_getfield(L, 1, const_cast<char*>(field));
        const int t = g_lua_type(L, -1);
        if (t == LUA_TNIL)
        {
            g_lua_settop(L, -2);
            return -1;
        }
        if (t == LUA_TNUMBER)
        {
            const int v = static_cast<int>(g_lua_tointeger(L, -1));
            g_lua_settop(L, -2);
            return v;
        }
        if (t == LUA_TSTRING && kind == POOL_SND)
        {
            char label[8] = { 0 };
            size_t len = 0;
            const char* s = g_lua_tolstring(L, -1, &len);
            std::string fullRoot;
            if (s)
            {
                fullRoot.assign(s, len);
                for (int c = 0; c < 7 && s[c]; ++c) label[c] = s[c];
            }
            g_lua_settop(L, -2);

            RcvPool& sp = PoolFor(kind);
            const int idx = AllocateRcvPoolRow(sp);
            if (idx < 0) return -2;
            std::uint8_t* pool = PartCurrentBuf(sp.pb);
            if (!pool) return -2;
            WriteSndRowSEH(pool, idx, label);
            RegisterFireSoundOverride(idx, fullRoot, -1, false, field);
            return idx;
        }

        if (t != LUA_TTABLE)
        {
            g_lua_settop(L, -2);
            LogWarn("SetReceiver: %s must be a number or a table", field);
            return -2;
        }

        RcvPool& rp = PoolFor(kind);

        if (kind == POOL_SND)
        {
            char label[8] = { 0 };
            std::string text;
            bool isEvent = false;
            int mSeg = -1;

            g_lua_getfield(L, -1, const_cast<char*>("event"));
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                size_t len = 0;
                const char* s = g_lua_tolstring(L, -1, &len);
                if (s) { text.assign(s, len); isEvent = true; }
            }
            g_lua_settop(L, -2);

            if (!isEvent)
            {
                g_lua_getfield(L, -1, const_cast<char*>("name"));
                if (g_lua_type(L, -1) != LUA_TSTRING)
                {
                    g_lua_settop(L, -2);
                    g_lua_rawgeti(L, -1, 1);
                }
                if (g_lua_type(L, -1) != LUA_TSTRING)
                {
                    g_lua_settop(L, -2);
                    g_lua_getfield(L, -1, const_cast<char*>("label"));
                }
                if (g_lua_type(L, -1) == LUA_TSTRING)
                {
                    size_t len = 0;
                    const char* s = g_lua_tolstring(L, -1, &len);
                    if (s) { text.assign(s, len); for (int c = 0; c < 7 && s[c]; ++c) label[c] = s[c]; }
                }
                g_lua_settop(L, -2);

                g_lua_getfield(L, -1, const_cast<char*>("middle"));
                if (g_lua_type(L, -1) == LUA_TBOOLEAN)
                    mSeg = g_lua_toboolean(L, -1) != 0 ? 1 : 2;
                g_lua_settop(L, -2);
            }
            else
            {
                g_lua_getfield(L, -1, const_cast<char*>("name"));
                if (g_lua_type(L, -1) == LUA_TSTRING)
                {
                    size_t len = 0;
                    const char* s = g_lua_tolstring(L, -1, &len);
                    if (s)
                        for (int c = 0; c < 7 && s[c]; ++c)
                            label[c] = s[c];
                }
                g_lua_settop(L, -2);
            }

            std::string supText;
            bool supIsEvent = false;
            g_lua_getfield(L, -1, const_cast<char*>("supEvent"));
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                size_t len = 0;
                const char* s = g_lua_tolstring(L, -1, &len);
                if (s) { supText.assign(s, len); supIsEvent = true; }
            }
            g_lua_settop(L, -2);
            if (supText.empty())
            {
                g_lua_getfield(L, -1, const_cast<char*>("sup"));
                if (g_lua_type(L, -1) == LUA_TSTRING)
                {
                    size_t len = 0;
                    const char* s = g_lua_tolstring(L, -1, &len);
                    if (s) supText.assign(s, len);
                }
                g_lua_settop(L, -2);
            }

            g_lua_settop(L, -2);

            const int idx = AllocateRcvPoolRow(rp);
            if (idx < 0) return -2;
            std::uint8_t* pool = PartCurrentBuf(rp.pb);
            if (!pool) return -2;
            WriteSndRowSEH(pool, idx, label);
            RegisterFireSoundOverride(idx, text, mSeg, isEvent, field);
            RegisterFireSoundSupOverride(idx, supText, supIsEvent, field);
            return idx;
        }

        double vals[12] = { 0 };
        int n = static_cast<int>(g_lua_objlen(L, -1));
        if (n > 12) n = 12;
        for (int i = 1; i <= n; ++i)
        {
            g_lua_rawgeti(L, -1, i);
            vals[i - 1] = static_cast<double>(g_lua_tonumber(L, -1));
            g_lua_settop(L, -2);
        }
        const char* const* names =
            kind == POOL_BASE ? kRcvBaseNames
            : kind == POOL_WOB ? kRcvWobNames : kRcvSysNames;
        for (int i = 0; i < 12 && names[i]; ++i)
        {
            g_lua_getfield(L, -1, const_cast<char*>(names[i]));
            if (g_lua_isnumber(L, -1))
            {
                vals[i] = static_cast<double>(g_lua_tonumber(L, -1));
                if (n < i + 1) n = i + 1;
            }
            g_lua_settop(L, -2);
        }
        g_lua_settop(L, -2);

        const int idx = AllocateRcvPoolRow(rp);
        if (idx < 0) return -2;
        std::uint8_t* pool = PartCurrentBuf(rp.pb);
        if (!pool) return -2;

        if (kind == POOL_BASE)
        {
            int v[8];
            v[0] = Clamp(static_cast<int>(vals[0] * 10.0 + 0.5), 0, 65535);
            v[1] = Clamp(static_cast<int>(vals[1] + 0.5), 0, 255);
            v[2] = Clamp(static_cast<int>(vals[2] * 200.0 + 0.5), 0, 255);
            v[3] = Clamp(static_cast<int>(vals[3] + 0.5), 0, 255);
            v[4] = Clamp(static_cast<int>(vals[4] + 0.5), 0, 255);
            v[5] = Clamp(static_cast<int>(vals[5] * 1000.0 + 0.5), 0, 65535);
            v[6] = Clamp(static_cast<int>(vals[6] * 100.0 + 0.5), 0, 65535);
            v[7] = Clamp(static_cast<int>(vals[7] * 100.0 + 0.5), 0, 255);
            WriteBaseRowRawSEH(pool, idx, v, n);
        }
        else if (kind == POOL_WOB)
        {
            int v[7];
            for (int k = 0; k < 7; ++k)
                v[k] = Clamp(static_cast<int>(vals[k] * 1000.0 + 0.5), 0, 65535);
            WriteWobRowRawSEH(pool, idx, v, n);
        }
        else
        {
            int iv[12];
            for (int k = 0; k < 12; ++k)
                iv[k] = static_cast<int>(vals[k] + 0.5);
            const int b0 = (iv[0] & 0x1f) | (iv[3] ? 0x20 : 0) | (iv[4] ? 0x40 : 0) | (iv[5] ? 0x80 : 0);
            const int b1 = (iv[1] & 0x1f) | ((iv[2] & 0x7) << 5);
            const int b2 = (iv[6] ? 1 : 0) | (iv[7] ? 2 : 0) | (iv[8] ? 4 : 0)
                         | (iv[9] ? 8 : 0) | (iv[10] ? 0x10 : 0) | (iv[11] ? 0x20 : 0);
            WriteSysRowRawSEH(pool, idx, b0, b1, b2);
        }
        return idx;
    }
}

int EquipParam_AllocateMagazineSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Magazine, name);
}

int EquipParam_AllocateStockSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Stock, name);
}

int EquipParam_AllocateMuzzleSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Muzzle, name);
}

int EquipParam_AllocateSightSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Sight, name);
}

int EquipParam_AllocateBarrelSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Barrel, name);
}

int EquipParam_AllocateUnderBarrelSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_UnderBarrel, name);
}

int EquipParam_AllocateOptionSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Option, name);
}

int EquipParam_AllocateBulletSlotForName(const char* name)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return AllocatePartSlot(g_Bullet, name);
}

static bool ReadLaserColorAt(lua_State* L, int idx, LaserColor& out)
{
    LaserColor c{};
    bool named = false;
    double v = 0.0;
    if (ReadNamedFloat(L, idx, "r", v)) { c.r = static_cast<float>(v); named = true; }
    if (ReadNamedFloat(L, idx, "g", v)) { c.g = static_cast<float>(v); named = true; }
    if (ReadNamedFloat(L, idx, "b", v)) { c.b = static_cast<float>(v); named = true; }
    if (ReadNamedFloat(L, idx, "a", v)) { c.a = static_cast<float>(v); named = true; }
    if (named)
    {
        out = c;
        return true;
    }

    int filled = 0;
    for (int i = 1; i <= 4; ++i)
    {
        g_lua_rawgeti(L, idx, i);
        const bool isNum = g_lua_isnumber(L, -1) != 0;
        if (isNum)
        {
            const float f = static_cast<float>(g_lua_tonumber(L, -1));
            if (i == 1)      c.r = f;
            else if (i == 2) c.g = f;
            else if (i == 3) c.b = f;
            else             c.a = f;
            ++filled;
        }
        g_lua_settop(L, -2);
        if (!isNum)
            break;
    }
    if (filled >= 3)
    {
        out = c;
        return true;
    }
    return false;
}

static void ApplyLaserColorField(lua_State* L, int tableIdx, int optionId)
{
    g_lua_getfield(L, tableIdx, const_cast<char*>("laserColor"));
    const int colorIdx = g_lua_gettop(L);
    if (g_lua_type(L, colorIdx) != LUA_TTABLE)
    {
        g_lua_settop(L, -2);
        return;
    }

    bool perEquip = false;
    g_lua_pushnil(L);
    while (g_lua_next(L, colorIdx) != 0)
    {
        if (g_lua_type(L, -1) == LUA_TTABLE)
            perEquip = true;
        g_lua_settop(L, -2);
        if (perEquip)
        {
            g_lua_settop(L, -2);
            break;
        }
    }

    if (!perEquip)
    {
        LaserColor c{};
        if (ReadLaserColorAt(L, colorIdx, c))
            LaserColor_SetDefaultForOption(optionId, c);
        else
            LogWarn("SetOption optionId=%d: laserColor has no r/g/b values, laser stays red", optionId);
        g_lua_settop(L, -2);
        return;
    }

    int taken = 0;
    g_lua_pushnil(L);
    while (g_lua_next(L, colorIdx) != 0)
    {
        const int valIdx = g_lua_gettop(L);
        if (g_lua_type(L, valIdx) == LUA_TTABLE)
        {
            LaserColor c{};
            if (ReadLaserColorAt(L, valIdx, c))
            {
                if (g_lua_isnumber(L, valIdx - 1) != 0)
                    LaserColor_SetForOptionEquip(
                        optionId, static_cast<int>(g_lua_tointeger(L, valIdx - 1)), c);
                else
                    LaserColor_SetDefaultForOption(optionId, c);
                ++taken;
            }
        }
        g_lua_settop(L, -2);
    }
    if (taken == 0)
        LogWarn("SetOption optionId=%d: no laserColor equipId entry has r/g/b, laser stays red", optionId);
    g_lua_settop(L, -2);
}

int __cdecl l_SetOption(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetOption: argument #1 must be a table");
        return 0;
    }

    int optionId = 0;
    if (!ReadNamedInt(L, 1, "optionId", optionId) || optionId <= 0)
    {
        LogWarn("SetOption: missing or invalid optionId");
        return 0;
    }

    int isLight = 0, isLaser = 0;
    ReadNamedInt(L, 1, "isLight", isLight);
    ReadNamedInt(L, 1, "isLaser", isLaser);

    ApplyLaserColorField(L, 1, optionId);

    for (int lt = 0; lt < kLaserTweakCount; ++lt)
    {
        const char* tweakName = LaserTweak_NameForIndex(lt);
        double tweakValue = 0.0;
        if (tweakName && ReadNamedFloat(L, 1, tweakName, tweakValue))
            LaserTweak_SetForOption(optionId, lt, static_cast<float>(tweakValue));
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Option))
    {
        LogError("SetOption optionId=%d: option table unavailable on this build, skipped", optionId);
        return 0;
    }
    int writeId = optionId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (optionId >= kWideIdBase && WideStateFor(kVanillaSpace_Option, optionId))
    {
        buf = WideRowFor(g_Option, optionId, writeId);
        wideRow = true;
    }
    else if (optionId > g_Option.maxId)
    {
        LogWarn("SetOption: optionId=%d out of range 1..%d, not written",
            optionId, g_Option.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Option);
    if (!buf)
        return 0;

    const bool vanillaRow = !wideRow && optionId <= g_Option.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1);
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Option, optionId, rowPtr, 1);

    WriteOptionSEH(buf, writeId, isLight, isLaser);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Option, optionId, rowPtr, 1);
    if (wideRow)
        SyncWideRowToAlias(g_Option, optionId);

    return 0;
}

int __cdecl l_SetUnderBarrel(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetUnderBarrel: argument #1 must be a table");
        return 0;
    }

    int underBarrelId = 0;
    if (!ReadNamedInt(L, 1, "underBarrelId", underBarrelId) || underBarrelId <= 0)
    {
        LogWarn("SetUnderBarrel: missing or invalid underBarrelId");
        return 0;
    }

    int receiverId = 0, magazineId = 0, underBarrelGrade = 0, motionFrom = 0;
    const bool hasReceiver = ReadNamedInt(L, 1, "receiverId", receiverId);
    ReadNamedInt(L, 1, "magazineId", magazineId);
    ReadNamedInt(L, 1, "underBarrelGrade", underBarrelGrade);
    const bool hasMotionFrom = ReadNamedInt(L, 1, "motionFrom", motionFrom);

    if (!hasReceiver || receiverId <= 0)
    {
        LogWarn("SetUnderBarrel underBarrelId=%d: no receiverId, rejected", underBarrelId);
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_UnderBarrel))
    {
        LogError("SetUnderBarrel underBarrelId=%d: under-barrel table unavailable on this build, skipped",
            underBarrelId);
        return 0;
    }
    receiverId = ResolvePartByteLocked(kVanillaSpace_Receiver, receiverId);
    const int logicalMagazineId = magazineId;
    magazineId = ResolvePartByteLocked(kVanillaSpace_Magazine, magazineId);
    if (receiverId <= 0 || (logicalMagazineId > 0 && magazineId <= 0))
    {
        LogWarn("SetUnderBarrel underBarrelId=%d: receiver/magazine got no engine lane, rejected",
            underBarrelId);
        return 0;
    }

    int writeId = underBarrelId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (underBarrelId >= kWideIdBase && WideStateFor(kVanillaSpace_UnderBarrel, underBarrelId))
    {
        buf = WideRowFor(g_UnderBarrel, underBarrelId, writeId);
        wideRow = true;
    }
    else if (underBarrelId > g_UnderBarrel.maxId)
    {
        LogWarn("SetUnderBarrel: underBarrelId=%d out of range 1..%d, not written",
            underBarrelId, g_UnderBarrel.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_UnderBarrel);
    if (!buf)
        return 0;

    const bool vanillaRow = !wideRow && underBarrelId <= g_UnderBarrel.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 3;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_UnderBarrel, underBarrelId, rowPtr, 3);

    WriteUnderBarrelSEH(buf, writeId, receiverId, magazineId, underBarrelGrade);
    g_UnderBarrelMagazine[underBarrelId] = logicalMagazineId;

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_UnderBarrel, underBarrelId, rowPtr, 3);
    if (wideRow)
        SyncWideRowToAlias(g_UnderBarrel, underBarrelId);

    EnsureUbTypeExt();
    if (g_UbTypeExtReady && underBarrelId > g_UnderBarrel.stockCount)
    {
        int ubType = 0;

        if (hasMotionFrom && motionFrom > 0 && motionFrom < kPartTypeExtCount)
            ubType = g_UbTypeExt[motionFrom];
        else
        {
            EnsureRcvTypeExt();
            const int wantRcType = (g_RcvTypeExtReady && receiverId > 0
                                    && receiverId < kPartTypeExtCount)
                ? g_RcvTypeExt[receiverId] : 0;
            if (wantRcType != 0)
            {
                for (int id = 1; id <= g_UnderBarrel.stockCount; ++id)
                {
                    const int rc = PartRowByte(g_UnderBarrel, id, 0);
                    if (rc <= 0 || rc >= 256)
                        continue;
                    if (g_RcvTypeExt[rc] != wantRcType)
                        continue;
                    if (g_UbTypeExt[id] == 0)
                        continue;
                    ubType = g_UbTypeExt[id];
                    break;
                }
            }
        }

        bool typeApplied = false;
        if (ubType != 0)
        {
            if (wideRow)
            {
                WidePartState* w = WideStateFor(kVanillaSpace_UnderBarrel, underBarrelId);
                if (w)
                {
                    w->pendingUbType = ubType;
                    typeApplied = w->alias ? WriteUnderBarrelType(w->alias, ubType)
                                           : true;
                }
            }
            else
                typeApplied = WriteUnderBarrelType(underBarrelId, ubType);
        }
        if (ubType == 0 || !typeApplied)
            LogWarn("SetUnderBarrel underBarrelId=%d: no anim type, no motion; set motionFrom",
                underBarrelId);
    }

    return 0;
}

int __cdecl l_SetBarrel(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetBarrel: argument #1 must be a table");
        return 0;
    }

    int barrelId = 0;
    if (!ReadNamedInt(L, 1, "barrelId", barrelId) || barrelId <= 0)
    {
        LogWarn("SetBarrel: missing or invalid barrelId");
        return 0;
    }

    int base = 0, barrelLength = 0, scopeMount = 0, unkFlag = 0, sideMount = 0, underMount = 0;
    double baseCurve[7] = { 0 };
    int baseCurveN = 0, baseNum = 0;
    int baseKind = ReadNumberOrFloatTable(L, 1, "barrelParamSetsBase", baseNum,
                                          baseCurve, 7, baseCurveN,
                                          kBarrelBaseNames);
    if (baseKind == 1)
        base = baseNum;
    ReadNamedInt(L, 1, "barrelLength", barrelLength);
    ReadNamedInt(L, 1, "hasScopeMount", scopeMount);
    ReadNamedInt(L, 1, "unk2", unkFlag);
    ReadNamedInt(L, 1, "hasSideMount", sideMount);
    ReadNamedInt(L, 1, "hasUnderMount", underMount);

    if (baseKind == 1 && (base < 0 || base > 255))
        LogWarn("SetBarrel barrelId=%d: base=%d out of 0..255, masked", barrelId, base);
    if (barrelLength < 0 || barrelLength > 15)
        LogWarn("SetBarrel barrelId=%d: barrelLength=%d out of 0..15, masked", barrelId, barrelLength);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Barrel))
    {
        LogError("SetBarrel barrelId=%d: barrel table unavailable on this build, skipped", barrelId);
        return 0;
    }
    int writeId = barrelId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (barrelId >= kWideIdBase && WideStateFor(kVanillaSpace_Barrel, barrelId))
    {
        buf = WideRowFor(g_Barrel, barrelId, writeId);
        wideRow = true;
    }
    else if (barrelId > g_Barrel.maxId)
    {
        LogWarn("SetBarrel: barrelId=%d out of range 1..%d, not written",
            barrelId, g_Barrel.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Barrel);
    if (!buf)
        return 0;

    if (baseKind == 2)
    {
        BarrelExtraMult ex;
        bool wantExtra = false;
        static const int kEngineMaxFields[3] = { 0, 4, 5 };
        static const int kIdentityFields[2]  = { 2, 3 };
        for (int f = 0; f < 3; ++f)
        {
            const int k = kEngineMaxFields[f];
            const double v = baseCurve[k];
            if (!(v >= 0.0) || v > 1.0e6)
            {
                baseCurve[k] = 0;
                LogWarn("SetBarrel barrelId=%d: barrelParamSetsBase[%d] invalid, zeroed", barrelId, k + 1);
                continue;
            }
            if (v > 2.555)
            {
                ex.rem[k] = v / 2.55;
                baseCurve[k] = 2.55;
                wantExtra = true;
            }
        }
        for (int f = 0; f < 2; ++f)
        {
            const int k = kIdentityFields[f];
            const double v = baseCurve[k];
            if (!(v >= 0.0) || v > 1.0e6)
            {
                baseCurve[k] = 0;
                LogWarn("SetBarrel barrelId=%d: barrelParamSetsBase[%d] invalid, zeroed", barrelId, k + 1);
                continue;
            }
            if (v > 2.555)
            {
                ex.rem[k] = v;
                baseCurve[k] = 1.0;
                wantExtra = true;
            }
        }
        if (baseCurve[1] > 2.555)
            LogWarn("SetBarrel barrelId=%d: unk2 clamped to 2.55", barrelId);
        if (baseCurve[6] > 2.555)
            LogWarn("SetBarrel barrelId=%d: percentOverride clamped to 2.55", barrelId);

        const int idx = AllocateRefPoolRow(g_BarrelBase);
        if (idx >= 0)
        {
            std::uint8_t* pool = PartCurrentBuf(g_BarrelBase.pb);
            if (pool)
            {
                WriteBarrelBaseRowSEH(pool, idx, baseCurve, baseCurveN);
                if (idx > kPoolDirectMax)
                {
                    g_BarrelOverflow[barrelId] = idx;
                    base = 0;
                    if (barrelId <= g_Barrel.stockCount)
                        EquipParam_VanillaForceTaint(kVanillaSpace_Barrel, barrelId,
                            "overflow curve rows the pre-image diff cannot see");
                }
                else
                {
                    g_BarrelOverflow.erase(barrelId);
                    base = idx;
                }
                if (wantExtra)
                {
                    g_BarrelExtraMult[barrelId] = ex;
                    if (barrelId <= g_Barrel.stockCount)
                        EquipParam_VanillaForceTaint(kVanillaSpace_Barrel, barrelId,
                            "above-ceiling multipliers the pre-image diff cannot see");
                }
                else
                {
                    g_BarrelExtraMult.erase(barrelId);
                }
            }
        }
        else
        {
            g_BarrelExtraMult.erase(barrelId);
            LogWarn("SetBarrel barrelId=%d: barrelParamSetsBase curve not added, dropped", barrelId);
        }
    }
    else
    {
        g_BarrelOverflow.erase(barrelId);
        g_BarrelExtraMult.erase(barrelId);
    }

    const bool vanillaRow = !wideRow && barrelId <= g_Barrel.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 2;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Barrel, barrelId, rowPtr, 2);

    WriteBarrelSEH(buf, writeId, base, barrelLength, scopeMount, unkFlag, sideMount, underMount);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Barrel, barrelId, rowPtr, 2);
    if (wideRow)
        SyncWideRowToAlias(g_Barrel, barrelId);

    return 0;
}

int __cdecl l_SetMagazine(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetMagazine: argument #1 must be a table");
        return 0;
    }

    int ammoId = 0;
    if (!ReadNamedInt(L, 1, "ammoId", ammoId) || ammoId <= 0)
    {
        LogWarn("SetMagazine: missing or invalid ammoId");
        return 0;
    }

    int eqpAmmoId = 0;
    int capacity = 0;
    int totalCarry = 0;
    int bulletId = 0;
    int magazineType = 0;
    g_lua_getfield(L, 1, const_cast<char*>("magazineType"));
    const int magazineTypeLuaType = g_lua_type(L, -1);
    if (magazineTypeLuaType == LUA_TNUMBER)
        magazineType = static_cast<int>(g_lua_tointeger(L, -1));
    g_lua_settop(L, -2);
    if (magazineTypeLuaType != LUA_TNIL && magazineTypeLuaType != LUA_TNUMBER)
        LogWarn("SetMagazine ammoId=%d: magazineType is not an integer, ignored", ammoId);
    ReadNamedInt(L, 1, "equipAmmoId", eqpAmmoId);
    ReadNamedInt(L, 1, "capacity", capacity);
    ReadNamedInt(L, 1, "totalCarry", totalCarry);
    ReadNamedInt(L, 1, "bulletId", bulletId);

    if (capacity < 0 || capacity > 0x3FF)
        LogWarn("SetMagazine ammoId=%d: capacity=%d out of 0..1023, clamped", ammoId, capacity);
    if (totalCarry > 0x3FFF)
        LogWarn("SetMagazine ammoId=%d: totalCarry=%d over 16383, clamped", ammoId, totalCarry);
    if (eqpAmmoId < 0 || eqpAmmoId > 0xFFFF)
        LogWarn("SetMagazine ammoId=%d: equipAmmoId=%d out of 0..65535, wraps to %d",
            ammoId, eqpAmmoId, eqpAmmoId & 0xFFFF);
    if (bulletId < 0 || bulletId > 0xFF)
        LogWarn("SetMagazine ammoId=%d: bulletId=%d out of 0..255, wraps to %d",
            ammoId, bulletId, bulletId & 0xFF);

    std::unique_lock<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Magazine))
    {
        LogError("SetMagazine ammoId=%d: magazine table unavailable on this build, skipped", ammoId);
        return 0;
    }
    int writeId = ammoId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (ammoId >= kWideIdBase && WideStateFor(kVanillaSpace_Magazine, ammoId))
    {
        const WidePartState* existing = WideStateFor(kVanillaSpace_Magazine, ammoId);
        if (existing->rowSet && existing->row.size() == 8
            && (existing->magazineDonor > 0 || g_SharedMagazineLanes.count(existing->alias))
            && ((!existing->looseDonor && existing->row[6] != static_cast<std::uint8_t>(bulletId & 0xFF))
                || (magazineType > 0 && magazineType != existing->motionType)))
        {
            LogWarn("SetMagazine ammoId=%d: bullet/motion already bound, rejected until restart", ammoId);
            return 0;
        }
        buf = WideRowFor(g_Magazine, ammoId, writeId);
        wideRow = true;
    }
    else if (ammoId > g_Magazine.maxId)
    {
        LogWarn("SetMagazine: ammoId=%d out of range 1..%d, not written",
            ammoId, g_Magazine.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Magazine);
    if (!buf)
        return 0;

    const bool bulletLockedLane = g_SharedMagazineLanes.count(ammoId) != 0;
    if (!wideRow && (bulletLockedLane || g_LooseMagazineLanes.count(ammoId))
        && ((bulletLockedLane
             && buf[static_cast<size_t>(ammoId - 1) * 8 + 6] != static_cast<std::uint8_t>(bulletId & 0xFF))
            || (magazineType > 0 && g_MagazineTypeExt[ammoId] != magazineType)))
    {
        LogWarn("SetMagazine ammoId=%d: bullet/motion shared by wide magazines, rejected until restart", ammoId);
        return 0;
    }

    const bool vanillaRow = !wideRow && ammoId <= g_Magazine.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 8;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Magazine, ammoId, rowPtr, 8);

    WriteMagazineSEH(buf, writeId, eqpAmmoId, capacity, totalCarry, bulletId);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Magazine, ammoId, rowPtr, 8);
    if (wideRow)
        SyncWideRowToAlias(g_Magazine, ammoId);

    HealSupplyAmmoCountTable();

    if (magazineType != 0)
    {
        if (!IsVanillaMagazineType(magazineType))
            LogWarn("SetMagazine ammoId=%d: magazineType=%d is not a vanilla type, ignored",
                ammoId, magazineType);
        else if (wideRow)
        {
            WidePartState* w = WideStateFor(kVanillaSpace_Magazine, ammoId);
            if (w)
            {
                w->motionType = magazineType;
                WriteDeclaredMagazineType(ammoId, magazineType);
                if (w->alias && !WriteDeclaredMagazineType(w->alias, magazineType))
                    LogError("SetMagazine ammoId=%d: magazine type table unavailable, magazineType ignored", ammoId);
            }
        }
        else if (!WriteDeclaredMagazineType(ammoId, magazineType))
            LogError("SetMagazine ammoId=%d: magazine type table unavailable, magazineType ignored", ammoId);
    }

    if (vanillaRow && ammoId < kPartTypeExtCount)
    {
        EnsurePartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                          gAddr.MotionLoaderImpl_MagazineTypeTable,
                          kMagazineTypeVanillaRows);
        const auto* retailTable = static_cast<const std::uint8_t*>(
            ResolveGameAddress(gAddr.MotionLoaderImpl_MagazineTypeTable));
        if (g_MagazineTypeExtReady && retailTable)
        {
            const int retailType = ammoId < kMagazineTypeVanillaRows
                ? ReadVanillaPartTypeSEH(retailTable, ammoId) : 0;
            if (retailType != static_cast<int>(g_MagazineTypeExt[ammoId]))
                EquipParam_VanillaForceTaint(kVanillaSpace_Magazine, ammoId,
                                             "a different magazine motion type");
        }
    }

    if (eqpAmmoId > 0)
    {
        const int nativeAmmoId = ResolvePartByteLocked(kVanillaSpace_Magazine, ammoId);
        const int rootAmmoId = WideMagazinesPlanned() ? ammoId : nativeAmmoId;
        if (nativeAmmoId > 0)
            TppEquip_NoteAmmoRootParam(eqpAmmoId, rootAmmoId, nativeAmmoId);
    }

    lock.unlock();
    if (wideRow)
        GunBasic_ReplayPendingForMagazine(ammoId);

    return 0;
}

int __cdecl l_SetBullet(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetBullet: argument #1 must be a table");
        return 0;
    }

    int bulletId = 0;
    if (!ReadNamedInt(L, 1, "bulletId", bulletId) || bulletId <= 0)
    {
        LogWarn("SetBullet: missing or invalid bulletId");
        return 0;
    }

    int u16v[3] = { 0, 0, 0 };
    int u8v[7]  = { 0, 0, 0, 0, 0, 0, 0 };
    int unk11 = 0, eqpType = 0;
    ReadNamedInt(L, 1, "bulletSpeed", u16v[0]);
    ReadNamedIntAlias(L, 1, "npcBulletSpeed", "unk02", u16v[1]);
    ReadNamedInt(L, 1, "dropRate", u16v[2]);
    double bbCurve[12] = { 0 };
    int bbCurveN = 0, bbNum = 0;
    const int bbKind = ReadNumberOrFloatTable(L, 1, "bulletParamSetsBase", bbNum,
                                              bbCurve, 12, bbCurveN,
                                              kBulletBaseNames);
    if (bbKind == 1)
        u8v[0] = bbNum;
    double npcCurve[12] = { 0 };
    int npcCurveN = 0, npcNum = 0;
    int npcKind = ReadNumberOrFloatTable(L, 1, "npcBulletParamSetsBase", npcNum,
                                         npcCurve, 12, npcCurveN, kBulletBaseNames);
    if (npcKind == 0)
        npcKind = ReadNumberOrFloatTable(L, 1, "unk07", npcNum,
                                         npcCurve, 12, npcCurveN, kBulletBaseNames);
    if (npcKind == 1)
        u8v[1] = npcNum;
    if (!ReadNamedInt(L, 1, "bulletTrailEffect", u8v[2]))
    {
        const std::string trailPath = ReadNamedStringOrTable1(L, 1, "bulletTrailEffect");
        if (!trailPath.empty())
        {
            const int idx = ResolveTrailPathIndex(FoxHashes::PathCode64Ext(trailPath));
            if (idx >= 0 && idx <= 255)
                u8v[2] = idx;
            else
                LogWarn("SetBullet bulletId=%d: bulletTrailEffect '%s' not registered, ignored",
                    bulletId, trailPath.c_str());
        }
    }
    ReadNamedInt(L, 1, "unk09", u8v[3]);
    ReadNamedInt(L, 1, "bulletType",   u8v[4]);
    ReadNamedInt(L, 1, "ricochetSize", u8v[5]);
    ReadNamedInt(L, 1, "blastId", u8v[6]);
    ReadNamedIntAlias(L, 1, "isLethal", "unk11", unk11);
    ReadNamedInt(L, 1, "eqpType", eqpType);

    auto warnWidth = [&](const char* name, int v, int maxV)
    {
        if (v < 0 || v > maxV)
            LogWarn("SetBullet bulletId=%d: %s=%d out of 0..%d, wraps to %d",
                bulletId, name, v, maxV, v & maxV);
    };
    warnWidth("bulletSpeed",    u16v[0], 0xFFFF);
    warnWidth("npcBulletSpeed", u16v[1], 0xFFFF);
    warnWidth("dropRate",       u16v[2], 0xFFFF);
    if (bbKind == 1)  warnWidth("bulletParamSetsBase",    u8v[0], 0xFF);
    if (npcKind == 1) warnWidth("npcBulletParamSetsBase", u8v[1], 0xFF);
    warnWidth("bulletTrailEffect", u8v[2], 0xFF);
    warnWidth("unk09",        u8v[3], 0xFF);
    warnWidth("bulletType",   u8v[4], 0xFF);
    warnWidth("ricochetSize", u8v[5], 0xFF);
    warnWidth("blastId",      u8v[6], 0xFF);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Bullet))
    {
        LogError("SetBullet bulletId=%d: bullet table unavailable on this build, skipped", bulletId);
        return 0;
    }
    if (bulletId > g_Bullet.maxId)
    {
        LogWarn("SetBullet: bulletId=%d out of range 1..%d, not written",
            bulletId, g_Bullet.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Bullet);
    if (!buf)
        return 0;

    if (bbKind == 2)
    {
        const int idx = AllocateRefPoolRow(g_BulletBase);
        if (idx >= 0)
        {
            std::uint8_t* pool = PartCurrentBuf(g_BulletBase.pb);
            if (pool)
            {
                WriteBulletBaseRowSEH(pool, idx, bbCurve, bbCurveN);
                if (idx > kPoolDirectMax)
                {
                    g_BulletBaseOverflow[bulletId] = idx;
                    u8v[0] = 0;
                    if (bulletId <= g_Bullet.stockCount)
                        EquipParam_VanillaForceTaint(kVanillaSpace_Bullet, bulletId,
                            "overflow falloff rows the pre-image diff cannot see");
                }
                else
                {
                    g_BulletBaseOverflow.erase(bulletId);
                    u8v[0] = idx;
                }
            }
        }
        else
        {
            LogWarn("SetBullet bulletId=%d: bulletParamSetsBase curve not added, dropped", bulletId);
        }
    }
    else
    {
        g_BulletBaseOverflow.erase(bulletId);
    }

    if (npcKind == 2)
    {
        const int idx = AllocateRefPoolRow(g_BulletBase);
        if (idx >= 0)
        {
            std::uint8_t* pool = PartCurrentBuf(g_BulletBase.pb);
            if (pool)
            {
                WriteBulletBaseRowSEH(pool, idx, npcCurve, npcCurveN);
                if (idx > kPoolDirectMax)
                {
                    u8v[1] = 0;
                    LogWarn("SetBullet bulletId=%d: npcBulletParamSetsBase row %d past 253, using curve 0",
                        bulletId, idx);
                }
                else
                {
                    u8v[1] = idx;
                }
            }
        }
        else
        {
            LogWarn("SetBullet bulletId=%d: npcBulletParamSetsBase curve not added, dropped", bulletId);
        }
    }

    const bool vanillaRow = bulletId <= g_Bullet.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(bulletId - 1) * 14;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Bullet, bulletId, rowPtr, 14);

    WriteBulletRowSEH(buf, bulletId, u16v, u8v, unk11, eqpType);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Bullet, bulletId, rowPtr, 14);

    bool lockOnGiven = false;
    int lockBulletType = -1;
    int lockAmmoPerShot = 0;
    g_lua_getfield(L, 1, const_cast<char*>("lockOn"));
    if (g_lua_type(L, -1) == LUA_TTABLE)
    {
        lockOnGiven = true;
        const int lockTbl = g_lua_gettop(L);
        int count = 1;
        double timeSec = 1.0, turnDeg = 90.0, lockedSpeed = 0.0;
        double minRange = 0.0, maxRange = 0.0, homingStart = 0.0;
        ReadNamedInt(L, lockTbl, "count", count);
        ReadNamedFloat(L, lockTbl, "time", timeSec);
        ReadNamedFloat(L, lockTbl, "turnRate", turnDeg);
        ReadNamedFloat(L, lockTbl, "minRange", minRange);
        ReadNamedFloat(L, lockTbl, "maxRange", maxRange);
        ReadNamedFloat(L, lockTbl, "bulletSpeed", lockedSpeed);
        ReadNamedFloat(L, lockTbl, "homingStartDistance", homingStart);
        ReadNamedInt(L, lockTbl, "bulletType", lockBulletType);
        ReadNamedInt(L, lockTbl, "ammoPerShot", lockAmmoPerShot);
        equip::LockOnCategories cats{};
        cats.soldiers = ReadNamedBoolTri(L, lockTbl, "canLockOnSoldier");
        cats.vehicles = ReadNamedBoolTri(L, lockTbl, "canLockOnVehicle");
        equip::LockOn_RegisterBullet(bulletId, count, timeSec, turnDeg,
                                     minRange, maxRange, lockedSpeed,
                                     static_cast<double>(u16v[0]),
                                     homingStart, &cats);
        if (lockBulletType >= 0)
            equip::LockOn_RegisterBulletType(bulletId, lockBulletType, u8v[4]);
    }
    g_lua_settop(L, -2);

    int ammoPerShot = 0;
    ReadNamedInt(L, 1, "ammoPerShot", ammoPerShot);
    if (ammoPerShot > 1 || lockAmmoPerShot > 1)
        equip::MultiShot_RegisterBullet(bulletId, ammoPerShot, lockAmmoPerShot);

    if (vanillaRow && lockOnGiven)
        EquipParam_VanillaForceTaint(kVanillaSpace_Bullet, bulletId, "lockOn homing");
    if (vanillaRow && (ammoPerShot > 1 || lockAmmoPerShot > 1))
        EquipParam_VanillaForceTaint(kVanillaSpace_Bullet, bulletId, "multi-shot ammoPerShot");

    if (u8v[4] == kBulletTypeShell && u8v[6] == 0)
        LogWarn("SetBullet bulletId=%d: BULLET_TYPE_SHELL with blastId=0 never detonates", bulletId);

    return 0;
}

int __cdecl l_SetStock(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetStock: argument #1 must be a table");
        return 0;
    }

    int stockId = 0;
    if (!ReadNamedInt(L, 1, "stockId", stockId) || stockId <= 0)
    {
        LogWarn("SetStock: missing or invalid stockId");
        return 0;
    }

    double spreadRecovery = 1.0;
    double movementSway = 1.0;
    ReadNamedFloat(L, 1, "spreadRecovery", spreadRecovery);
    ReadNamedFloat(L, 1, "movementSway", movementSway);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Stock))
    {
        LogError("SetStock stockId=%d: stock table unavailable on this build, skipped", stockId);
        return 0;
    }
    int writeId = stockId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (stockId >= kWideIdBase && WideStateFor(kVanillaSpace_Stock, stockId))
    {
        buf = WideRowFor(g_Stock, stockId, writeId);
        wideRow = true;
    }
    else if (stockId > g_Stock.maxId)
    {
        LogWarn("SetStock: stockId=%d out of range 1..%d, not written",
            stockId, g_Stock.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Stock);
    if (!buf)
        return 0;

    const bool vanillaRow = !wideRow && stockId <= g_Stock.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 2;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Stock, stockId, rowPtr, 2);

    WriteStockSEH(buf, writeId, ScaleByte(spreadRecovery, 100.0), ScaleByte(movementSway, 100.0));

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Stock, stockId, rowPtr, 2);
    if (wideRow)
        SyncWideRowToAlias(g_Stock, stockId);

    return 0;
}

int __cdecl l_SetMuzzle(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetMuzzle: argument #1 must be a table");
        return 0;
    }

    int muzzleOptionId = 0;
    if (!ReadNamedInt(L, 1, "muzzleOptionId", muzzleOptionId) || muzzleOptionId <= 0)
    {
        LogWarn("SetMuzzle: missing or invalid muzzleOptionId");
        return 0;
    }

    double grouping = 1.0;
    int durability = -1;
    int suppressor = 0;
    ReadNamedFloat(L, 1, "grouping", grouping);
    ReadNamedInt(L, 1, "durability", durability);
    ReadNamedInt(L, 1, "suppressor", suppressor);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Muzzle))
    {
        LogError("SetMuzzle muzzleOptionId=%d: muzzle table unavailable on this build, skipped",
            muzzleOptionId);
        return 0;
    }
    int writeId = muzzleOptionId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (muzzleOptionId >= kWideIdBase && WideStateFor(kVanillaSpace_MuzzleOption, muzzleOptionId))
    {
        buf = WideRowFor(g_Muzzle, muzzleOptionId, writeId);
        wideRow = true;
    }
    else if (muzzleOptionId > g_Muzzle.maxId)
    {
        LogWarn("SetMuzzle: muzzleOptionId=%d out of range 1..%d, not written",
            muzzleOptionId, g_Muzzle.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Muzzle);
    if (!buf)
        return 0;

    const bool vanillaRow = !wideRow && muzzleOptionId <= g_Muzzle.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 3;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_MuzzleOption, muzzleOptionId, rowPtr, 3);

    WriteMuzzleSEH(buf, writeId, ScaleByte(grouping, 100.0), durability, suppressor);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_MuzzleOption, muzzleOptionId, rowPtr, 3);
    if (wideRow)
        SyncWideRowToAlias(g_Muzzle, muzzleOptionId);

    return 0;
}

int __cdecl l_SetSight(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetSight: argument #1 must be a table");
        return 0;
    }

    int scopeId = 0;
    if (!ReadNamedInt(L, 1, "scopeId", scopeId) || scopeId <= 0)
    {
        LogWarn("SetSight: missing or invalid scopeId");
        return 0;
    }

    int zoom1 = 0, zoom2 = 0, zoom3 = 0, scopeUiId = 0;
    int booster = 0, nvg = 0, builtIn = 0, rangeFinder = 0, rangeFinderBulletDrop = 0;
    ReadNamedInt(L, 1, "zoom1", zoom1);
    ReadNamedInt(L, 1, "zoom2", zoom2);
    ReadNamedInt(L, 1, "zoom3", zoom3);
    ReadNamedInt(L, 1, "scopeUiId", scopeUiId);
    ReadNamedInt(L, 1, "booster", booster);
    ReadNamedInt(L, 1, "nvg", nvg);
    ReadNamedInt(L, 1, "builtIn", builtIn);
    ReadNamedInt(L, 1, "rangeFinder", rangeFinder);
    ReadNamedInt(L, 1, "rangeFinderBulletDrop", rangeFinderBulletDrop);

    std::string factoryName, scopeUiPath;
    auto readUiString = [&](const char* name, std::string& value)
    {
        g_lua_getfield(L, 1, const_cast<char*>(name));
        const int type = g_lua_type(L, -1);
        bool valid = type == LUA_TNIL;
        if (type == LUA_TSTRING)
        {
            size_t length = 0;
            const char* text = g_lua_tolstring(L, -1, &length);
            valid = text != nullptr;
            if (text)
                value.assign(text, length);
        }
        g_lua_settop(L, -2);
        if (!valid)
            LogWarn("SetSight scopeId=%d: %s must be a string, skipped", scopeId, name);
        return valid;
    };
    if (!readUiString("windowFactoryName", factoryName)
        || !readUiString("scopeUiPath", scopeUiPath))
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Sight))
    {
        LogError("SetSight scopeId=%d: sight table unavailable on this build, skipped", scopeId);
        return 0;
    }
    int writeId = scopeId;
    bool wideRow = false;
    std::uint8_t* buf = nullptr;
    if (scopeId >= kWideIdBase && WideStateFor(kVanillaSpace_Sight, scopeId))
    {
        buf = WideRowFor(g_Sight, scopeId, writeId);
        wideRow = true;
    }
    else if (scopeId > g_Sight.maxId)
    {
        LogWarn("SetSight: scopeId=%d out of range 1..%d, not written",
            scopeId, g_Sight.maxId);
        return 0;
    }
    else
        buf = PartCurrentBuf(g_Sight);
    if (!buf)
        return 0;

    const int flags = (booster ? 0x01 : 0) | (nvg ? 0x02 : 0) | (builtIn ? 0x04 : 0)
                    | (rangeFinder ? 0x08 : 0) | (rangeFinderBulletDrop ? 0x10 : 0);
    if (!CustomScopeUi::Configure(scopeId, factoryName, scopeUiPath))
        return 0;
    const bool vanillaRow = !wideRow && scopeId <= g_Sight.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(writeId - 1) * 5;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Sight, scopeId, rowPtr, 5);

    WriteSightSEH(buf, writeId, Clamp(zoom1, 0, 255), Clamp(zoom2, 0, 255),
                  Clamp(zoom3, 0, 255), Clamp(scopeUiId, 0, 255), flags);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Sight, scopeId, rowPtr, 5);
    if (wideRow)
        SyncWideRowToAlias(g_Sight, scopeId);

    BindCustomSightScopeUi(scopeId, factoryName.empty() ? Clamp(scopeUiId, 0, 255) : 0,
                           Clamp(zoom1, 0, 255),
                           Clamp(zoom2, 0, 255), Clamp(zoom3, 0, 255));
    if (const WidePartState* state = WideStateFor(kVanillaSpace_Sight, scopeId))
        CustomScopeUi::BindAlias(scopeId, state->alias);

    return 0;
}

static const int kGunBasicScanMaxId  = 65535;
static const int kMinPlausibleRcvRefs = 32;

static bool BuildReferencedReceiverIds(std::set<int>& out)
{
    out.clear();
    for (int weaponId = 1; weaponId <= kGunBasicScanMaxId; ++weaponId)
    {
        unsigned char row[12] = {};
        if (!GunBasic_ReadRowBytes(weaponId, row))
            continue;
        if (row[0])
            out.insert(static_cast<int>(row[0]));
    }
    if (static_cast<int>(out.size()) < kMinPlausibleRcvRefs)
    {
        out.clear();
        return false;
    }
    return true;
}

static bool ReceiverIdHeldByName(int receiverId)
{
    for (const auto& named : g_RcvIdBuf.nameToId)
        if (named.second == receiverId)
            return true;
    return false;
}

static int CountFreeReceiverLaneSlots()
{
    const bool grown = EnsurePartShadow(g_RcvIdBuf);
    std::uint8_t* rbuf = grown ? PartCurrentBuf(g_RcvIdBuf) : ImplBufPtr(kReceiverImplOffset);
    if (!rbuf)
        return 0;
    const int cap = grown ? kReceiverMaxId : kReceiverCapacity;
    int freeCount = 0;
    for (int idx0 = cap - 1; idx0 >= 1; --idx0)
    {
        const int receiverId = idx0 + 1;
        if (receiverId == 0xD0 || receiverId == 0xE7)
            continue;
        if (g_RcvClaimed.count(receiverId) || ReceiverIdHeldByName(receiverId))
            continue;
        if (RcvRowIsZeroSEH(rbuf, idx0) != 1)
            continue;
        ++freeCount;
    }
    return freeCount;
}

static int AllocateReceiverLaneSlot()
{
    const bool grown = EnsurePartShadow(g_RcvIdBuf);
    std::uint8_t* rbuf = grown ? PartCurrentBuf(g_RcvIdBuf) : ImplBufPtr(kReceiverImplOffset);
    if (!rbuf)
        return 0;

    const int cap = grown ? kReceiverMaxId : kReceiverCapacity;
    for (int idx0 = cap - 1; idx0 >= 1; --idx0)
    {
        const int receiverId = idx0 + 1;
        if (receiverId == 0xD0 || receiverId == 0xE7)
            continue;
        if (g_RcvClaimed.count(receiverId) || ReceiverIdHeldByName(receiverId))
            continue;
        if (RcvRowIsZeroSEH(rbuf, idx0) != 1)
            continue;

        g_RcvClaimed.insert(receiverId);
        return receiverId;
    }

    std::set<int> referenced;
    if (!BuildReferencedReceiverIds(referenced))
        return 0;

    for (int idx0 = cap - 1; idx0 >= 1; --idx0)
    {
        const int receiverId = idx0 + 1;
        if (receiverId == 0xD0 || receiverId == 0xE7)
            continue;
        if (g_RcvClaimed.count(receiverId) || ReceiverIdHeldByName(receiverId))
            continue;
        if (referenced.count(receiverId))
            continue;
        if (RcvRowIsZeroSEH(rbuf, idx0) < 0)
            continue;

        g_RcvClaimed.insert(receiverId);
        return receiverId;
    }
    return 0;
}

int EquipParam_AllocateReceiverSlotForName(const char* name)
{
    if (!name || !name[0])
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    auto it = g_RcvNameToId.find(name);
    if (it != g_RcvNameToId.end())
        return it->second;

    EnsurePartIdWidenArmed();
    if (PartIdWiden_IsPlanned())
    {
        const int shadowId = AllocatePartSlot(g_RcvIdBuf, name);
        if (shadowId)
        {
            g_RcvNameToId[name] = shadowId;
            return shadowId;
        }
    }
    else
    {
        const int wideId = AllocateWidePartId(g_RcvIdBuf, name);
        if (wideId)
        {
            g_RcvNameToId[name] = wideId;
            return wideId;
        }
    }

    const int receiverId = AllocateReceiverLaneSlot();
    if (receiverId)
    {
        g_RcvNameToId[name] = receiverId;
        return receiverId;
    }

    LogError("EquipParam: no free receiver slot for '%s', receiver unavailable", name);
    return 0;
}

int __cdecl l_SetReceiver(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    ChimeraMotion_EnsureWrapInstalled(L);

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetReceiver: argument #1 must be a table");
        return 0;
    }

    int receiverId = 0;
    if (!ReadNamedInt(L, 1, "receiverId", receiverId) || receiverId <= 0)
    {
        LogWarn("SetReceiver: missing or invalid receiverId");
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    std::uint8_t* rbuf = ImplBufPtr(kReceiverImplOffset);
    if (!rbuf)
    {
        LogError("SetReceiver receiverId=%d: receiver table unavailable on this build, skipped", receiverId);
        return 0;
    }
    bool wideRow = false;
    int writeId = receiverId;
    std::uint8_t* rowBuf = rbuf;
    if (receiverId >= kWideIdBase && WideStateFor(kVanillaSpace_Receiver, receiverId))
    {
        EnsurePartShadow(g_RcvIdBuf);
        rowBuf = WideRowFor(g_RcvIdBuf, receiverId, writeId);
        if (!rowBuf)
            return 0;
        wideRow = true;
    }
    else if (receiverId > kReceiverMaxId)
    {
        if (!PartIdWiden_IsPlanned() || !EnsurePartShadow(g_RcvIdBuf)
            || receiverId > g_RcvIdBuf.maxId)
        {
            LogWarn("SetReceiver: receiverId=%d out of range 1..%d, not written",
                receiverId, PartIdWiden_IsPlanned() ? g_RcvIdBuf.maxId : kReceiverMaxId);
            return 0;
        }
        rowBuf = PartCurrentBuf(g_RcvIdBuf);
        if (!rowBuf)
        {
            LogWarn("SetReceiver receiverId=%d: grown receiver table unavailable, not written", receiverId);
            return 0;
        }
        rbuf = rowBuf;
    }

    int attackId = 0, motionFrom = 0;
    const bool hasAttack     = ReadNamedInt(L, 1, "attackId", attackId);
    const bool hasMotionFrom = ReadNamedInt(L, 1, "motionFrom", motionFrom);

    int base = ResolvePoolField(L, "receiverParamSetsBase", POOL_BASE);
    int wob = ResolvePoolField(L, "receiverParamSetsWobbling", POOL_WOB);
    int sys = ResolvePoolField(L, "receiverParamSetsSystem", POOL_SYS);
    int snd = ResolvePoolField(L, "receiverParamSetsSound", POOL_SND);

    if (base == -2 || wob == -2 || sys == -2 || snd == -2)
        return 0;
    if (base < 0 || wob < 0 || sys < 0 || snd < 0)
    {
        LogWarn("SetReceiver receiverId=%d: needs receiverParamSetsBase/Wobbling/System/Sound", receiverId);
        return 0;
    }

    {
        std::uint8_t* sndPool = PartCurrentBuf(g_Snd.pb);
        char existing[9] = {};
        if (sndPool && ReadSndRowSEH(sndPool, snd, existing) && existing[0] == '\0')
        {
            std::string donorRoot;
            if (hasMotionFrom && ResolveDonorSoundRoot(motionFrom, donorRoot))
            {
                char label[8] = {};
                for (int c = 0; c < 7 && donorRoot[c]; ++c)
                    label[c] = donorRoot[c];
                WriteSndRowSEH(sndPool, snd, label);
            }
            else
            {
                LogWarn("SetReceiver receiverId=%d: receiverParamSetsSound has no root, omitted side silent",
                    receiverId);
            }
        }
    }
    if (!hasAttack)
    {
        attackId = 0;
        LogWarn("SetReceiver receiverId=%d: no attackId, written as 0", receiverId);
    }

    {
        RcvOverflowRef of;
        of.idx[POOL_BASE] = base > kPoolDirectMax ? base : -1;
        of.idx[POOL_WOB]  = wob  > kPoolDirectMax ? wob  : -1;
        of.idx[POOL_SYS]  = sys  > kPoolDirectMax ? sys  : -1;
        of.idx[POOL_SND]  = snd  > kPoolDirectMax ? snd  : -1;
        if (of.idx[0] >= 0 || of.idx[1] >= 0 || of.idx[2] >= 0 || of.idx[3] >= 0)
        {
            g_RcvOverflow[receiverId] = of;
            if (receiverId <= kReceiverCapacity && !g_RcvClaimed.count(receiverId))
                EquipParam_VanillaForceTaint(kVanillaSpace_Receiver, receiverId,
                    "overflow pool rows the pre-image diff cannot see");
        }
        else
        {
            g_RcvOverflow.erase(receiverId);
        }
        if (base > kPoolDirectMax) base = 0;
        if (wob  > kPoolDirectMax) wob  = 0;
        if (sys  > kPoolDirectMax) sys  = 0;
        if (snd  > kPoolDirectMax) snd  = 0;
    }

    const bool vanillaRow = !wideRow && receiverId <= kReceiverCapacity
        && !g_RcvClaimed.count(receiverId);
    const std::uint8_t* rowPtr =
        rowBuf + static_cast<size_t>(writeId - 1) * kReceiverStride;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Receiver, receiverId, rowPtr,
                                   kReceiverStride);

    WriteReceiverRowSEH(rowBuf, writeId, attackId, base, wob, sys, snd);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Receiver, receiverId, rowPtr,
                                    kReceiverStride);
    if (wideRow)
        SyncWideRowToAlias(g_RcvIdBuf, receiverId);

    int motionType = -1;
    g_MotionFromReceiverType.erase(receiverId);
    if (hasMotionFrom && motionFrom > 0 && motionFrom < kPartTypeExtCount)
    {
        EnsureRcvTypeExt();
        if (g_RcvTypeExtReady)
        {
            motionType = g_RcvTypeExt[motionFrom];
            if (motionType == 0)
                LogWarn("SetReceiver receiverId=%d: motionFrom=%d has motion type 0, weapon invalid on draw",
                    receiverId, motionFrom);
            else if (wideRow)
            {
                WidePartState* w = WideStateFor(kVanillaSpace_Receiver, receiverId);
                if (w)
                {
                    w->motionFrom = motionFrom;
                    w->motionType = motionType;
                    if (w->alias)
                    {
                        ChimeraMotion_InheritFromMotionFrom(w->alias, motionFrom);
                        g_ReceiverMotionDonor[w->alias] = motionFrom;
                    }
                }
            }
            else
            {
                ChimeraMotion_InheritFromMotionFrom(receiverId, motionFrom);
                if (receiverId > 0)
                    g_ReceiverMotionDonor[receiverId] = motionFrom;
            }
            GunBasic_ReNarrowReceiverRows(receiverId);
        }
    }

    if (motionType > 0)
        g_MotionFromReceiverType[receiverId] = motionType;
    {
        const auto itDecl = g_DeclaredReceiverType.find(receiverId);
        if (itDecl != g_DeclaredReceiverType.end())
        {
            if (motionType > 0 && motionType != itDecl->second)
                LogReceiverTypeConflict(receiverId, itDecl->second, motionType);
            motionType = itDecl->second;
            GunBasic_ReNarrowReceiverRows(receiverId);
        }
    }

    if (motionType < 0)
    {
        motionType = 0;
        g_ReceiverWrittenUntyped.insert(receiverId);
    }
    else
    {
        g_ReceiverWrittenUntyped.erase(receiverId);
    }
    if (wideRow)
    {
        WidePartState* w = WideStateFor(kVanillaSpace_Receiver, receiverId);
        if (w)
        {
            w->motionType = motionType;
            if (w->alias && !WriteReceiverType(w->alias, motionType))
                LogError("SetReceiver receiverId=%d: receiver type table unavailable, motion type not set",
                    receiverId);
        }
    }
    else
    {
        EnsureRcvTypeExt();
        const int prevType = (g_RcvTypeExtReady && receiverId > 0
                              && receiverId < kPartTypeExtCount)
            ? g_RcvTypeExt[receiverId] : -1;
        if (!WriteReceiverType(receiverId, motionType))
        {
            LogError("SetReceiver receiverId=%d: receiver type table unavailable, motion type not set",
                receiverId);
        }
        else if (vanillaRow && prevType >= 0 && motionType != prevType)
        {
            EquipParam_VanillaForceTaint(kVanillaSpace_Receiver, receiverId,
                                         "a different motion/animation type");
        }
    }

    return 0;
}

namespace
{
    static PartBuffer* SpaceBuffer(int space)
    {
        switch (space)
        {
        case kVanillaSpace_Receiver:     return &g_RcvIdBuf;
        case kVanillaSpace_Barrel:       return &g_Barrel;
        case kVanillaSpace_Magazine:     return &g_Magazine;
        case kVanillaSpace_Stock:        return &g_Stock;
        case kVanillaSpace_MuzzleOption: return &g_Muzzle;
        case kVanillaSpace_Sight:        return &g_Sight;
        case kVanillaSpace_UnderBarrel:  return &g_UnderBarrel;
        case kVanillaSpace_Option:       return &g_Option;
        default:                         return nullptr;
        }
    }

    static int AllocateAliasByteLocked(PartBuffer& pb)
    {
        if (pb.space < 0 || pb.space >= kVanillaSpace_Count)
            return 0;
        for (int cand = pb.stockCount + 1; cand < kWideIdBase; ++cand)
        {
            if (PartIdTakenThisSession(pb, cand)
                || pb.persistReserved.count(cand) != 0)
                continue;
            bool usedAsAlias = false;
            for (const auto& kv : g_WideParts[pb.space])
                if (kv.second.alias == cand) { usedAsAlias = true; break; }
            if (usedAsAlias)
                continue;
            return cand;
        }
        return 0;
    }

    static void CollectMagazineDonorLanesLocked(bool (&lanes)[256])
    {
        for (int donor = 1; donor <= g_Magazine.stockCount; ++donor)
            lanes[donor] = true;
        for (const auto& named : g_Magazine.nameToId)
            if (named.second > g_Magazine.stockCount && named.second < 256)
                lanes[named.second] = true;
        for (const auto& entry : g_WideParts[kVanillaSpace_Magazine])
            if (entry.second.alias > 0 && entry.second.alias < 256)
                lanes[entry.second.alias] = true;
    }

    static int BindLooseMagazineLaneLocked(WidePartState& w, int id)
    {
        if (!WideMagazinesPlanned() || !w.rowSet || w.row.size() != 8)
            return 0;
        EnsurePartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                          gAddr.MotionLoaderImpl_MagazineTypeTable,
                          kMagazineTypeVanillaRows);
        const auto* rows = (EnsurePartShadow(g_Magazine) && g_MagazineTypeExtReady)
            ? PartCurrentBuf(g_Magazine) : nullptr;
        if (!rows)
            return 0;
        bool lanes[256]{};
        CollectMagazineDonorLanesLocked(lanes);
        int sameType = 0;
        int anyType = 0;
        for (int donor = 1; donor <= 255; ++donor)
        {
            if (!lanes[donor] || rows[static_cast<size_t>(donor - 1) * 8 + 6] == 0)
                continue;
            const int laneType = g_MagazineTypeExt[donor];
            if (laneType == (w.motionType > 0 ? w.motionType : 0))
            {
                sameType = donor;
                break;
            }
            if (w.motionType <= 0 && !anyType)
                anyType = donor;
        }
        const int donor = sameType ? sameType : anyType;
        if (!donor)
            return 0;
        if (!sameType)
        {
            w.motionType = g_MagazineTypeExt[donor];
            LogWarn("EquipParam: magazine id %d reloads with magazine type %d (lane %d); set magazineType to pick one",
                id, w.motionType, donor);
        }
        w.magazineDonor = donor;
        w.looseDonor = true;
        g_LooseMagazineLanes.insert(donor);
        LogInfo("EquipParam: magazine id %d shares lane %d (no lane with its bullet left)", id, donor);
        return donor;
    }

    static int ResolvePartByteLocked(int space, int id)
    {
        if (id < kWideIdBase)
            return id;
        WidePartState* w = WideStateFor(space, id);
        if (!w)
        {
            if (space == kVanillaSpace_Receiver)
            {
                const auto itd = g_ReceiverMotionDonor.find(id);
                if (itd != g_ReceiverMotionDonor.end()
                    && itd->second > 0 && itd->second < 234)
                    return itd->second;
            }
            static std::set<long long> logged;
            if (logged.size() < 32 &&
                logged.insert((static_cast<long long>(space) << 32) | id).second)
                LogWarn("EquipParam: part id %d (space %d) is not declared, treated as no part", id, space);
            return 0;
        }
        if (w->alias)
            return w->alias;
        if (w->looseDonor && w->magazineDonor > 0)
            return w->magazineDonor;

        if (space == kVanillaSpace_Magazine && WideMagazinesPlanned()
            && w->rowSet && w->row.size() == 8)
        {
            EnsurePartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                              gAddr.MotionLoaderImpl_MagazineTypeTable,
                              kMagazineTypeVanillaRows);
            const auto* rows = (EnsurePartShadow(g_Magazine) && g_MagazineTypeExtReady)
                ? PartCurrentBuf(g_Magazine) : nullptr;
            bool compatibleLanes[256]{};
            CollectMagazineDonorLanesLocked(compatibleLanes);
            int typed = 0;
            int untyped = 0;
            for (int donor = 1; rows && donor <= 255; ++donor)
            {
                if (!compatibleLanes[donor]
                    || rows[static_cast<size_t>(donor - 1) * 8 + 6] != w->row[6])
                    continue;
                const int laneType = g_MagazineTypeExt[donor];
                if (w->motionType > 0)
                {
                    if (laneType != w->motionType)
                        continue;
                    typed = donor;
                    break;
                }
                if (laneType > 0 && !typed)
                    typed = donor;
                else if (laneType == 0 && !untyped)
                    untyped = donor;
            }
            const int donor = typed ? typed : untyped;
            if (donor)
            {
                if (w->motionType <= 0 && g_MagazineTypeExt[donor] > 0)
                    w->motionType = g_MagazineTypeExt[donor];
                w->magazineDonor = donor;
                g_SharedMagazineLanes.insert(donor);
                return donor;
            }
        }

        PartBuffer* pb = SpaceBuffer(space);
        int alias = 0;
        if (space == kVanillaSpace_Receiver)
            alias = AllocateReceiverLaneSlot();
        else if (pb && EnsurePartShadow(*pb))
            alias = AllocateAliasByteLocked(*pb);
        if (!alias && space == kVanillaSpace_Magazine)
        {
            const int loose = BindLooseMagazineLaneLocked(*w, id);
            if (loose)
                return loose;
        }
        if (!alias)
        {
            static std::set<long long> noLaneLogged;
            if ((space != kVanillaSpace_Magazine || w->rowSet) && noLaneLogged.size() < 32
                && noLaneLogged.insert((static_cast<long long>(space) << 32) | id).second)
                LogWarn("EquipParam: %s id %d: no free engine lane, weapon gets part 0",
                    pb ? pb->name : "part", id);
            return 0;
        }
        w->alias = alias;

        if (pb && !w->row.empty())
        {
            EnsurePartShadow(*pb);
            std::uint8_t* buf = PartCurrentBuf(*pb);
            if (buf)
                std::memcpy(buf + static_cast<size_t>(alias - 1) * pb->stride,
                            w->row.data(), static_cast<size_t>(pb->stride));
        }

        switch (space)
        {
        case kVanillaSpace_Receiver:
            if (w->motionFrom > 0 && w->motionType > 0)
            {
                ChimeraMotion_InheritFromMotionFrom(alias, w->motionFrom);
                g_ReceiverMotionDonor[alias] = w->motionFrom;
            }
            WriteReceiverType(alias, w->motionType >= 0 ? w->motionType : 0);
            {
                auto ito = g_RcvOverflow.find(id);
                if (ito != g_RcvOverflow.end())
                    g_RcvOverflow[alias] = ito->second;
            }
            break;
        case kVanillaSpace_UnderBarrel:
            if (w->pendingUbType > 0)
                WriteUnderBarrelType(alias, w->pendingUbType);
            break;
        case kVanillaSpace_Magazine:
            if (w->motionType > 0 && !WriteDeclaredMagazineType(alias, w->motionType))
                LogError("EquipParam: ammoId=%d: magazine type table unavailable, no magazine motion", id);
            break;
        case kVanillaSpace_Sight:
            if (alias > kUiSightVanillaMax && alias < kPartTypeExtCount)
                g_UiSightDonor[alias] = g_UiSightDonor[id];
            CustomScopeUi::BindAlias(id, alias);
            break;
        case kVanillaSpace_Barrel:
            {
                auto ito = g_BarrelOverflow.find(id);
                if (ito != g_BarrelOverflow.end())
                    g_BarrelOverflow[alias] = ito->second;
                auto itx = g_BarrelExtraMult.find(id);
                if (itx != g_BarrelExtraMult.end())
                    g_BarrelExtraMult[alias] = itx->second;
            }
            break;
        default:
            break;
        }

        return alias;
    }
}

int EquipParam_ResolvePartByte(int space, int id)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return ResolvePartByteLocked(space, id);
}

bool EquipParam_SetReceiverMotionType(int receiverId, int receiverType)
{
    if (receiverId <= 0 || receiverId >= kPartTypeExtCount)
    {
        LogWarn("ReceiverMotion: receiverId=%d is invalid, receiverType %d ignored", receiverId, receiverType);
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (receiverType <= 0 || receiverType > 0xFF
        || LowestVanillaReceiverOfType(receiverType) == 0)
    {
        LogWarn("ReceiverMotion: receiverId=%d: receiverType=%d is not a vanilla type, ignored",
            receiverId, receiverType);
        return false;
    }
    g_DeclaredReceiverType[receiverId] = receiverType;

    const auto itFrom = g_MotionFromReceiverType.find(receiverId);
    if (itFrom != g_MotionFromReceiverType.end() && itFrom->second != receiverType)
        LogReceiverTypeConflict(receiverId, receiverType, itFrom->second);

    WidePartState* w = WideStateFor(kVanillaSpace_Receiver, receiverId);
    if (w)
    {
        w->motionType = receiverType;
        if (w->alias && !WriteReceiverType(w->alias, receiverType))
            LogError("ReceiverMotion: receiverId=%d: receiver type table unavailable, receiverType not set",
                receiverId);
    }
    else
    {
        EnsureRcvTypeExt();
        const int prevType = g_RcvTypeExtReady ? g_RcvTypeExt[receiverId] : -1;
        if (!WriteReceiverType(receiverId, receiverType))
            LogError("ReceiverMotion: receiverId=%d: receiver type table unavailable, receiverType not set",
                receiverId);
        else if (receiverId <= kReceiverCapacity && !g_RcvClaimed.count(receiverId)
                 && prevType >= 0 && prevType != receiverType)
            EquipParam_VanillaForceTaint(kVanillaSpace_Receiver, receiverId,
                                         "a different motion/animation type");
    }
    GunBasic_ReNarrowReceiverRows(receiverId);
    return true;
}

bool Install_MotionLoader_ReceiverTypeHook()
{
    EnsureRcvTypeExt();

    void* target = ResolveGameAddress(gAddr.MotionLoaderImpl_GetReceiverType);
    if (!target)
    {
        LogError("EquipParam: GetReceiverType address missing, custom receiver anim types off");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetReceiverType,
        reinterpret_cast<void**>(&g_OrigGetReceiverType));
    if (!ok)
        LogError("EquipParam: GetReceiverType hook failed, custom receiver anim types off");
    return ok;
}

void Uninstall_MotionLoader_ReceiverTypeHook()
{
    if (gAddr.MotionLoaderImpl_GetReceiverType)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetReceiverType));
    g_OrigGetReceiverType = nullptr;
    g_RcvTypeExtReady = false;
}

namespace
{
    struct PartTypeHookSpec
    {
        const char*   label;
        uintptr_t     fnAddr;
        uintptr_t     tableAddr;
        std::uint8_t* ext;
        bool*         ready;
        int           rows;
        void*         detour;
        void**        orig;
    };

    static bool InstallPartTypeHook(const PartTypeHookSpec& s)
    {
        EnsurePartTypeExt(s.ext, *s.ready, s.tableAddr, s.rows);

        void* target = ResolveGameAddress(s.fnAddr);
        if (!target)
        {
            LogError("EquipParam: Get%sType address missing, custom %s anim types off", s.label, s.label);
            return true;
        }

        const bool ok = CreateAndEnableHook(target, s.detour, s.orig);
        if (!ok)
            LogError("EquipParam: Get%sType hook failed, custom %s anim types off", s.label, s.label);
        return ok;
    }
}

int EquipParam_InheritPartMotionTypes(int barrelDst, int barrelSrc,
                                     int magazineDst, int magazineSrc,
                                     int stockDst, int stockSrc,
                                     int* outEligible)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (g_MagazineTypeDeclaredLane.count(magazineDst) != 0)
        magazineSrc = 0;
    int eligible = 0;
    int applied = 0;
    InheritPartTypeExt(g_BarrelTypeExt, g_BarrelTypeExtReady,
                       gAddr.MotionLoaderImpl_BarrelTypeTable,
                       kBarrelTypeVanillaRows, barrelDst, barrelSrc,
                       eligible, applied);
    InheritPartTypeExt(g_MagazineTypeExt, g_MagazineTypeExtReady,
                       gAddr.MotionLoaderImpl_MagazineTypeTable,
                       kMagazineTypeVanillaRows, magazineDst, magazineSrc,
                       eligible, applied);
    InheritPartTypeExt(g_StockTypeExt, g_StockTypeExtReady,
                       gAddr.MotionLoaderImpl_StockTypeTable,
                       kStockTypeVanillaRows, stockDst, stockSrc,
                       eligible, applied);
    if (outEligible)
        *outEligible = eligible;
    return applied;
}

bool Install_MotionLoader_BarrelTypeHook()
{
    const PartTypeHookSpec s{
        "Barrel", gAddr.MotionLoaderImpl_GetBarrelType,
        gAddr.MotionLoaderImpl_BarrelTypeTable,
        g_BarrelTypeExt, &g_BarrelTypeExtReady, kBarrelTypeVanillaRows,
        &hkGetBarrelType, reinterpret_cast<void**>(&g_OrigGetBarrelType) };
    return InstallPartTypeHook(s);
}

void Uninstall_MotionLoader_BarrelTypeHook()
{
    if (gAddr.MotionLoaderImpl_GetBarrelType)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetBarrelType));
    g_OrigGetBarrelType = nullptr;
    g_BarrelTypeExtReady = false;
}

bool Install_MotionLoader_MagazineTypeHook()
{
    const PartTypeHookSpec s{
        "Magazine", gAddr.MotionLoaderImpl_GetMagazineType,
        gAddr.MotionLoaderImpl_MagazineTypeTable,
        g_MagazineTypeExt, &g_MagazineTypeExtReady, kMagazineTypeVanillaRows,
        &hkGetMagazineType, reinterpret_cast<void**>(&g_OrigGetMagazineType) };
    return InstallPartTypeHook(s);
}

void Uninstall_MotionLoader_MagazineTypeHook()
{
    if (gAddr.MotionLoaderImpl_GetMagazineType)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetMagazineType));
    g_OrigGetMagazineType = nullptr;
    g_MagazineTypeExtReady = false;
}

bool Install_MotionLoader_StockTypeHook()
{
    const PartTypeHookSpec s{
        "Stock", gAddr.MotionLoaderImpl_GetStockType,
        gAddr.MotionLoaderImpl_StockTypeTable,
        g_StockTypeExt, &g_StockTypeExtReady, kStockTypeVanillaRows,
        &hkGetStockType, reinterpret_cast<void**>(&g_OrigGetStockType) };
    return InstallPartTypeHook(s);
}

void Uninstall_MotionLoader_StockTypeHook()
{
    if (gAddr.MotionLoaderImpl_GetStockType)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetStockType));
    g_OrigGetStockType = nullptr;
    g_StockTypeExtReady = false;
}

static void InstallSightWindowObservers()
{
    void* create = ResolveGameAddress(gAddr.SightCommonFactory_CreateWindow);
    void* del    = ResolveGameAddress(gAddr.SightCommonFactory_DeleteWindow);
    if (!create || !del)
    {
        LogError("EquipParam: SightCommonFactory address missing, custom sights use a borrowed overlay");
        return;
    }
    if (!CreateAndEnableHook(create, &hkSightWindowCreate,
            reinterpret_cast<void**>(&g_OrigSightWindowCreate)))
        LogError("EquipParam: SightCommonFactory::CreateWindow hook failed, custom sight overlays untracked");
    if (!CreateAndEnableHook(del, &hkSightWindowDelete,
            reinterpret_cast<void**>(&g_OrigSightWindowDelete)))
        LogError("EquipParam: SightCommonFactory::DeleteWindow hook failed, closed overlays stay marked live");
}

bool Install_UiController_UiSightTypeHook()
{
    void* target = ResolveGameAddress(gAddr.UiControllerImpl_GetUiSightTypeBySightId);
    if (!target)
    {
        LogError("EquipParam: GetUiSightTypeBySightId address missing, custom sights have no scope UI");
        return true;
    }

    __try
    {
        if (std::memcmp(target, kUiSightTypePrologue,
                        sizeof(kUiSightTypePrologue)) != 0)
        {
            LogError("EquipParam: GetUiSightTypeBySightId prologue mismatch at %p, custom sights have no scope UI",
                target);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogError("EquipParam: GetUiSightTypeBySightId at %p unreadable, custom sights have no scope UI", target);
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetUiSightTypeBySightId,
        reinterpret_cast<void**>(&g_OrigGetUiSightTypeBySightId));
    InstallSightWindowObservers();
    if (ok)
        CustomScopeUi::Install();
    if (!ok)
        LogError("EquipParam: GetUiSightTypeBySightId hook failed, custom sights have no scope UI");
    return ok;
}

void Uninstall_UiController_UiSightTypeHook()
{
    CustomScopeUi::Uninstall();
    if (gAddr.SightCommonFactory_CreateWindow)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.SightCommonFactory_CreateWindow));
    if (gAddr.SightCommonFactory_DeleteWindow)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.SightCommonFactory_DeleteWindow));
    g_OrigSightWindowCreate = nullptr;
    g_OrigSightWindowDelete = nullptr;
    if (gAddr.UiControllerImpl_GetUiSightTypeBySightId)
        DisableAndRemoveHook(
            ResolveGameAddress(gAddr.UiControllerImpl_GetUiSightTypeBySightId));
    g_OrigGetUiSightTypeBySightId = nullptr;
}

bool Install_MotionLoader_UnderBarrelTypeHook()
{
    EnsureUbTypeExt();

    void* target = ResolveGameAddress(gAddr.MotionLoaderImpl_GetUnderBarrelType);
    if (!target)
    {
        LogError("EquipParam: GetUnderBarrelType address missing, custom under-barrels have no animation");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetUnderBarrelType,
        reinterpret_cast<void**>(&g_OrigGetUnderBarrelType));
    if (!ok)
        LogError("EquipParam: GetUnderBarrelType hook failed, custom under-barrels have no animation");
    return ok;
}

void Uninstall_MotionLoader_UnderBarrelTypeHook()
{
    if (gAddr.MotionLoaderImpl_GetUnderBarrelType)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MotionLoaderImpl_GetUnderBarrelType));
    g_OrigGetUnderBarrelType = nullptr;
    g_UbTypeExtReady = false;
}

namespace
{

    using GetAttackId_t = unsigned short(__fastcall*)(void* self, int equipId);
    static GetAttackId_t g_OrigGetAttackId = nullptr;

    static int SehAvOnly(unsigned long code)
    {
        return (code == EXCEPTION_ACCESS_VIOLATION)
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    static unsigned short __fastcall hkGetAttackIdByEquipId(void* self, int equipId)
    {
        const std::uint32_t supportWeapon = SupportWeapon::RunningItem(static_cast<std::uint32_t>(equipId));
        const std::uint32_t familyFrom    = SupportWeapon::FamilyFrom(supportWeapon);
        if (familyFrom != 0)
        {
            const int attackId = SupportWeapon::AttackId(supportWeapon);
            if (attackId > 0)
                return static_cast<unsigned short>(attackId);
            equipId = static_cast<int>(familyFrom);
        }

        const int declared = EquipParam_GetDeclaredWeaponAttackId(equipId);
        if (declared > 0)
            return static_cast<unsigned short>(declared & 0xFFFF);
        if (GunBasic_WeaponHasNoRow(equipId))
            return 0;   // the original reads receiver 0 as a null pointer

        __try
        {
            return g_OrigGetAttackId(self, equipId);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 2000)
            {
                lastMs = now;
                LogWarn("EquipParam: GetAttackIdByEquipId fault caught for equipId=%d, attackId 0",
                    equipId);
            }
            return 0;
        }
    }
}

namespace
{

    using SetUpGunInfo_t = void(__fastcall*)(void*, void*, unsigned int, void*,
                                             void*, void*, void*, void*, void*);
    static SetUpGunInfo_t g_OrigSetUpGunInfo = nullptr;

    static int ReadByteAtSEH(const void* base, size_t off)
    {
        __try
        {
            return static_cast<const std::uint8_t*>(base)[off];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int WriteByteAtSEH(void* base, size_t off, std::uint8_t val)
    {
        __try
        {
            static_cast<std::uint8_t*>(base)[off] = val;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int SwapByteSEH(std::uint8_t* loc, std::uint8_t val, std::uint8_t* savedOut)
    {
        __try
        {
            *savedOut = *loc;
            *loc = val;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int RestoreByteSEH(std::uint8_t* loc, std::uint8_t val)
    {
        __try
        {
            *loc = val;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int CopyPoolRowSEH(std::uint8_t* pool, int dstIdx, int srcIdx, int stride)
    {
        __try
        {
            std::memcpy(pool + static_cast<size_t>(dstIdx) * stride,
                        pool + static_cast<size_t>(srcIdx) * stride,
                        static_cast<size_t>(stride));
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    struct PoolSwapState
    {
        std::uint8_t* loc[12] = {};
        std::uint8_t  saved[12] = {};
        int           count = 0;
    };

    static void SwapReceiverPools(int rcId, int scratchIdx, int sndSlot,
                                  PoolSwapState& st)
    {
        (void)sndSlot;
        const auto it = g_RcvOverflow.find(rcId);
        if (it == g_RcvOverflow.end())
            return;
        std::uint8_t* rbuf = ImplBufPtr(kReceiverImplOffset);
        if (!rbuf)
            return;
        for (int k = 0; k < 4; ++k)
        {
            const int idx = it->second.idx[k];
            if (idx < 0)
                continue;
            RcvPool& rp = PoolFor(static_cast<RcvPoolKind>(k));
            std::uint8_t* pool = PartCurrentBuf(rp.pb);
            if (!pool)
                continue;
            if (CopyPoolRowSEH(pool, scratchIdx, idx, rp.pb.stride) != 1)
                continue;
            std::uint8_t* loc = rbuf
                + static_cast<size_t>(rcId - 1) * kReceiverStride
                + rp.rowByteOffset;
            if (k == POOL_SND)
            {
                std::uint8_t prev = 0;
                if (SwapByteSEH(loc, static_cast<std::uint8_t>(scratchIdx),
                                &prev) == 1)
                {
                    std::lock_guard<std::mutex> lock(g_FireSoundMutex);
                    const auto fit = g_FireSoundByRow.find(idx);
                    if (fit != g_FireSoundByRow.end())
                        g_FireSoundByRow[scratchIdx] = fit->second;
                    else
                        g_FireSoundByRow.erase(scratchIdx);
                }
                continue;
            }
            if (st.count < 12
                && SwapByteSEH(loc, static_cast<std::uint8_t>(scratchIdx),
                               &st.saved[st.count]) == 1)
            {
                st.loc[st.count] = loc;
                ++st.count;
            }
        }
    }

    static void SwapBarrelPool(int barrelId, PoolSwapState& st)
    {
        const auto it = g_BarrelOverflow.find(barrelId);
        if (it == g_BarrelOverflow.end())
            return;
        std::uint8_t* pool = PartCurrentBuf(g_BarrelBase.pb);
        std::uint8_t* bbuf = PartCurrentBuf(g_Barrel);
        if (!pool || !bbuf)
            return;
        if (CopyPoolRowSEH(pool, kPoolScratch0, it->second, g_BarrelBase.pb.stride) != 1)
            return;
        std::uint8_t* loc = bbuf + static_cast<size_t>(barrelId - 1) * 2 + 1;
        if (st.count < 12
            && SwapByteSEH(loc, static_cast<std::uint8_t>(kPoolScratch0),
                           &st.saved[st.count]) == 1)
        {
            st.loc[st.count] = loc;
            ++st.count;
        }
    }

    static void PrepareGunInfoSwap(void* desc, PoolSwapState& st)
    {
        if (!desc)
            return;
        if (g_RcvOverflow.empty() && g_BarrelOverflow.empty())
            return;
        const int rc = ReadByteAtSEH(desc, 0);
        const int ba = ReadByteAtSEH(desc, 1);
        const int ub = ReadByteAtSEH(desc, 10);
        if (rc > 0)
            SwapReceiverPools(rc, kPoolScratch0, 0, st);
        int ubRc = 0;
        if (ub > 0)
        {
            std::uint8_t* ubuf = ImplBufPtr(0x48);
            if (ubuf)
                ubRc = ReadByteAtSEH(ubuf, static_cast<size_t>(ub - 1) * 3);
        }
        if (ubRc > 0 && ubRc != rc)
            SwapReceiverPools(ubRc, kPoolScratch1, 1, st);
        if (ba > 0)
            SwapBarrelPool(ba, st);
    }

    static void RestoreGunInfoSwap(PoolSwapState& st)
    {
        for (int i = st.count - 1; i >= 0; --i)
            RestoreByteSEH(st.loc[i], st.saved[i]);
    }

    static int CallOrigGunInfoSEH(void* self, void* desc, unsigned int equipId,
                                  void* gunInfo, void* a5, void* a6, void* a7,
                                  void* a8, void* a9)
    {
        __try
        {
            g_OrigSetUpGunInfo(self, desc, equipId, gunInfo, a5, a6, a7, a8, a9);
            return 1;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 2000)
            {
                lastMs = now;
                LogWarn("EquipParam: SetUpGunInfo fault caught, equipId=%u rc=%d ba=%d ub=%d, GunInfo partial",
                    equipId, ReadByteAtSEH(desc, 0), ReadByteAtSEH(desc, 1),
                    ReadByteAtSEH(desc, 10));
            }
            return 0;
        }
    }

    static int ApplyBarrelExtraSEH(void* gunInfo, const BarrelExtraMult& m)
    {
        __try
        {
            std::uint8_t* g = static_cast<std::uint8_t*>(gunInfo);
            if (m.rem[0] != 1.0)
            {
                const double d = *reinterpret_cast<std::uint16_t*>(g + 0x68) * m.rem[0] + 0.5;
                *reinterpret_cast<std::uint16_t*>(g + 0x68) =
                    d > 65535.0 ? 65535 : static_cast<std::uint16_t>(d);
            }
            if (m.rem[2] != 1.0)
            {
                const double d = g[0x55] * m.rem[2] + 0.5;
                g[0x55] = d > 255.0 ? 255 : static_cast<std::uint8_t>(d);
            }
            if (m.rem[3] != 1.0)
            {
                double d = g[0x54] * m.rem[3] + 0.5;
                g[0x54] = d > 255.0 ? 255 : static_cast<std::uint8_t>(d);
                d = *reinterpret_cast<std::uint16_t*>(g + 0x6c) * m.rem[3] + 0.5;
                *reinterpret_cast<std::uint16_t*>(g + 0x6c) =
                    d > 65535.0 ? 65535 : static_cast<std::uint16_t>(d);
            }
            if (m.rem[4] != 1.0)
            {
                const double d = g[0x81] * m.rem[4] + 0.5;
                g[0x81] = d > 255.0 ? 255 : static_cast<std::uint8_t>(d);
            }
            if (m.rem[5] != 1.0)
            {
                const double d = *reinterpret_cast<std::uint16_t*>(g + 0x1e) * m.rem[5] + 0.5;
                *reinterpret_cast<std::uint16_t*>(g + 0x1e) =
                    d > 65535.0 ? 65535 : static_cast<std::uint16_t>(d);
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::uint8_t g_SafeDescRow[12] = {};
    static bool         g_HaveSafeDescRow = false;

    static bool EnsureSafeDescRow()
    {
        if (g_HaveSafeDescRow)
            return true;
        const int hi = kGunBasicParameters2StockSlots;
        unsigned char row[12];
        for (int wid = 1; wid <= hi; ++wid)
        {
            if (!GunBasic_ReadRowBytes(wid, row))
                continue;
            if (row[0] >= 1 && row[0] <= 253
                && row[1] >= 1 && row[1] <= 253
                && row[2] >= 1 && row[2] <= 253)
            {
                std::memcpy(g_SafeDescRow, row, 12);
                g_HaveSafeDescRow = true;
                return true;
            }
        }
        return false;
    }

    static int SubstituteEmptyDesc(void* desc, std::uint8_t saved[12], std::uint8_t mask[12])
    {
        for (int k = 0; k < 12; ++k)
            mask[k] = 0;
        if (!desc)
            return 0;
        if (ReadByteAtSEH(desc, 0) != 0)
            return 0;
        if (!EnsureSafeDescRow())
            return 0;
        auto* d = static_cast<std::uint8_t*>(desc);
        int n = 0;
        for (int k = 0; k < 12; ++k)
            if (SwapByteSEH(d + k, g_SafeDescRow[k], &saved[k]) == 1)
            {
                mask[k] = 1;
                ++n;
            }
        return n > 0 ? 1 : 0;
    }

    static void RestoreSubstitutedDesc(void* desc, const std::uint8_t saved[12],
                                       const std::uint8_t mask[12])
    {
        if (!desc)
            return;
        auto* d = static_cast<std::uint8_t*>(desc);
        for (int k = 0; k < 12; ++k)
            if (mask[k])
                RestoreByteSEH(d + k, saved[k]);
    }

    static void FillCustomMotionEntriesEarly();

    static int ReceiverRowByteFor(int rc)
    {
        if (rc <= 0)
            return 0;
        const int ownRow = ReceiverMotion_RowByteFor(rc);
        if (ownRow == kPartMotionRowReceiver)
            return ownRow;
        const auto it = g_ReceiverMotionDonor.find(rc);
        if (it != g_ReceiverMotionDonor.end() && it->second > 0 && it->second < 234)
            return it->second;
        if (WidePartState* w = WideStateFor(kVanillaSpace_Receiver, rc))
        {
            if (w->motionFrom > 0 && w->motionFrom < 234)
                return w->motionFrom;
            if (w->alias > 0 && w->alias < 234)
                return w->alias;
        }
        if (ownRow)
            return ownRow;
        return (rc < kWideIdBase) ? rc : 0;
    }

    static void ApplyUnderbarrelMagazine(void* gunInfo, void* ammoEquipId,
                                         const MagazineParameters& magazine)
    {
        __try
        {
            auto* info = static_cast<std::uint8_t*>(gunInfo);
            const auto flags = *reinterpret_cast<const std::uint16_t*>(info + 0x8A);
            *reinterpret_cast<std::uint16_t*>(info + 0x62) =
                static_cast<std::uint16_t>(magazine.capacity + ((flags & 0x20) ? 1 : 0));
            *reinterpret_cast<std::uint16_t*>(info + 0x66) = magazine.carryMax;
            *reinterpret_cast<std::uint8_t*>(info + 0x7D) = static_cast<std::uint8_t>(magazine.bulletId);
            if (ammoEquipId)
                *static_cast<std::uint32_t*>(ammoEquipId) = magazine.equipId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static bool ReadOutIdSEH(const std::uint32_t* out, std::uint32_t& value)
    {
        __try
        {
            value = *out;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void WriteOutIdSEH(std::uint32_t* out, std::uint32_t value)
    {
        __try
        {
            *out = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void HandAmmoAliasToEngine(std::uint32_t* equipAmmoOut, unsigned int gunEngineId)
    {
        std::uint32_t ammoId = 0;
        if (!equipAmmoOut || !ReadOutIdSEH(equipAmmoOut, ammoId) || ammoId == 0)
            return;
        const std::int32_t engineId = equip::ToEngine(static_cast<std::int32_t>(ammoId));
        if (engineId == static_cast<std::int32_t>(ammoId))
            return;
        WriteOutIdSEH(equipAmmoOut, static_cast<std::uint32_t>(engineId));
        equip::NoteAmmoAliasFor(static_cast<std::int32_t>(gunEngineId), engineId);
    }

    static void __fastcall hkSetUpGunInfo(void* self, void* desc,
                                          unsigned int equipId, void* gunInfo,
                                          void* a5, void* a6, void* a7,
                                          void* a8, void* a9)
    {
        PartIdWidenReadGuard widthGuard;
        if (!widthGuard)
            return;
        EnsureShadowsRelocated();
        FillCustomMotionEntriesEarly();

        const bool widen = PartIdWiden_IsActive();
        std::uint8_t wideDesc[24] = {};
        void* passDesc = desc;
        int wideIds[11] = {};
        bool haveWideIds = false;

        if (!widen)
            LaneWindow_BindForBuild(desc, equipId);
        std::uint8_t descSaved[12], descMask[12];
        const int substituted = SubstituteEmptyDesc(desc, descSaved, descMask);
        if (substituted)
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 2000)
            {
                lastMs = now;
                LogWarn("EquipParam: equipId=%u has no gunBasic row, built as a vanilla stand-in", equipId);
            }
        }
        if (desc)
        {
            const int heldSub = TppEquip_GetSubIdForEquipId(static_cast<int>(equipId));
            const int heldAmmo =
                heldSub > 0 ? GunBasic_PendingWideAmmoForWeapon(heldSub) : 0;
            if (heldAmmo > 0)
            {
                static std::mutex heldMx;
                static std::set<int> heldLogged;
                bool first = false;
                {
                    std::lock_guard<std::mutex> lk(heldMx);
                    first = heldLogged.insert(heldSub).second;
                }
                if (first)
                    LogWarn("GunBasic: weaponId=%d (equipId %u) built before SetMagazine ammoId=%d, using %s",
                        heldSub, equipId, heldAmmo,
                        substituted ? "a vanilla stand-in" : "its previous row");
            }
        }

        if (widen && desc)
        {
            const int sub = TppEquip_GetSubIdForEquipId(static_cast<int>(equipId));
            unsigned char narrow[12] = {};
            for (int k = 0; k < 12; ++k)
            {
                const int b = ReadByteAtSEH(desc, static_cast<size_t>(k));
                narrow[k] = (b > 0) ? static_cast<unsigned char>(b) : 0;
            }
            haveWideIds = !substituted && sub > 0
                && GunBasic_GetLogicalParts(sub, wideIds, narrow);
            PartIdWiden_BuildWideDesc(narrow, haveWideIds ? wideIds : nullptr, wideDesc);
            passDesc = wideDesc;
        }

        PoolSwapState st;
        PrepareGunInfoSwap(desc, st);
        const int ok = CallOrigGunInfoSEH(self, passDesc, equipId, gunInfo,
                                          a5, a6, a7, a8, a9);
        RestoreGunInfoSwap(st);
        if (ok == 1)
        {
            HandAmmoAliasToEngine(static_cast<std::uint32_t*>(a8), equipId);
            HandAmmoAliasToEngine(static_cast<std::uint32_t*>(a9), equipId);
        }

        if (ok == 1 && gunInfo && EquipParam_WideMagazinesReady())
        {
            const int underbarrel = haveWideIds ? wideIds[8] : ReadByteAtSEH(desc, 10);
            int magazineId = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(g_Mutex);
                const auto it = g_UnderBarrelMagazine.find(underbarrel);
                if (it != g_UnderBarrelMagazine.end())
                    magazineId = it->second;
            }
            MagazineParameters magazine{};
            if (magazineId >= kWideIdBase && EquipParam_GetMagazineParameters(magazineId, magazine))
                ApplyUnderbarrelMagazine(gunInfo, a9, magazine);
        }

        if (substituted)
            RestoreSubstitutedDesc(desc, descSaved, descMask);

        if (ok == 1 && gunInfo && !g_BarrelExtraMult.empty())
        {
            const int ba = (widen && haveWideIds) ? wideIds[1] : ReadByteAtSEH(desc, 1);
            if (ba > 0)
            {
                const auto it = g_BarrelExtraMult.find(ba);
                if (it != g_BarrelExtraMult.end()
                    && ApplyBarrelExtraSEH(gunInfo, it->second) != 1)
                {
                    static std::atomic<int> extraBudget{ 8 };
                    if (extraBudget.fetch_sub(1) > 0)
                        LogWarn("EquipParam: barrelId=%d extra multiplier write faulted, base values kept", ba);
                }
            }
        }

        if (ok == 1 && gunInfo)
        {
            const int rc = (widen && haveWideIds) ? wideIds[0] : ReadByteAtSEH(desc, 0);
            const int ub = (widen && haveWideIds) ? wideIds[8] : ReadByteAtSEH(desc, 10);
            int ubRc = 0;
            if (ub > 0)
                ubRc = PartRowByte(g_UnderBarrel, ub, 0);

            const int opt1 = (widen && haveWideIds) ? wideIds[9]  : ReadByteAtSEH(desc, 8);
            const int opt2 = (widen && haveWideIds) ? wideIds[10] : ReadByteAtSEH(desc, 9);
            int laserOpt = 0;
            if (opt2 > 0)
            {
                if (PartRowByte(g_Option, opt2, 0) & 0x2)
                    laserOpt = opt2;
            }
            else if (opt1 > 0 && (PartRowByte(g_Option, opt1, 0) & 0x2))
                laserOpt = opt1;
            LaserColor_NoteWeaponOption(static_cast<int>(equipId), laserOpt);

            if (rc > 0)
            {
                ReceiverMotion_EnsurePartRowsPublished(rc);
                const int rowVal = ReceiverRowByteFor(rc);
                if (rowVal == kPartMotionRowReceiver)
                {
                    ReceiverPartRowIndices own;
                    if (!ReceiverMotion_GetPartRowIndices(rc, own)
                        || !PartMotionRows_SetEquipRow(equipId, 0, own.motion,
                                                       own.pose, own.flags))
                    {
                        static std::atomic<int> rowBudget{ 8 };
                        if (rowBudget.fetch_sub(1) > 0)
                            LogWarn("PartMotionRows: equipId=%u receiverId=%d: part motion row not published, slide/bolt static",
                                equipId, rc);
                    }
                }
                if (rowVal > 0 && rowVal != rc
                    && ReadByteAtSEH(gunInfo, 0x7a) == (rc & 0xFF))
                    WriteByteAtSEH(gunInfo, 0x7a, static_cast<std::uint8_t>(rowVal));
            }

            bool ubOwnRow = false;
            if (ubRc > 0)
            {
                ReceiverMotion_EnsurePartRowsPublished(ubRc);
                ReceiverPartRowIndices ubOwn;
                if (ReceiverMotion_GetPartRowIndices(ubRc, ubOwn)
                    && PartMotionRows_SetEquipRow(equipId, 1, ubOwn.motion,
                                                  ubOwn.pose, ubOwn.flags)
                    && ReadByteAtSEH(gunInfo, 0x7b) == (ubRc & 0xFF)
                    && WriteByteAtSEH(gunInfo, 0x7b,
                           static_cast<std::uint8_t>(kPartMotionRowUnderBarrel)) == 1)
                    ubOwnRow = true;
            }

            if (!ubOwnRow && ubRc > 0 && !g_ReceiverMotionDonor.empty())
            {
                const auto it = g_ReceiverMotionDonor.find(ubRc);
                if (it != g_ReceiverMotionDonor.end() && it->second > 0 && it->second < 256
                    && ReadByteAtSEH(gunInfo, 0x7b) == (ubRc & 0xFF))
                    WriteByteAtSEH(gunInfo, 0x7b, static_cast<std::uint8_t>(it->second));
            }

            if (ReadByteAtSEH(gunInfo, 0x7a) == 0)
            {
                const bool stamped = rc >= kWideIdBase
                    && WriteByteAtSEH(gunInfo, 0x7a, 1) == 1;
                static std::atomic<int> guarded{ 0 };
                if (guarded.fetch_add(1, std::memory_order_relaxed) < 8)
                    LogWarn("EquipParam: equipId=%u receiverId=%d has no motion row (%s), crashes on draw; set motionFrom",
                        equipId, rc,
                        stamped ? "wide id without donor, stamped receiver 1" : "left 0 by engine");
            }
        }
    }
}

namespace
{
    static std::uint8_t* g_BulletSwapLoc   = nullptr;
    static std::uint8_t  g_BulletSwapSaved = 0;
}

int EquipParam_BulletFalloffSwapBegin(int bulletId)
{
    if (bulletId <= 0 || g_BulletBaseOverflow.empty())
        return 0;
    if (g_BulletSwapLoc)
        return 0;
    const auto it = g_BulletBaseOverflow.find(bulletId);
    if (it == g_BulletBaseOverflow.end())
        return 0;
    std::uint8_t* pool = PartCurrentBuf(g_BulletBase.pb);
    std::uint8_t* bbuf = PartCurrentBuf(g_Bullet);
    if (!pool || !bbuf)
        return 0;
    if (CopyPoolRowSEH(pool, kPoolScratch0, it->second, g_BulletBase.pb.stride) != 1)
        return 0;
    std::uint8_t* loc = bbuf + static_cast<size_t>(bulletId - 1) * 14 + 6;
    if (SwapByteSEH(loc, static_cast<std::uint8_t>(kPoolScratch0),
                    &g_BulletSwapSaved) != 1)
        return 0;
    g_BulletSwapLoc = loc;
    return 1;
}

void EquipParam_BulletFalloffSwapEnd()
{
    if (!g_BulletSwapLoc)
        return;
    RestoreByteSEH(g_BulletSwapLoc, g_BulletSwapSaved);
    g_BulletSwapLoc = nullptr;
}

namespace
{

    using SetupWeaponInfo_t = void(__fastcall*)(void* self, void* work, int slot);
    static SetupWeaponInfo_t g_OrigSetupWeaponInfo = nullptr;

    using Resolver20_t  = std::uint32_t(__fastcall*)(void* self, std::uint32_t key);
    using Resolver40_t  = char(__fastcall*)(void* self, std::uint32_t key);
    using Resolver150_t = void*(__fastcall*)(void* self, std::uint32_t key);
    static Resolver20_t  g_OrigResolver20  = nullptr;
    static Resolver40_t  g_OrigResolver40  = nullptr;
    static Resolver150_t g_OrigResolver150 = nullptr;
    static Resolver150_t g_Finder148       = nullptr;


    using PartCode_t = std::uint32_t(__fastcall*)(void* self, std::uint32_t partId);
    static PartCode_t g_OrigPartCode[5] = {};
    static void*      g_PartCodeAddr[5] = {};
    static bool       g_PartCodeExact[5] = {};
    static const size_t kPartVtblOff[5] = { 0x20, 0x28, 0x30, 0x38, 0x40 };
    static const int    kPartGbByte[5]  = { 0, 1, 2, 3, 10 };
    static thread_local std::uint32_t g_TlsCustomEquip  = 0;
    static thread_local std::uint32_t g_TlsVanillaEquip = 0;
    static void* g_ResolverB = nullptr;

    struct FamilyGb
    {
        std::uint16_t subId = 0;
        unsigned char gb[12] = {};
        bool ok = false;
    };
    static std::map<std::uint32_t, FamilyGb> g_GbByEquipId;
    static void* g_Resolver20Addr  = nullptr;
    static void* g_Resolver40Addr  = nullptr;
    static void* g_Resolver150Addr = nullptr;
    static std::atomic<bool> g_ResolverHookTried{ false };
    static thread_local bool g_InSetupWeaponInfo = false;
    static thread_local std::uint32_t g_TlsSetupEquip = 0;

    static std::mutex g_WeaponKeyMutex;

    static std::map<std::uint32_t, std::uint32_t> g_FamilyFrom;
    static std::atomic<bool> g_HasSwaps{ false };
    static std::set<std::uint32_t> g_FamilyLogged;
    static std::map<std::uint32_t, void*> g_TemplateByEquipId;

    static uintptr_t SetupWeaponInfoAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x14105fdb0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x14105fe20ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x1410605f0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x141060640ull;
        default:                                          return 0;
        }
    }

    static std::uint32_t MapEquipId(std::uint32_t equipId)
    {
        if (!g_HasSwaps.load(std::memory_order_relaxed))
            return equipId;
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        const auto it = g_FamilyFrom.find(
            static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(equipId))));
        return it != g_FamilyFrom.end() ? it->second : equipId;
    }

    static int ReadU16SEH(const void* p, std::uint16_t& v)
    {
        __try
        {
            v = *static_cast<const std::uint16_t*>(p);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int WriteU8SEH(void* p, std::uint8_t v)
    {
        __try
        {
            *static_cast<std::uint8_t*>(p) = v;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::uint32_t GetEquipTypeForEquipId(std::uint32_t equipId)
    {
        if (equipId >= static_cast<std::uint32_t>(EquipIdCompression::kExtendedAllocFirst))
        {
            V_ExtendedEquipRow ext{};
            if (TppEquip_GetExtendedEquipRow(static_cast<int>(equipId), &ext))
                return static_cast<std::uint32_t>(ext.equipType) & 0x3F;
        }
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(static_cast<std::int32_t>(equipId));
        if (!EquipIdCompression::IsCompressedInBounds(idx))
            return 0;
        auto* words = static_cast<std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
        std::uint16_t w = 0;
        if (!words || ReadU16SEH(words + idx, w) != 1)
            return 0;
        return w & 0x3F;
    }

    static bool GetGbForEquipId(std::uint32_t engineOrRealId, FamilyGb& out)
    {
        const auto equipId =
            static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(engineOrRealId)));
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            const auto it = g_GbByEquipId.find(equipId);
            if (it != g_GbByEquipId.end())
            {
                out = it->second;
                return out.ok;
            }
        }
        FamilyGb f;
        if (equipId >= static_cast<std::uint32_t>(EquipIdCompression::kExtendedAllocFirst))
        {
            V_ExtendedEquipRow ext{};
            if (TppEquip_GetExtendedEquipRow(static_cast<int>(equipId), &ext)
                && ext.subId > 0 && ext.subId <= 0x3FF)
            {
                f.subId = static_cast<std::uint16_t>(ext.subId);
                if (GunBasic_ReadRowBytes(f.subId, f.gb))
                    f.ok = true;
            }
        }
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(static_cast<std::int32_t>(equipId));
        if (!f.ok && EquipIdCompression::IsCompressedInBounds(idx))
        {
            auto* words = static_cast<std::uint16_t*>(
                ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
            std::uint16_t w = 0;
            if (words && ReadU16SEH(words + idx, w) == 1)
            {
                f.subId = (w >> 6) & 0x3FF;
                if (f.subId != 0 && GunBasic_ReadRowBytes(f.subId, f.gb))
                    f.ok = true;
            }
        }
        if (f.ok && f.gb[0] != 0)
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            g_GbByEquipId[equipId] = f;
        }
        out = f;
        return f.ok && f.gb[0] != 0;
    }

}

int EquipParam_VanillaPartCount(int space)
{
    const PartBuffer* pb = SpaceBuffer(space);
    return pb ? pb->stockCount : 0;
}

int EquipParam_GetWideAlias(int space, int wideId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    WidePartState* w = WideStateFor(space, wideId);
    if (space == kVanillaSpace_Magazine && w && w->magazineDonor > 0)
        return w->magazineDonor;
    return (w && w->alias > 0 && w->alias <= 0xFF) ? w->alias : 0;
}

int EquipParam_WidePartRowState(int space, int wideId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    WidePartState* w = WideStateFor(space, wideId);
    if (!w)
        return -1;
    return (w->rowSet && !w->row.empty()) ? 1 : 0;
}

int EquipParam_GetWideReceiverDonor(int wideId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (WidePartState* w = WideStateFor(kVanillaSpace_Receiver, wideId))
    {
        if (w->alias > 0 && w->alias <= 0xFF)
            return w->alias;
        if (w->motionFrom > 0 && w->motionFrom < 234)
            return w->motionFrom;
    }
    const auto it = g_ReceiverMotionDonor.find(wideId);
    if (it != g_ReceiverMotionDonor.end() && it->second > 0 && it->second < 234)
        return it->second;
    if (wideId >= kWideIdBase)
    {
        const auto itDecl = g_DeclaredReceiverType.find(wideId);
        if (itDecl != g_DeclaredReceiverType.end())
            return LowestVanillaReceiverOfType(itDecl->second);
    }
    return 0;
}

bool EquipParam_InitializeWidePartIds()
{
    if (g_HookBatchMode || !g_OrigSetUpGunInfo)
    {
        LogError("PartIdWiden: SetUpGunInfo hook not live, wide part ids off");
        return false;
    }
    EnsurePartIdWidenArmed();
    if (!PartIdWiden_Install())
        return false;
    WideMagazine_Install();
    return true;
}

void EquipParam_EnableWidePartIds(int newMaxId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    PartBuffer* all[] = { &g_Magazine, &g_Stock, &g_Muzzle, &g_Sight, &g_Barrel,
                          &g_UnderBarrel, &g_Option, &g_Bullet, &g_RcvIdBuf };
    for (PartBuffer* pb : all)
    {
        if (!pb || pb->active || newMaxId <= pb->maxId)
            continue;
        pb->maxId = newMaxId;
    }
}

int EquipParam_GetReceiverForEquipId(int equipId)
{
    const int sub = TppEquip_GetSubIdForEquipId(equipId);
    if (sub <= 0)
        return 0;
    return GunBasic_GetLogicalPart(sub, kVanillaSpace_Receiver);
}

int EquipParam_GetDeclaredWeaponAttackId(int equipId)
{
    const int logical = EquipParam_GetReceiverForEquipId(equipId);
    if (logical < kWideIdBase)
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    WidePartState* w = WideStateFor(kVanillaSpace_Receiver, logical);
    if (w && w->rowSet && w->row.size() >= 2)
        return static_cast<int>(static_cast<unsigned>(w->row[0]) |
                                (static_cast<unsigned>(w->row[1]) << 8));

    if (!PartIdWiden_IsPlanned() || !g_RcvIdBuf.active || logical > g_RcvIdBuf.maxId)
        return 0;
    const int lo = PartRowByte(g_RcvIdBuf, logical, 0);
    const int hi = PartRowByte(g_RcvIdBuf, logical, 1);
    return static_cast<int>(static_cast<unsigned>(lo)
                            | (static_cast<unsigned>(hi) << 8));
}

namespace
{


    struct KeyTypeSwap
    {
        std::uint32_t* loc[16];
        std::uint32_t  saved[16];
        int n = 0;
    };

    static int SwapKeyWordsSEH(void* self, int slot,
                               const std::uint32_t* fromIds,
                               const std::uint32_t* toTypes, int m,
                               KeyTypeSwap* st)
    {
        __try
        {
            std::uint8_t* a = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + 0x8);
            if (!a) return 0;
            a = *reinterpret_cast<std::uint8_t**>(a + 0x138);
            if (!a) return 0;
            a = *reinterpret_cast<std::uint8_t**>(a + 0xC8);
            if (!a) return 0;
            std::uint8_t* obj = *reinterpret_cast<std::uint8_t**>(a + 0x8);
            if (!obj) return 0;
            const int stride = *reinterpret_cast<int*>(obj + 0xC);
            std::uint32_t* table = *reinterpret_cast<std::uint32_t**>(obj + 0x40);
            if (!table || stride <= 0 || stride > 16 || slot < 0 || slot > 15)
                return 0;
            for (int j = 0; j < stride && st->n < 16; ++j)
            {
                std::uint32_t* w = table + static_cast<size_t>(stride) * slot + j;
                const std::uint32_t v = *w;
                if ((v >> 16) == 0xFFFF)
                    continue;
                const std::uint32_t eq = (v & 0xFFFF) >> 5;
                for (int i = 0; i < m; ++i)
                {
                    if (eq == fromIds[i] && toTypes[i] >= 1 && toTypes[i] <= 8
                        && (v & 0x1F) != toTypes[i])
                    {
                        st->loc[st->n] = w;
                        st->saved[st->n] = v;
                        *w = (v & ~0x1Fu) | toTypes[i];
                        ++st->n;
                        break;
                    }
                }
            }
            return st->n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void RestoreKeyWordsSEH(KeyTypeSwap* st)
    {
        __try
        {
            for (int i = 0; i < st->n; ++i)
                *st->loc[i] = st->saved[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        st->n = 0;
    }

 
    static int ReadU32AtSEH(const void* base, size_t off, std::uint32_t& out);

    static void* g_GetAnimFromGaniAddr = nullptr;

    static uintptr_t GetAnimFileFromGaniPathAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141a6c300ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x141a6c270ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x141a6c590ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x141a6c6b0ull;
        default:                                          return 0;
        }
    }

    using RealizedEquipRealize_t =
        unsigned long long(__fastcall*)(void*, unsigned int, unsigned int);
    static RealizedEquipRealize_t g_OrigRealizedEquipRealize = nullptr;
    static void* g_RealizeAddr = nullptr;

    static uintptr_t RealizedEquipRealizeAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141307d70ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x141307d90ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x141308860ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x1413089a0ull;
        default:                                          return 0;
        }
    }

    static constexpr std::uint8_t kRealizePrologue[20] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D,
        0x6C, 0x24, 0xA0, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00 };

    static void* ReadPtrAtSEH(void* base, size_t off);
    static void* FollowThunkSeh(void* at);

    static void*            g_GunInfoAccessor = nullptr;
    static bool             g_LanePinned[256] = {};

    static int RefreshPinnedLanes()
    {
        void* rA   = g_GunInfoAccessor;
        void* pool = rA ? ReadPtrAtSEH(rA, 0x1d8) : nullptr;
        if (!pool)
            return -1;
        for (int i = 0; i < 256; ++i)
            g_LanePinned[i] = false;
        int occupied = 0;
        for (int e = 0; e < 48; ++e)
        {
            std::uint8_t* ep = static_cast<std::uint8_t*>(pool)
                + static_cast<std::size_t>(e) * 0x90;
            std::uint16_t tag = 0x7FF;
            if (ReadU16SEH(ep + 0x5e, tag) != 1)
                continue;
            if (tag == 0x7FF || tag == 0)
                continue;
            ++occupied;
            const int sub = TppEquip_GetSubIdForEquipId(static_cast<int>(tag));
            if (sub <= 0)
                continue;
            unsigned char row[12] = {};
            if (!GunBasic_ReadRowBytes(sub, row))
                continue;
            if (row[0])
                g_LanePinned[row[0]] = true;
        }
        return occupied;
    }

    static bool ZeroRcvLaneSEH(std::uint8_t* buf, int alias)
    {
        __try
        {
            std::memset(buf + static_cast<size_t>(alias - 1) * kReceiverStride,
                        0, kReceiverStride);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void EvictUnpinnedReceiverLanesLocked(std::vector<int>& freed)
    {
        freed.clear();
        if (RefreshPinnedLanes() < 0)
            return;

        std::uint8_t* buf = PartCurrentBuf(g_RcvIdBuf);
        if (!buf)
            return;

        for (auto& kv : g_WideParts[kVanillaSpace_Receiver])
        {
            WidePartState& w = kv.second;
            if (w.alias <= 0 || w.alias >= 256)
                continue;
            if (g_LanePinned[w.alias])
                continue;

            if (!ZeroRcvLaneSEH(buf, w.alias))
                continue;

            g_RcvClaimed.erase(w.alias);
            g_ReceiverMotionDonor.erase(w.alias);
            g_RcvOverflow.erase(w.alias);
            freed.push_back(w.alias);
            w.alias = 0;
        }
    }

    static bool PatchDescWideSlotsSEH(void* desc, const unsigned char lanes[12])
    {
        __try
        {
            std::uint8_t* d = static_cast<std::uint8_t*>(desc);
            for (int i = 0; i < 11; ++i)
                if (lanes[i])
                    d[i] = lanes[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void LaneWindow_BindForBuild(void* desc, unsigned int equipId)
    {
        if (!desc)
            return;
        const int sub = TppEquip_GetSubIdForEquipId(static_cast<int>(equipId));
        if (sub <= 0)
            return;

        unsigned char lanes[12] = {};
        if (!GunBasic_WeaponNeedsLaneBind(sub))
        {
            if (GunBasic_GetWideSlotLanes(sub, kVanillaSpace_Receiver, lanes) > 0)
                PatchDescWideSlotsSEH(desc, lanes);
            return;
        }

        std::vector<int> freed;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            EvictUnpinnedReceiverLanesLocked(freed);
        }
        for (int lane : freed)
            GunBasic_ClearLaneFromRows(kVanillaSpace_Receiver, lane);

        const int bound = GunBasic_RebindWidePartsForWeapon(sub);
        if (GunBasic_GetWideSlotLanes(sub, kVanillaSpace_Receiver, lanes) > 0)
            PatchDescWideSlotsSEH(desc, lanes);
        if (bound == 0)
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 2000)
            {
                lastMs = now;
                LogWarn("LaneWindow: equipId %u (weaponId %d): no free part lane, built without receiver",
                    equipId, sub);
            }
        }
    }

    using PostPutMotion_t =
        unsigned long long(__fastcall*)(void*, void*, void*, void*);
    static PostPutMotion_t g_OrigPostPutMotion = nullptr;
    static void* g_PostPutMotionAddr = nullptr;

    static constexpr std::uint8_t kPostPutMotionPrologue[] = {
        0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55, 0x56, 0x57, 0x41, 0x56,
        0x41, 0x57, 0x49, 0x8D, 0x6B, 0xC8, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00 };

    static uintptr_t PostPutMotionAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141307150ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x141307170ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x141307c50ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x141307d80ull;
        default:                                          return 0;
        }
    }

    static unsigned ReadRecWord(void* base, int slot, int off)
    {
        const int lo = ReadByteAtSEH(base, static_cast<size_t>(slot) * 0x14 + off);
        const int hi = ReadByteAtSEH(base, static_cast<size_t>(slot) * 0x14 + off + 1);
        if (lo < 0 || hi < 0)
            return 0xFFFFFFFFu;
        return static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
    }

    static thread_local unsigned t_probeSub[8] = { 0 };
    static thread_local int t_probeBones[8] = { 0 };
    static thread_local int t_probeC[8] = { 0 };
    static thread_local unsigned t_probeAmmo[8] = { 0 };

    static thread_local unsigned t_meshEq = 0;
    static thread_local int t_meshCount = 0;
    static thread_local unsigned t_meshHash[64] = { 0 };
    static thread_local unsigned char t_meshFA[64] = { 0 };
    static thread_local unsigned char t_meshFB[64] = { 0 };
    static thread_local void* t_cylCtrl = nullptr;
    static thread_local int t_cylSlot = 0;
    static thread_local void* t_cylModel = nullptr;
    static thread_local unsigned t_cylAmmo = 0;
    static thread_local int t_roundCount = 0;

    static void FeedCylinderVisibilitySEH(void* self)
    {
        t_meshCount = 0;
        t_cylCtrl = nullptr;
        __try
        {
            std::uint8_t* p = static_cast<std::uint8_t*>(self);
            void* ctrl = *reinterpret_cast<void**>(p + 0x30);
            void* recsV = *reinterpret_cast<void**>(p + 0xE8);
            void* p60 = *reinterpret_cast<void**>(p + 0x60);
            void* p58 = *reinterpret_cast<void**>(p + 0x58);
            if (!ctrl || !recsV || !p60 || !p58) return;
            void* ctrlModels = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(ctrl) + 0x58);
            void* map = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(p60) + 0x10);
            void* rows = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(p58) + 0x28);
            if (!ctrlModels || !map || !rows) return;
            const int slotCount = *reinterpret_cast<int*>(p + 0x50);
            const int lim = slotCount < 0 ? 0 : (slotCount > 8 ? 8 : slotCount);
            const int base54 = *reinterpret_cast<int*>(p + 0x54);
            std::uint8_t* recs = static_cast<std::uint8_t*>(recsV);
            for (int slot = 0; slot < lim; ++slot)
            {
                std::uint8_t* rec = recs + static_cast<size_t>(slot) * 0x14;
                t_probeSub[slot] = (static_cast<unsigned>(rec[4]) |
                                    (static_cast<unsigned>(rec[5]) << 8)) & 0x7FFu;
                void* model = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(ctrlModels) + static_cast<size_t>(slot) * 8);
                const int bones = model
                    ? *reinterpret_cast<std::int16_t*>(static_cast<std::uint8_t*>(model) + 0xF8)
                    : -1;
                t_probeBones[slot] = bones;
                const unsigned idx = *reinterpret_cast<unsigned*>(
                    static_cast<std::uint8_t*>(map) + static_cast<size_t>(base54 + slot) * 4);
                int b0C = -1;
                unsigned ammo = 0;
                if (idx != 0xFFFFFFFFu)
                {
                    std::uint8_t* row = static_cast<std::uint8_t*>(rows) +
                                        static_cast<size_t>(idx) * 0xE;
                    b0C = row[0xC];
                    ammo = *reinterpret_cast<std::uint16_t*>(row + 4);
                }
                t_probeC[slot] = b0C;
                t_probeAmmo[slot] = ammo;
                if ((b0C & 0x20) && bones > 0 && !t_cylCtrl)
                {
                    t_cylCtrl = ctrl;
                    t_cylSlot = slot;
                    t_cylModel = model;
                    t_cylAmmo = ammo;
                    const unsigned mc = *reinterpret_cast<std::uint16_t*>(
                        static_cast<std::uint8_t*>(model) + 0x1E8);
                    void* harr = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(model) + 0x180);
                    void* fa = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(model) + 0x178);
                    void* fb = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(model) + 0x170);
                    if (harr && mc)
                    {
                        const unsigned n = mc > 64 ? 64 : mc;
                        for (unsigned i = 0; i < n; ++i)
                        {
                            t_meshHash[i] = *reinterpret_cast<std::uint32_t*>(
                                static_cast<std::uint8_t*>(harr) + i * 4);
                            t_meshFA[i] = fa ? *(static_cast<std::uint8_t*>(fa) + i) : 0xFF;
                            t_meshFB[i] = fb ? *(static_cast<std::uint8_t*>(fb) + i) : 0xFF;
                        }
                        t_meshCount = static_cast<int>(n);
                        t_meshEq = t_probeSub[slot];
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void DriveMeshSEH(void* ctrl, unsigned slot, unsigned long long hash, int flag)
    {
        __try
        {
            void** vt = *reinterpret_cast<void***>(ctrl);
            auto fn = reinterpret_cast<void(__fastcall*)(void*, unsigned, unsigned long long, int)>(
                vt[0x100 / 8]);
            fn(ctrl, slot, hash, flag);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    struct RoundMeshList { unsigned hash[16]; int count; };
    static std::mutex g_roundsMeshMx;
    static std::map<void*, RoundMeshList> g_roundsMesh;
    static void DriveCylinderRounds()
    {
        t_roundCount = 0;
        if (!t_cylCtrl || t_meshCount <= 0) return;
        RoundMeshList rl;
        {
            std::lock_guard<std::mutex> lock(g_roundsMeshMx);
            auto it = g_roundsMesh.find(t_cylModel);
            if (it != g_roundsMesh.end())
            {
                rl = it->second;
            }
            else
            {
                rl.count = 0;
                for (int i = 0; i < t_meshCount && rl.count < 16; ++i)
                    if (t_meshFA[i] == 0xFF)
                        rl.hash[rl.count++] = t_meshHash[i];
                g_roundsMesh[t_cylModel] = rl;
            }
        }
        const int show = t_cylAmmo > 16u ? 16 : static_cast<int>(t_cylAmmo);
        for (int i = 0; i < rl.count; ++i)
            DriveMeshSEH(t_cylCtrl, static_cast<unsigned>(t_cylSlot),
                         static_cast<unsigned long long>(rl.hash[i]), i < show ? 1 : 0);
        t_roundCount = rl.count;
    }

    struct CylBoneCandidate { unsigned hash; const char* name; };
    static const CylBoneCandidate kCylBoneCandidates[] = {
        { 0x69E02A5Eu, "SKL_002_CYLINDER" },
        { 0x23E96556u, "SKL_001_CYLINDER" },
        { 0xD7E97EC3u, "SKL_003_CYLINDER" },
        { 0x23227DE0u, "SKL_004_CYLINDER" },
        { 0xC0889846u, "SKL_005_CYLINDER" },
        { 0x8D655BE1u, "SKL_006_CYLINDER" },
        { 0xF7719044u, "WEAPON_CYLINDER" },
        { 0x26A4DD2Fu, "CYLINDER" },
    };

    struct CylRotState
    {
        int boneIdx = -2;
        unsigned chambers = 5;
        double stepRad = 0.0;
        unsigned prevAmmo = 0xFFFFFFFFu;
        double target = 0.0;
        double current = 0.0;
        bool haveBase = false;
        float base[16] = {};
        float lastWritten[16] = {};
    };
    static std::mutex g_cylRotMx;
    static std::map<void*, CylRotState> g_cylRot;

    static int ResolveBoneIdxSEH(void* ctrl, unsigned slot, unsigned hash)
    {
        __try
        {
            void** vt = *reinterpret_cast<void***>(ctrl);
            auto fn = reinterpret_cast<unsigned long long(__fastcall*)(
                void*, unsigned long long, unsigned long long, unsigned long long)>(vt[0x140 / 8]);
            return static_cast<int>(fn(ctrl, slot, static_cast<unsigned long long>(hash), 0));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static bool ReadMtx64SEH(void* model, int idx, float* out16)
    {
        __try
        {
            void* pose = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(model) + 0xE0);
            if (!pose) return false;
            memcpy(out16, static_cast<std::uint8_t*>(pose) + static_cast<size_t>(idx) * 0x40, 0x40);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool WriteMtx64SEH(void* model, int idx, const float* in16)
    {
        __try
        {
            void* pose = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(model) + 0xE0);
            if (!pose) return false;
            memcpy(static_cast<std::uint8_t*>(pose) + static_cast<size_t>(idx) * 0x40, in16, 0x40);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void DriveCylinderRotation()
    {
        if (!t_cylCtrl || !t_cylModel) return;
        std::lock_guard<std::mutex> lock(g_cylRotMx);
        CylRotState& st = g_cylRot[t_cylModel];
        if (st.boneIdx == -2)
        {
            st.boneIdx = -1;
            for (const auto& cand : kCylBoneCandidates)
            {
                const int idx = ResolveBoneIdxSEH(
                    t_cylCtrl, static_cast<unsigned>(t_cylSlot), cand.hash);
                if (idx >= 0) { st.boneIdx = idx; break; }
            }
            st.chambers = (t_roundCount > 0) ? static_cast<unsigned>(t_roundCount) : 5u;
            double declaredDeg = 0.0;
            const bool declared =
                ReceiverMotion_CylinderShotDegrees(t_meshEq, declaredDeg);
            st.stepRad = declared
                             ? declaredDeg * 0.017453292519943295
                             : 6.283185307179586 /
                                   static_cast<double>(st.chambers ? st.chambers : 5u);
        }
        if (st.boneIdx < 0) return;

        float cur[16];
        if (!ReadMtx64SEH(t_cylModel, st.boneIdx, cur)) return;

        const float basis = fabsf(cur[0]) + fabsf(cur[1]) + fabsf(cur[2]) +
                            fabsf(cur[4]) + fabsf(cur[5]) + fabsf(cur[6]) +
                            fabsf(cur[8]) + fabsf(cur[9]) + fabsf(cur[10]);
        if (basis < 0.25f) return;

        if (!st.haveBase || memcmp(cur, st.lastWritten, sizeof(cur)) != 0)
        {
            memcpy(st.base, cur, sizeof(cur));
            st.haveBase = true;
        }
        if (st.prevAmmo == 0xFFFFFFFFu) st.prevAmmo = t_cylAmmo;
        if (t_cylAmmo != st.prevAmmo && st.chambers > 0)
        {
            int delta = static_cast<int>(st.prevAmmo) - static_cast<int>(t_cylAmmo);
            if (delta < 0) delta = -delta;
            const double step = st.stepRad;
            st.target += step * static_cast<double>(delta);
        }
        st.prevAmmo = t_cylAmmo;
        st.current += (st.target - st.current) * 0.35;
        if (st.current >= 6.283185307179586 && st.target >= 6.283185307179586)
        {
            st.current -= 6.283185307179586;
            st.target -= 6.283185307179586;
        }
        const float c = static_cast<float>(cos(st.current));
        const float s = static_cast<float>(sin(st.current));
        float m[16];
        for (int j = 0; j < 4; ++j)
        {
            m[0 * 4 + j] =  c * st.base[0 * 4 + j] + s * st.base[1 * 4 + j];
            m[1 * 4 + j] = -s * st.base[0 * 4 + j] + c * st.base[1 * 4 + j];
            m[2 * 4 + j] = st.base[2 * 4 + j];
            m[3 * 4 + j] = st.base[3 * 4 + j];
        }
        WriteMtx64SEH(t_cylModel, st.boneIdx, m);
        memcpy(st.lastWritten, m, sizeof(m));
    }

    static bool WritePtrAtSEH(void* base, size_t off, void* val)
    {
        __try
        {
            *reinterpret_cast<void**>(static_cast<std::uint8_t*>(base) + off) = val;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uint32_t GetNativeSubId(std::uint32_t equipId)
    {
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(static_cast<std::int32_t>(equipId));
        if (!EquipIdCompression::IsCompressedInBounds(idx))
            return 0;
        auto* words = static_cast<std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
        std::uint16_t w = 0;
        if (!words || ReadU16SEH(words + idx, w) != 1)
            return 0;
        return (w >> 6) & 0x3FF;
    }

    constexpr std::uint32_t kMotionShadowRows = 0x10000;

    static void*              g_MotionShadow = nullptr;
    static std::atomic<bool>  g_MotionShadowActive{ false };
    static std::atomic<void*> g_EngineMotionTable{ nullptr };

    static void* MotionEntryEngineTable();

    static bool MotionBytesMatchSEH(std::uintptr_t va, const std::uint8_t* want,
                                    std::size_t n)
    {
        __try
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(va);
            for (std::size_t i = 0; i < n; ++i)
                if (p[i] != want[i])
                    return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static int MotionReadBytesSEH(std::uintptr_t va, std::uint8_t* out, std::size_t n)
    {
        __try
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(va);
            for (std::size_t i = 0; i < n; ++i)
                out[i] = p[i];
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool MotionWriteCodeSEH(std::uintptr_t va, const std::uint8_t* src,
                                   std::size_t n)
    {
        DWORD prot = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(va), n,
                            PAGE_EXECUTE_READWRITE, &prot))
            return false;
        bool ok = true;
        __try
        {
            std::memcpy(reinterpret_cast<void*>(va), src, n);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        DWORD back = 0;
        VirtualProtect(reinterpret_cast<void*>(va), n, prot, &back);
        return ok;
    }

    static std::uintptr_t MotionEntryIndexSiteAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141307ef2ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x141307f12ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x1413089e2ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x141308b22ull;
        default:                                          return 0;
        }
    }

    static std::mutex g_MotionShadowArmMutex;

    static void PutRel32(std::uint8_t* dst, std::uintptr_t nextAddr,
                         std::uintptr_t target)
    {
        const auto rel = static_cast<std::int32_t>(
            static_cast<std::int64_t>(target) - static_cast<std::int64_t>(nextAddr));
        std::memcpy(dst, &rel, sizeof(rel));
    }

    static void* MotionTryAllocIn(const MEMORY_BASIC_INFORMATION& mbi, std::size_t size,
                                  std::uintptr_t gran, std::uintptr_t lo,
                                  std::uintptr_t hi)
    {
        if (mbi.State != MEM_FREE)
            return nullptr;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t regionEnd = regionBase + mbi.RegionSize;
        std::uintptr_t base = (regionBase + gran - 1) & ~(gran - 1);
        if (base < lo)
            base = (lo + gran - 1) & ~(gran - 1);
        for (; base + size <= regionEnd && base <= hi; base += gran)
            if (void* p = VirtualAlloc(reinterpret_cast<LPVOID>(base), size,
                                       MEM_RESERVE | MEM_COMMIT,
                                       PAGE_EXECUTE_READWRITE))
                return p;
        return nullptr;
    }

    static void* MotionAllocNear(std::uintptr_t nearAddr, std::size_t size)
    {
        if (void* arena = HookArena::AllocateNear(nearAddr, size))
            return arena;
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const std::uintptr_t gran  = si.dwAllocationGranularity;
        const std::uintptr_t reach = 0x78000000ull;
        const std::uintptr_t lo    = (nearAddr > reach) ? (nearAddr - reach) : gran;
        const std::uintptr_t hi    = nearAddr + reach;

        for (std::uintptr_t a = nearAddr & ~(gran - 1); a <= hi; )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                break;
            if (void* p = MotionTryAllocIn(mbi, size, gran, lo, hi))
                return p;
            const auto next = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)
                + mbi.RegionSize;
            if (next <= a)
                break;
            a = next;
        }
        for (std::uintptr_t a = nearAddr & ~(gran - 1); a > lo; )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                break;
            if (void* p = MotionTryAllocIn(mbi, size, gran, lo, hi))
                return p;
            const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            if (base < gran)
                break;
            a = base - gran;
        }
        return nullptr;
    }

    static void ArmMotionEntryShadow()
    {
        if (g_MotionShadowActive.load(std::memory_order_acquire))
            return;

        std::lock_guard<std::mutex> armLock(g_MotionShadowArmMutex);
        if (g_MotionShadowActive.load(std::memory_order_acquire))
            return;

        static int attempts = 0;
        constexpr int kMaxAttempts = 4096;
        if (attempts > kMaxAttempts)
            return;
        ++attempts;

        auto* orig = static_cast<std::uint8_t*>(
            reinterpret_cast<void*>(ResolveGameAddress(gAddr.Equip_MotionEntryTable)));
        if (!orig)
            return;

        static const std::uint8_t kExpect[44] = {
            0x83, 0xFE, 0x7C, 0x7E, 0x0B, 0x8D, 0x8E, 0x00, 0xFC, 0xFF, 0xFF,
            0x83, 0xF9, 0x4F, 0x77, 0x2F, 0x48, 0x8B, 0x47, 0x70, 0x8B, 0xCE,
            0x81, 0xFE, 0x00, 0x04, 0x00, 0x00, 0x7C, 0x06,
            0x8D, 0x8E, 0x7D, 0xFC, 0xFF, 0xFF,
            0x48, 0x63, 0xD1, 0x48, 0x8B, 0x54, 0xD0, 0x08 };
        constexpr std::size_t kPatchLen = 36;

        const std::uintptr_t site = reinterpret_cast<std::uintptr_t>(
            ResolveGameAddress(MotionEntryIndexSiteAddr()));
        if (!site)
        {
            attempts = kMaxAttempts + 1;
            LogError("WeaponKey: motion-entry index site missing on this build, custom equipIds get no motion entry");
            return;
        }
        if (!MotionBytesMatchSEH(site, kExpect, sizeof(kExpect)))
        {
            attempts = kMaxAttempts + 1;
            LogError("WeaponKey: motion-entry index site 0x%llX mismatch, custom equipIds get no motion entry",
                static_cast<unsigned long long>(site));
            return;
        }

        const std::size_t bytes = 8 + static_cast<std::size_t>(kMotionShadowRows) * 8;
        void* shadow = VirtualAlloc(nullptr, bytes,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!shadow)
        {
            attempts = kMaxAttempts + 1;
            LogError("WeaponKey: motion-entry shadow alloc (%zu bytes) failed, custom equipIds get no motion entry",
                bytes);
            return;
        }
        std::memset(shadow, 0, bytes);

        MotionEntryEngineTable();

        constexpr std::size_t kTrampBytes = 72;
        void* tramp = MotionAllocNear(site, kTrampBytes);
        if (!tramp)
        {
            attempts = kMaxAttempts + 1;
            VirtualFree(shadow, 0, MEM_RELEASE);
            LogError("WeaponKey: no trampoline page near 0x%llX, custom equipIds get no motion entry",
                static_cast<unsigned long long>(site));
            return;
        }

        const std::uintptr_t backAddr  = site + 36;
        const std::uintptr_t bailAddr  = site + 63;
        const auto           trampBase = reinterpret_cast<std::uintptr_t>(tramp);

        std::uint8_t body[kTrampBytes] = {
            0x83, 0xFE, 0x7C,
            0x7F, 0x0B,
            0x48, 0x8B, 0x47, 0x70,
            0x8B, 0xCE,
            0xE9, 0, 0, 0, 0,
            0x8D, 0x8E, 0x00, 0xFC, 0xFF, 0xFF,
            0x83, 0xF9, 0x4F,
            0x77, 0x0F,
            0x48, 0x8B, 0x47, 0x70,
            0x8D, 0x8E, 0x7D, 0xFC, 0xFF, 0xFF,
            0xE9, 0, 0, 0, 0,
            0x81, 0xFE, 0xFF, 0xFF, 0x00, 0x00,
            0x77, 0x11,
            0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
            0x8B, 0xCE,
            0xE9, 0, 0, 0, 0,
            0xE9, 0, 0, 0, 0 };

        const std::uint64_t shadowBase = reinterpret_cast<std::uint64_t>(shadow);
        std::memcpy(body + 52, &shadowBase, sizeof(shadowBase));
        PutRel32(body + 12, trampBase + 16, backAddr);
        PutRel32(body + 38, trampBase + 42, backAddr);
        PutRel32(body + 63, trampBase + 67, backAddr);
        PutRel32(body + 68, trampBase + 72, bailAddr);
        std::memcpy(tramp, body, sizeof(body));

        std::uint8_t patch[kPatchLen];
        std::memset(patch, 0x90, sizeof(patch));
        patch[0] = 0xE9;
        PutRel32(patch + 1, site + 5, trampBase);

        if (!MotionWriteCodeSEH(site, patch, sizeof(patch)))
        {
            attempts = kMaxAttempts + 1;
            VirtualFree(shadow, 0, MEM_RELEASE);
            LogError("WeaponKey: motion-entry code write at 0x%llX failed, custom equipIds get no motion entry",
                static_cast<unsigned long long>(site));
            return;
        }

        g_MotionShadow = shadow;
        g_MotionShadowActive.store(true, std::memory_order_release);
    }

    static uintptr_t MotionEntryTableVtblAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x1422a0f18ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x1422a0ed8ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x1422a0de8ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x1422a0d68ull;
        default:                                          return 0;
        }
    }

    static void* MotionEntryEngineTable()
    {
        if (void* t = g_EngineMotionTable.load(std::memory_order_acquire))
            return t;
        void* obj = reinterpret_cast<void*>(
            ResolveGameAddress(gAddr.Equip_MotionEntryTable));
        void* vtbl = ResolveGameAddress(MotionEntryTableVtblAddr());
        void* held = obj ? ReadPtrAtSEH(obj, 0) : nullptr;
        if (!obj || !vtbl || held != vtbl)
        {
            static std::atomic<bool> warned{ false };
            if (!warned.exchange(true))
                LogError("WeaponKey: motion-entry table vtable %p is not EquipMotionDataTable %p, motion aliases off",
                    held, vtbl);
            return nullptr;
        }
        g_EngineMotionTable.store(obj, std::memory_order_release);
        return obj;
    }

    static bool MotionEntrySlot(std::uint32_t equipId, void** table, int* idx)
    {
        if (equipId >= 1 && equipId < 0x7D)
        {
            *table = MotionEntryEngineTable();
            *idx   = static_cast<int>(equipId);
            return *table != nullptr;
        }
        if (equipId >= 0x400 && equipId < 0x450)
        {
            *table = MotionEntryEngineTable();
            *idx   = static_cast<int>(equipId) - 899;
            return *table != nullptr;
        }
        if (g_MotionShadowActive.load(std::memory_order_acquire) && g_MotionShadow
            && equipId >= 1 && equipId < kMotionShadowRows)
        {
            *table = g_MotionShadow;
            *idx   = static_cast<int>(equipId);
            return true;
        }
        return false;
    }

    static bool MotionEntryHasSlot(std::uint32_t equipId)
    {
        void* table = nullptr;
        int   idx   = 0;
        return MotionEntrySlot(equipId, &table, &idx);
    }

    static void* ReadMotionEntry(std::uint32_t equipId)
    {
        void* table = nullptr;
        int   idx   = 0;
        if (!MotionEntrySlot(equipId, &table, &idx))
            return nullptr;
        return ReadPtrAtSEH(table, 8 + static_cast<size_t>(idx) * 8);
    }

    static bool WriteMotionEntry(std::uint32_t equipId, void* entry)
    {
        void* table = nullptr;
        int   idx   = 0;
        if (!MotionEntrySlot(equipId, &table, &idx))
            return false;
        if (const std::int32_t alias = equip::CurrentAliasOf(static_cast<std::int32_t>(equipId)))
            WriteMotionEntry(static_cast<std::uint32_t>(alias), entry);
        return WritePtrAtSEH(table, 8 + static_cast<size_t>(idx) * 8, entry);
    }

    static std::uint32_t NextDonorCandidate(std::uint32_t v)
    {
        if (v + 1 == 0x7D)
            return 0x400;
        if (v + 1 == 0x450)
            return 0;
        return v + 1;
    }

    struct MotionResidencyPlan
    {
        std::uint32_t donorEq = 0;
        std::uint64_t donorPack = 0;
        std::uint64_t familyPack = 0;
    };
    static std::map<std::uint32_t, MotionResidencyPlan> g_MotionResidency;
    static std::map<std::uint64_t, std::uint32_t> g_RemapEntryEq;

    using MtarGetAnimFileFn_t = void*(__fastcall*)(void*, unsigned long long);

    static std::atomic<void*> g_EquipDataProvider{ nullptr };

    static void CaptureEquipDataProvider(void* reoi)
    {
        if (!reoi || g_EquipDataProvider.load(std::memory_order_relaxed))
            return;
        void* provider = ReadPtrAtSEH(reoi, 0x68);
        void* vt = provider ? ReadPtrAtSEH(provider, 0) : nullptr;
        void* fn = vt ? ReadPtrAtSEH(vt, 0x10) : nullptr;
        if (!fn)
            return;
        g_EquipDataProvider.store(provider, std::memory_order_relaxed);
    }

    static void* ResolveMtarForEntrySEH(unsigned long long entry)
    {
        if (void* resident = MtarRegistry_Lookup(entry))
            return resident;
        void* provider = g_EquipDataProvider.load(std::memory_order_relaxed);
        if (!provider)
            return nullptr;
        __try
        {
            void* vt = *reinterpret_cast<void**>(provider);
            if (!vt)
                return nullptr;
            void* fn = *reinterpret_cast<void**>(static_cast<char*>(vt) + 0x10);
            if (!fn)
                return nullptr;
            return reinterpret_cast<MtarGetAnimFileFn_t>(fn)(provider, entry);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void* CallGetAnimFileSEH(void* fn, void* mtar, unsigned long long clip)
    {
        __try
        {
            return reinterpret_cast<MtarGetAnimFileFn_t>(fn)(mtar, clip);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(1));
        }
    }

    static int ReadDwordAtSEH2(void* base, size_t off)
    {
        int v = 0;
        for (int i = 0; i < 4; ++i)
            v |= (ReadByteAtSEH(base, off + static_cast<size_t>(i)) & 0xFF) << (8 * i);
        return v;
    }

    static MtarGetAnimFileFn_t g_OrigGetAnimFile = nullptr;
    static void* g_GetAnimFileAddr = nullptr;

    static std::mutex g_LiveMtarMutex;
    static void* g_LiveMtars[256];
    static int g_LiveMtarCount = 0;

    static void NoteLiveMtar(void* mtar)
    {
        if (!mtar)
            return;
        MtarRegistry_ArmFromLiveArchive(mtar);
        std::lock_guard<std::mutex> lock(g_LiveMtarMutex);
        for (int i = 0; i < g_LiveMtarCount; ++i)
            if (g_LiveMtars[i] == mtar)
                return;
        if (g_LiveMtarCount >= 256)
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LogWarn("WeaponKey: over 256 motion archives resident, clips in later archives refused");
            }
            return;
        }
        g_LiveMtars[g_LiveMtarCount++] = mtar;
    }

    static void ForgetLiveMtar(void* mtar)
    {
        if (!mtar)
            return;
        std::lock_guard<std::mutex> lock(g_LiveMtarMutex);
        for (int i = 0; i < g_LiveMtarCount; ++i)
            if (g_LiveMtars[i] == mtar)
            {
                g_LiveMtars[i] = g_LiveMtars[--g_LiveMtarCount];
                g_LiveMtars[g_LiveMtarCount] = nullptr;
                return;
            }
    }

    static int SnapshotLiveMtars(void** out, int max)
    {
        std::lock_guard<std::mutex> lock(g_LiveMtarMutex);
        int n = 0;
        for (int i = 0; i < g_LiveMtarCount && n < max; ++i)
            out[n++] = g_LiveMtars[i];
        return n;
    }
    static thread_local void* t_clipSetCtl = nullptr;
    static thread_local unsigned int t_clipSetSlot = 0;

    static bool ClipRigFitsModelSEH(void* af, void* ctl, unsigned int slot);

    static void* FindClipInRegisteredArchivesSEH(void* mtar, unsigned long long clip)
    {
        std::uint64_t cands[96];
        int cnt = 0;
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            for (const auto& kv : g_RemapEntryEq)
            {
                if (cnt >= 96)
                    break;
                cands[cnt++] = kv.first;
            }
        }
        for (int i = 0; i < cnt; ++i)
        {
            void* m = ResolveMtarForEntrySEH(cands[i]);
            if (!m || m == mtar)
                continue;
            void* af = CallGetAnimFileSEH(
                reinterpret_cast<void*>(g_OrigGetAnimFile), m, clip);
            if (af && reinterpret_cast<uintptr_t>(af) != 1)
            {
                if (t_clipSetCtl
                    && !ClipRigFitsModelSEH(af, t_clipSetCtl, t_clipSetSlot))
                    continue;
                return af;
            }
        }
        void* live[256];
        const int ln = SnapshotLiveMtars(live, 256);
        for (int i = 0; i < ln; ++i)
        {
            if (live[i] == mtar)
                continue;
            void* af = CallGetAnimFileSEH(
                reinterpret_cast<void*>(g_OrigGetAnimFile), live[i], clip);
            if (af && reinterpret_cast<uintptr_t>(af) != 1)
            {
                if (t_clipSetCtl
                    && !ClipRigFitsModelSEH(af, t_clipSetCtl, t_clipSetSlot))
                    continue;
                return af;
            }
        }
        return nullptr;
    }

    static void* __fastcall hkGetAnimFile(void* mtar, unsigned long long clip)
    {
        NoteLiveMtar(mtar);
        if (void* quiet = QuietWeaponMotion_GetAnimFile(mtar, clip, reinterpret_cast<void*>(g_OrigGetAnimFile)))
            return quiet;
        void* swap = outfit::RedirectMotionMtarForClip(mtar);
        NoteLiveMtar(swap);
        void* r = g_OrigGetAnimFile(swap ? swap : mtar, clip);
        if (!r && swap)
            r = g_OrigGetAnimFile(mtar, clip);
        if (r || !mtar || !t_clipSetCtl)
            return r;
        static thread_local int depth = 0;
        if (depth)
            return r;
        ++depth;
        void* alt = FindClipInRegisteredArchivesSEH(mtar, clip);
        --depth;
        if (!alt)
            return r;
        return alt;
    }

    using ClipSetFn_t =
        unsigned char(__fastcall*)(void*, unsigned int, unsigned long long);
    static ClipSetFn_t g_OrigClipSetByPath = nullptr;
    static void** g_ClipFallbackVtbl = nullptr;
    static int g_ClipFallbackSlot = -1;

    struct LastClipState
    {
        unsigned long long clip;
        bool foreign;
    };
    static std::map<std::uint64_t, LastClipState> g_LastClipByCtlSlot;
    static std::map<std::uintptr_t, LastClipState> g_LastClipByCtl;

    static void RecordClipState(void* ctl, unsigned int slot, unsigned long long clip,
                                bool foreign)
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        if (g_LastClipByCtlSlot.size() > 512)
            g_LastClipByCtlSlot.clear();
        if (g_LastClipByCtl.size() > 128)
            g_LastClipByCtl.clear();
        const std::uint64_t key =
            (reinterpret_cast<std::uint64_t>(ctl) << 8) | (slot & 0xFF);
        g_LastClipByCtlSlot[key] = { clip, foreign };
        g_LastClipByCtl[reinterpret_cast<std::uintptr_t>(ctl)] = { clip, foreign };
    }

    static bool ClipForeignToBoundArchive(void* ctl, unsigned int slot,
                                          unsigned long long clip)
    {
        if (!g_OrigGetAnimFile)
            return false;
        void* slots = ReadPtrAtSEH(ctl, 0x60);
        void* outer = slots ? ReadPtrAtSEH(slots, static_cast<size_t>(slot) * 8)
                            : nullptr;
        if (!outer)
            return false;
        std::uint8_t* inner =
            static_cast<std::uint8_t*>(outer) + ReadDwordAtSEH2(outer, 0xc);
        void* bound = ReadPtrAtSEH(inner, 0x80);
        if (!bound)
            return false;
        void* af = CallGetAnimFileSEH(
            reinterpret_cast<void*>(g_OrigGetAnimFile), bound, clip);
        return !af || reinterpret_cast<uintptr_t>(af) == 1;
    }

    using AnimGetDataInfoFn_t = void(__fastcall*)(void*, void*);

    static bool CallGetDataInfoSEH(void* fn, void* animFile, void* out)
    {
        __try
        {
            reinterpret_cast<AnimGetDataInfoFn_t>(fn)(animFile, out);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static int  ModelBoneCapSEH(void* ctl, int idx);

    static bool ClipRigFitsModelSEH(void* af, void* ctl, unsigned int slot)
    {
        void* getDataInfo = ResolveGameAddress(gAddr.Mtar_GetDataInfo);
        if (!getDataInfo)
            return false;
        unsigned char info[0x60] = {};
        if (!CallGetDataInfoSEH(getDataInfo, af, info))
            return false;
        void* dataPtr = *reinterpret_cast<void**>(info);
        if (!dataPtr)
            return false;
        const int tracks = ReadDwordAtSEH2(dataPtr, 0);
        const int cap = ModelBoneCapSEH(ctl, static_cast<int>(slot));
        if (tracks <= 0 || cap <= 0 || tracks > cap)
        {
            static std::atomic<int> budget{ 8 };
            if (budget.fetch_sub(1) > 0)
                LogWarn("WeaponKey: cross-archive clip refused, %d tracks but model has %d bones", tracks, cap);
            return false;
        }
        return true;
    }

    static unsigned char __fastcall hkClipArchiveFallback(void* ctl, unsigned int slot,
                                                          unsigned long long clip)
    {

        {
            void* mdlArr = ReadPtrAtSEH(ctl, 0x58);
            void* model = mdlArr ? ReadPtrAtSEH(mdlArr, static_cast<size_t>(slot) * 8) : nullptr;
            if (!model)
                return 0;
        }
        struct ClipSetScope
        {
            void* prevCtl;
            unsigned int prevSlot;
            ClipSetScope(void* c, unsigned int sl)
                : prevCtl(t_clipSetCtl), prevSlot(t_clipSetSlot)
            {
                t_clipSetCtl = c;
                t_clipSetSlot = sl;
            }
            ~ClipSetScope()
            {
                t_clipSetCtl = prevCtl;
                t_clipSetSlot = prevSlot;
            }
        } clipSetScope(ctl, slot);

        const unsigned char r = g_OrigClipSetByPath(ctl, slot, clip);
        if (r)
        {
            RecordClipState(ctl, slot, clip, ClipForeignToBoundArchive(ctl, slot, clip));
            return r;
        }
        std::uint64_t cands[96];
        int cnt = 0;
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            for (const auto& kv : g_RemapEntryEq)
            {
                if (cnt >= 96)
                    break;
                cands[cnt++] = kv.first;
            }
        }
        if (cnt == 0)
            return 0;
        void* gaf = ResolveGameAddress(gAddr.Mtar_GetAnimFile);
        if (!gaf)
            return 0;
        void* slots = ReadPtrAtSEH(ctl, 0x60);
        void* outer = slots ? ReadPtrAtSEH(slots, static_cast<size_t>(slot) * 8)
                            : nullptr;
        if (!outer)
            return 0;
        std::uint8_t* inner =
            static_cast<std::uint8_t*>(outer) + ReadDwordAtSEH2(outer, 0xc);
        void* bound = ReadPtrAtSEH(inner, 0x80);
        void* afBound = bound ? CallGetAnimFileSEH(gaf, bound, clip) : nullptr;
        if (afBound && reinterpret_cast<uintptr_t>(afBound) != 1)
            return 0;
        for (int i = 0; i < cnt; ++i)
        {
            void* m = ResolveMtarForEntrySEH(cands[i]);
            if (!m || m == bound)
                continue;
            void* af = CallGetAnimFileSEH(gaf, m, clip);
            if (!af || reinterpret_cast<uintptr_t>(af) == 1)
                continue;
            if (!ClipRigFitsModelSEH(af, ctl, slot))
                continue;
            if (!WritePtrAtSEH(inner, 0x80, m))
                continue;
            const unsigned char r2 = g_OrigClipSetByPath(ctl, slot, clip);
            if (r2)
            {
                RecordClipState(ctl, slot, clip, true);
                return r2;
            }
            WritePtrAtSEH(inner, 0x80, bound);
        }
        {
            void* live[256];
            const int ln = SnapshotLiveMtars(live, 256);
            for (int i = 0; i < ln; ++i)
            {
                if (live[i] == bound)
                    continue;
                void* af = CallGetAnimFileSEH(gaf, live[i], clip);
                if (!af || reinterpret_cast<uintptr_t>(af) == 1)
                    continue;
                if (!ClipRigFitsModelSEH(af, ctl, slot))
                    continue;
                if (!WritePtrAtSEH(inner, 0x80, live[i]))
                    continue;
                const unsigned char r3 = g_OrigClipSetByPath(ctl, slot, clip);
                if (r3)
                {
                    RecordClipState(ctl, slot, clip, true);
                    return r3;
                }
                WritePtrAtSEH(inner, 0x80, bound);
            }
        }
        return 0;
    }

    using HideMagazine_t =
        void(__fastcall*)(void*, unsigned long long, unsigned char);
    static HideMagazine_t g_OrigHideMagazine = nullptr;
    static void* g_HideMagazineAddr = nullptr;

    using GetPartsControllerFn_t =
        void*(__fastcall*)(void*, unsigned int, unsigned int*);

    static void* CallGetPartsControllerSEH(void* equipSys, unsigned int key,
                                           unsigned int* outIdx)
    {
        auto fn = reinterpret_cast<GetPartsControllerFn_t>(
            ResolveGameAddress(gAddr.EquipSystemImpl_GetPartsController));
        if (!fn)
            return nullptr;
        __try
        {
            return fn(static_cast<char*>(equipSys) - 0x20, key, outIdx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void __fastcall hkHideMagazine(void* self, unsigned long long handle,
                                          unsigned char which)
    {
        const std::uint32_t eqpType = static_cast<std::uint32_t>(handle) & 0x1f;
        const std::uint32_t eq = (static_cast<std::uint32_t>(handle) >> 5) & 0x7ff;
        if (which == 0 && eqpType >= 1 && eqpType <= 8 &&
            TppEquip_GetSubIdForEquipId(static_cast<int>(eq)) != 0)
        {
            unsigned int idx = 0;
            void* ctl = CallGetPartsControllerSEH(
                self, static_cast<unsigned int>(handle), &idx);
            bool foreign = false;
            if (ctl)
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                const std::uint64_t key =
                    (reinterpret_cast<std::uint64_t>(ctl) << 8) | (idx & 0xFF);
                auto it = g_LastClipByCtlSlot.find(key);
                if (it != g_LastClipByCtlSlot.end())
                {
                    foreign = it->second.foreign;
                }
                else
                {
                    auto itc =
                        g_LastClipByCtl.find(reinterpret_cast<std::uintptr_t>(ctl));
                    if (itc != g_LastClipByCtl.end())
                    {
                        foreign = itc->second.foreign;
                    }
                }
            }
            if (foreign)
                return;
        }
        g_OrigHideMagazine(self, handle, which);
    }

    static ClipSetFn_t g_OrigSetMotionByPathReal = nullptr;
    static void* g_SetMotionByPathRealAddr = nullptr;

    static unsigned char __fastcall hkSetMotionByPathSafe(void* ctl, unsigned int slot,
                                                          unsigned long long clip)
    {
        void* mdlArr = ReadPtrAtSEH(ctl, 0x58);
        void* model = mdlArr ? ReadPtrAtSEH(mdlArr, static_cast<size_t>(slot) * 8) : nullptr;
        if (!model)
            return 0;
        __try
        {
            return g_OrigSetMotionByPathReal(ctl, slot, clip);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    using PartsVoidSetterFn_t =
        void(__fastcall*)(void*, unsigned int, unsigned long long, unsigned long long);

    static bool PartsSlotModelNullSEH(void* self, unsigned int index)
    {
        void* mdlArr = ReadPtrAtSEH(self, 0x58);
        void* model = mdlArr ? ReadPtrAtSEH(mdlArr, static_cast<size_t>(index) * 8) : nullptr;
        return model == nullptr;
    }

    static PartsVoidSetterFn_t g_OrigPartsThermo = nullptr;
    static PartsVoidSetterFn_t g_OrigPartsB0 = nullptr;
    static PartsVoidSetterFn_t g_OrigPartsVisMesh = nullptr;

    static std::atomic<int> g_PartsSlotNullLogged{ 0 };

    static void NotePartsSlotModelNull(unsigned int index, const char* label)
    {
        if (g_PartsSlotNullLogged.load(std::memory_order_relaxed) >= 24)
            return;
        if (g_PartsSlotNullLogged.fetch_add(1, std::memory_order_relaxed) >= 24)
            return;
        LogWarn("EquipParam: parts slot %u has no model (%s), part not drawn; check its .parts/fpk",
            index, label);
    }

    static void __fastcall hkPartsThermoSafe(void* self, unsigned int index,
                                             unsigned long long a2, unsigned long long a3)
    {
        if (PartsSlotModelNullSEH(self, index))
        {
            NotePartsSlotModelNull(index, "SetThermoGraphy(vtbl+0x1a0)");
            return;
        }
        __try { g_OrigPartsThermo(self, index, a2, a3); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkPartsB0Safe(void* self, unsigned int index,
                                         unsigned long long a2, unsigned long long a3)
    {
        if (PartsSlotModelNullSEH(self, index))
        {
            NotePartsSlotModelNull(index, "partsSetter(vtbl+0xb0)");
            return;
        }
        __try { g_OrigPartsB0(self, index, a2, a3); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkPartsVisMeshSafe(void* self, unsigned int index,
                                              unsigned long long a2, unsigned long long a3)
    {
        if (PartsSlotModelNullSEH(self, index))
        {
            NotePartsSlotModelNull(index, "SetVisibilityMesh(vtbl+0x100)");
            return;
        }
        __try { g_OrigPartsVisMesh(self, index, a2, a3); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    struct PartsSlotGuardDef
    {
        uintptr_t addr;
        void* hook;
        void** orig;
        const char* label;
    };

    struct PartsSlotGuardAddrs
    {
        uintptr_t thermo;
        uintptr_t partsSetter;
        uintptr_t visibilityMesh;
    };

    static bool PartsSlotTargetInImage(uintptr_t addr)
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base || addr <= base)
            return false;
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + static_cast<uintptr_t>(dos->e_lfanew));
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return false;
            return addr < base + nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static PartsSlotGuardAddrs PartsSlotGuardAddrsForBuild()
    {
        PartsSlotGuardAddrs out{ 0, 0, 0 };
        auto* vtbl = static_cast<void**>(
            reinterpret_cast<void*>(ResolveGameAddress(gAddr.SimplePartsControllerImpl_Vtable)));
        if (!vtbl)
            return out;
        const auto thermo = reinterpret_cast<uintptr_t>(
            ReadPtrAtSEH(vtbl, 0x1a0));
        const auto setter = reinterpret_cast<uintptr_t>(
            ReadPtrAtSEH(vtbl, 0xb0));
        const auto visMesh = reinterpret_cast<uintptr_t>(
            ReadPtrAtSEH(vtbl, 0x100));
        if (!PartsSlotTargetInImage(thermo) || !PartsSlotTargetInImage(setter)
            || !PartsSlotTargetInImage(visMesh))
        {
            LogError("EquipParam: parts-controller vtable %p stale on %s, empty-slot guard off",
                static_cast<void*>(vtbl), GetGameBuildName(gGameBuild));
            return out;
        }
        out.thermo = thermo;
        out.partsSetter = setter;
        out.visibilityMesh = visMesh;
        return out;
    }

    static void* g_PartsSlotGuardAddrs[3] = { nullptr, nullptr, nullptr };

    static void InstallPartsSlotModelGuards()
    {
        const PartsSlotGuardAddrs a = PartsSlotGuardAddrsForBuild();
        if (!a.thermo && !a.partsSetter && !a.visibilityMesh)
            return;
        const PartsSlotGuardDef defs[3] = {
            { a.thermo, reinterpret_cast<void*>(&hkPartsThermoSafe),
              reinterpret_cast<void**>(&g_OrigPartsThermo),  "SetThermoGraphy(vtbl+0x1a0)" },
            { a.partsSetter, reinterpret_cast<void*>(&hkPartsB0Safe),
              reinterpret_cast<void**>(&g_OrigPartsB0),      "partsSetter(vtbl+0xb0)" },
            { a.visibilityMesh, reinterpret_cast<void*>(&hkPartsVisMeshSafe),
              reinterpret_cast<void**>(&g_OrigPartsVisMesh), "SetVisibilityMesh(vtbl+0x100)" },
        };
        for (int i = 0; i < 3; ++i)
        {
            if (!defs[i].addr)
                continue;
            void* target = reinterpret_cast<void*>(defs[i].addr);
            if (!target)
                continue;
            if (CreateAndEnableHook(target, defs[i].hook, defs[i].orig))
                g_PartsSlotGuardAddrs[i] = target;
            else
                LogError("EquipParam: parts-slot guard %s hook failed, empty slots can crash", defs[i].label);
        }
    }

    static void RemovePartsSlotModelGuards()
    {
        for (int i = 0; i < 3; ++i)
        {
            if (g_PartsSlotGuardAddrs[i])
            {
                DisableAndRemoveHook(g_PartsSlotGuardAddrs[i]);
                g_PartsSlotGuardAddrs[i] = nullptr;
            }
        }
        g_OrigPartsThermo = nullptr;
        g_OrigPartsB0 = nullptr;
        g_OrigPartsVisMesh = nullptr;
    }

    static void ArmClipArchiveFallback()
    {
        void** vtbl = static_cast<void**>(
            reinterpret_cast<void*>(ResolveGameAddress(gAddr.SimplePartsControllerImpl_Vtable)));
        void* target = ResolveGameAddress(gAddr.SimplePartsControllerImpl_SetMotionDataByPath);
        if (!vtbl || !target)
        {
            LogError("WeaponKey: SetMotionDataByPath address missing on %s, clip archive fallback off",
                GetGameBuildName(gGameBuild));
            return;
        }
        const int slot = 0x1b0 / 8;
        if (FollowThunkSeh(vtbl[slot]) != FollowThunkSeh(target))
        {
            LogError("WeaponKey: SimplePartsControllerImpl vtbl+0x1B0 is %p, not %p, clip archive fallback off",
                vtbl[slot], target);
            return;
        }
        DWORD prot = 0;
        if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_READWRITE, &prot))
        {
            LogError("WeaponKey: VirtualProtect on vtbl+0x1B0 failed (%lu), clip archive fallback off",
                GetLastError());
            return;
        }
        g_OrigClipSetByPath = reinterpret_cast<ClipSetFn_t>(vtbl[slot]);
        vtbl[slot] = reinterpret_cast<void*>(&hkClipArchiveFallback);
        VirtualProtect(&vtbl[slot], sizeof(void*), prot, &prot);
        g_ClipFallbackVtbl = vtbl;
        g_ClipFallbackSlot = slot;
    }

    static void DisarmClipArchiveFallback()
    {
        if (!g_ClipFallbackVtbl || g_ClipFallbackSlot < 0 || !g_OrigClipSetByPath)
            return;
        DWORD prot = 0;
        if (VirtualProtect(&g_ClipFallbackVtbl[g_ClipFallbackSlot], sizeof(void*),
                           PAGE_READWRITE, &prot))
        {
            g_ClipFallbackVtbl[g_ClipFallbackSlot] =
                reinterpret_cast<void*>(g_OrigClipSetByPath);
            VirtualProtect(&g_ClipFallbackVtbl[g_ClipFallbackSlot], sizeof(void*),
                           prot, &prot);
        }
        g_ClipFallbackVtbl = nullptr;
        g_ClipFallbackSlot = -1;
        g_OrigClipSetByPath = nullptr;
    }

    static const char* const kChimeraCatDirs[4] = {
        "receiver", "barrel", "magazine", "underBarrel" };
    static const char* const kPlayerPkgPrefixes[4] = {
        "/Assets/tpp/pack/player/motion/equip/receiver/pl_rcvr_",
        "/Assets/tpp/pack/player/motion/equip/barrel/pl_brrl_",
        "/Assets/tpp/pack/player/motion/equip/magazine/pl_mgzn_",
        "/Assets/tpp/pack/player/motion/equip/under_barrel/pl_ubrrl_" };

    static std::unordered_map<std::uint64_t, std::pair<int, std::string>>
        g_FamilyRootByPkg;
    static std::once_flag g_FamilyRootOnce;

    static void BuildFamilyRootTable()
    {
        g_FamilyRootByPkg.reserve(4 * 26 * 26 * 100);
        for (int c = 0; c < 4; ++c)
        {
            std::string p = std::string(kPlayerPkgPrefixes[c]) + "aa00.fpk";
            const size_t off = std::strlen(kPlayerPkgPrefixes[c]);
            for (char a = 'a'; a <= 'z'; ++a)
                for (char b = 'a'; b <= 'z'; ++b)
                    for (int d = 0; d < 100; ++d)
                    {
                        p[off] = a;
                        p[off + 1] = b;
                        p[off + 2] = static_cast<char>('0' + d / 10);
                        p[off + 3] = static_cast<char>('0' + d % 10);
                        g_FamilyRootByPkg.emplace(
                            FoxHashes::PathCode64Ext(p),
                            std::make_pair(c, p.substr(off, 4)));
                    }
        }
    }

    static void WarmFamilyRootTable()
    {
        std::thread([] { std::call_once(g_FamilyRootOnce, BuildFamilyRootTable); })
            .detach();
    }

    static void RecoverFamilyRoot(std::uint64_t pkg, int& cat, std::string& root)
    {
        std::call_once(g_FamilyRootOnce, BuildFamilyRootTable);
        cat = -1;
        root.clear();
        auto it = g_FamilyRootByPkg.find(pkg);
        if (it != g_FamilyRootByPkg.end())
        {
            cat = it->second.first;
            root = it->second.second;
        }
    }

    static std::set<std::uint64_t> g_ChimeraPackNames;

    static bool ScanChimeraPackArraySEH(void* base, std::uint64_t fpkHash,
                                        int* validCount)
    {
        const std::uint64_t want = fpkHash & 0x7FFFFFFFFFFFFFFFull;
        bool found = false;
        int valid = 0;
        __try
        {
            const std::uint64_t* p = static_cast<const std::uint64_t*>(base);
            for (int i = 0; i < 512 && !found; ++i, p += 8)
            {
                const std::uint64_t fpk = p[1] & 0x7FFFFFFFFFFFFFFFull;
                if ((fpk >> 56) != 0x52ull)
                    continue;
                ++valid;
                if (fpk == want)
                    found = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        if (validCount)
            *validCount = valid;
        return found;
    }

    using ReloadChimeraParts_t = int(__fastcall*)(lua_State*);
    static ReloadChimeraParts_t g_OrigReloadChimeraParts = nullptr;
    static void* g_ReloadChimeraPartsAddr = nullptr;

    static bool ReloadChimeraPartsTargetVerified(void* target)
    {
        static const std::uint8_t kPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
            0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
            0x4C, 0x8D, 0x05 };
        static const std::uint8_t kTopOfStack[] = { 0x83, 0xCA, 0xFF };
        static const char kField[] = "packageInfos";
        const auto va = reinterpret_cast<std::uintptr_t>(target);
        if (!va || !MotionBytesMatchSEH(va, kPrologue, sizeof(kPrologue))
            || !MotionBytesMatchSEH(va + 0x1b, kTopOfStack, sizeof(kTopOfStack)))
            return false;
        std::uint8_t disp[4] = {};
        if (!MotionReadBytesSEH(va + 0x17, disp, sizeof(disp)))
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, disp, sizeof(rel));
        const std::uintptr_t field = va + 0x1b + static_cast<std::intptr_t>(rel);
        return MotionBytesMatchSEH(field,
            reinterpret_cast<const std::uint8_t*>(kField), sizeof(kField));
    }

    static int __fastcall hkReloadChimeraPartsInfoTable(lua_State* L)
    {
        if (ResolveLuaApi() && g_lua_getfield && g_lua_rawgeti && g_lua_settop &&
            g_lua_objlen && g_lua_type && g_lua_tolstring)
        {
            std::set<std::uint64_t> names;
            g_lua_getfield(L, -1, const_cast<char*>("packageInfos"));
            if (g_lua_type(L, -1) == LUA_TTABLE)
            {
                const int rows = static_cast<int>(g_lua_objlen(L, -1));
                for (int i = 1; i <= rows; ++i)
                {
                    g_lua_rawgeti(L, -1, i);
                    if (g_lua_type(L, -1) == LUA_TTABLE)
                    {
                        g_lua_rawgeti(L, -1, 2);
                        if (g_lua_type(L, -1) == LUA_TSTRING)
                        {
                            const char* s = g_lua_tolstring(L, -1, nullptr);
                            if (s && *s)
                                names.insert(FoxHashes::PathCode64Ext(s));
                        }
                        g_lua_settop(L, -2);
                    }
                    g_lua_settop(L, -2);
                }
            }
            g_lua_settop(L, -2);
            if (!names.empty())
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                g_ChimeraPackNames.swap(names);
            }
        }
        return g_OrigReloadChimeraParts ? g_OrigReloadChimeraParts(L) : 0;
    }

    static bool ChimeraPackTableContains(std::uint64_t fpkHash, bool* tableValid)
    {
        if (tableValid)
            *tableValid = false;
        if (!fpkHash)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            if (!g_ChimeraPackNames.empty())
            {
                if (tableValid)
                    *tableValid = true;
                return g_ChimeraPackNames.count(fpkHash) != 0;
            }
        }
        void* base = ReadPtrAtSEH(
            reinterpret_cast<void*>(ResolveGameAddress(gAddr.Equip_ChimeraPartsPackageInfos)), 0);
        if (!base)
            return false;
        int valid = 0;
        const bool found = ScanChimeraPackArraySEH(base, fpkHash, &valid);
        if (tableValid)
            *tableValid = valid > 0;
        return found;
    }

    static std::uint64_t FindFamilyChimeraPack(int cat, const std::string& root)
    {
        static std::mutex mx;
        static std::map<std::string, std::uint64_t> cache;
        const std::string key = std::string(kChimeraCatDirs[cat]) + "/" + root;
        {
            std::lock_guard<std::mutex> lock(mx);
            auto it = cache.find(key);
            if (it != cache.end())
                return it->second;
        }
        static const char* const kMidByCat[4][2] = {
            { "_main", nullptr },
            { "_main", "_barl" },
            { "_main", "_ammo" },
            { "_main", nullptr } };
        static const char* const kTails[2] = { "_def_v00.fpk", "_def.fpk" };
        const std::string base =
            std::string("/Assets/tpp/pack/collectible/chimera/")
            + kChimeraCatDirs[cat] + "/" + root;
        std::uint64_t found = 0;
        bool anyValid = false;
        for (int m = 0; m < 2 && !found; ++m)
        {
            if (!kMidByCat[cat][m])
                continue;
            for (int v = 0; v < 10 && !found; ++v)
                for (int t = 0; t < 2 && !found; ++t)
                {
                    const std::uint64_t h = FoxHashes::PathCode64Ext(
                        base + kMidByCat[cat][m]
                        + static_cast<char>('0' + v) + kTails[t]);
                    bool tableValid = false;
                    if (ChimeraPackTableContains(h, &tableValid))
                        found = h;
                    anyValid = anyValid || tableValid;
                }
        }
        if (found || anyValid)
        {
            std::lock_guard<std::mutex> lock(mx);
            cache[key] = found;
        }
        return found;
    }

    static std::uint64_t RegisterFamilyArchives(const std::string& root,
                                                std::uint32_t equipId)
    {
        static const char* const kSuffixes[] = {
            "_default", "_under", "_under_1", "_under_2", "_under_3", "_under_4",
            "_under_5", "_fullauto" };
        std::uint64_t defEntry = 0;
        for (const char* s : kSuffixes)
        {
            const std::uint64_t h = FoxHashes::PathCode64Ext(
                "/Assets/tpp/motion/mtar/equip/chimera/receiver/" + root + s
                + ".mtar");
            if (!h)
                continue;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                g_RemapEntryEq[h] = equipId;
            }
            if (s == kSuffixes[0])
                defEntry = h;
        }
        const std::uint64_t asmEntry = FoxHashes::PathCode64Ext(
            "/Assets/tpp/motion/mtar/equip/chimera/assemble/" + root + "_asm.mtar");
        if (asmEntry)
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            g_RemapEntryEq[asmEntry] = equipId;
        }
        return defEntry;
    }

    static std::uint64_t GetNativeRowPackHash(std::uint32_t equipId)
    {
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(static_cast<std::int32_t>(equipId));
        if (!EquipIdCompression::IsCompressedInBounds(idx))
            return 0;
        auto* infoList = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_InfoList));
        if (!infoList)
            return 0;
        return reinterpret_cast<std::uint64_t>(
            ReadPtrAtSEH(infoList, static_cast<size_t>(idx) * 0x18 + 8));
    }

    struct FoxPathArray
    {
        std::uint32_t Count;
        std::uint32_t Capacity;
        std::uint64_t* Data;
    };

    using FoxArrayExtend_t =
        bool(__fastcall*)(void*, std::uint32_t, std::uint32_t, void*);

    static bool FoxArrayExtendVerified()
    {
        static std::atomic<int> s_state{ 0 };
        const int known = s_state.load(std::memory_order_acquire);
        if (known != 0)
            return known > 0;
        static const std::uint8_t kPrologue[] = {
            0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
            0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20 };
        static const std::uint8_t kElemSizeRead[] = { 0x48, 0x8B, 0x49, 0x20 };
        static const std::uint8_t kAlignRead[] = {
            0x48, 0x0F, 0xAF, 0xCA, 0x48, 0x8B, 0x57, 0x18 };
        void* extend = ResolveGameAddress(gAddr.Fox_ArrayBaseExtend);
        const auto body = reinterpret_cast<std::uintptr_t>(FollowThunkSeh(extend));
        void* ops = ResolveGameAddress(gAddr.Fox_ArrayOperatorVtbl);
        bool opsOk = ops != nullptr;
        for (std::size_t k = 0; opsOk && k < 4; ++k)
            opsOk = PartsSlotTargetInImage(
                reinterpret_cast<uintptr_t>(ReadPtrAtSEH(ops, k * 8)));
        const bool ok = body
            && MotionBytesMatchSEH(body, kPrologue, sizeof(kPrologue))
            && MotionBytesMatchSEH(body + 0x17, kElemSizeRead, sizeof(kElemSizeRead))
            && MotionBytesMatchSEH(body + 0x24, kAlignRead, sizeof(kAlignRead))
            && opsOk
            && ReadPtrAtSEH(ops, 0) == ReadPtrAtSEH(ops, 0x20);
        int expected = 0;
        if (s_state.compare_exchange_strong(expected, ok ? 1 : -1) && !ok)
            LogError("WeaponKey: fox::ArrayBase::Extend mismatch on %s, full pack lists not grown",
                GetGameBuildName(gGameBuild));
        return ok;
    }

    static bool AppendPackPath(void* arr_, std::uint64_t h)
    {
        if (!arr_ || !h)
            return false;
        auto* arr = static_cast<FoxPathArray*>(arr_);
        __try
        {
            for (std::uint32_t i = 0; i < arr->Count; ++i)
                if (arr->Data[i] == h)
                    return true;
            if (arr->Count >= arr->Capacity)
            {
                struct
                {
                    void** data;
                    std::uint32_t* count;
                    std::uint32_t* cap;
                    std::uint64_t elemSize;
                    std::uint64_t align;
                } info = { reinterpret_cast<void**>(&arr->Data), &arr->Count,
                           &arr->Capacity, 8, 8 };
                std::uint64_t opObj = reinterpret_cast<std::uint64_t>(
                    ResolveGameAddress(gAddr.Fox_ArrayOperatorVtbl));
                auto extend = reinterpret_cast<FoxArrayExtend_t>(
                    ResolveGameAddress(gAddr.Fox_ArrayBaseExtend));
                if (!extend || !opObj || !FoxArrayExtendVerified())
                    return false;
                const std::uint32_t newCap = arr->Count ? arr->Count * 2 : 16;
                if (!extend(&info, newCap, 0xd0001u, &opObj))
                    return false;
            }
            arr->Data[arr->Count] = h;
            arr->Count += 1;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void RegisterCustomFamilyFallbacks()
    {
        static std::set<std::uint32_t> s_done;
        for (std::uint32_t eq = 1; eq < 0x450; ++eq)
        {
            if (eq >= 0x7D && eq < 0x36D)
                continue;
            if (TppEquip_GetSubIdForEquipId(static_cast<int>(eq)) == 0)
                continue;
            const std::uint32_t t = GetEquipTypeForEquipId(eq) & 0x1F;
            if (t < 1 || t > 8)
                continue;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                if (!s_done.insert(eq).second)
                    continue;
            }
            FamilyGb cus;
            if (!GetGbForEquipId(eq, cus))
                continue;
            int rc = cus.gb[0];
            if (rc >= 234)
            {
                const auto itd = g_ReceiverMotionDonor.find(rc);
                rc = (itd != g_ReceiverMotionDonor.end()) ? itd->second : 0;
            }
            if (rc <= 0 || rc >= 234)
                continue;
            std::string root;
            if (!ResolveDonorSoundRoot(rc, root) || root.empty())
                continue;

            RegisterFamilyArchives(root, eq);
            const std::uint64_t ph = FindFamilyChimeraPack(0, root);
            if (ph)
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                g_MotionResidency[eq].familyPack = ph;
            }
            else
                LogWarn("WeaponKey: equipId=%u family '%s' has no chimera receiver pack, missing clips refused",
                    eq, root.c_str());
        }
    }

    static void FillCustomMotionEntriesTable()
    {
        std::vector<std::uint32_t> ids;
        if (g_MotionShadowActive.load(std::memory_order_acquire))
        {
            std::vector<int> custom;
            TppEquip_GetCustomWeaponEquipIds(custom);
            ids.reserve(custom.size());
            for (int c : custom)
                if (c > 0 && static_cast<std::uint32_t>(c) < kMotionShadowRows)
                    ids.push_back(static_cast<std::uint32_t>(c));
        }
        else
        {
            for (std::uint32_t e = 1; e != 0; e = NextDonorCandidate(e))
                ids.push_back(e);
        }

        for (std::uint32_t eq : ids)
        {
            const int subId = TppEquip_GetSubIdForEquipId(static_cast<int>(eq));
            if (subId == 0)
                continue;
            const std::uint32_t t = GetEquipTypeForEquipId(eq) & 0x1F;
            if (t < 1 || t > 8)
                continue;
            if (const std::uint64_t ownMtar = ReceiverMotion_WeaponArchiveForEquip(eq))
            {
                if (ReadMotionEntry(eq) == reinterpret_cast<void*>(ownMtar))
                    continue;
                if (!MotionEntryHasSlot(eq))
                {
                    static std::set<std::uint32_t> noSlot;
                    if (noSlot.insert(eq).second)
                        LogWarn("ReceiverMotion: equipId=%u: motion-entry table invalid, weapon archive not bound", eq);
                    continue;
                }
                if (WriteMotionEntry(eq, reinterpret_cast<void*>(ownMtar)))
                    continue;
                static std::set<std::uint32_t> refused;
                if (refused.insert(eq).second)
                    LogWarn("WeaponKey: equipId=%u motion-entry write faulted, weapon archive not bound yet", eq);
            }
            if (ReadMotionEntry(eq))
                continue;
            std::uint32_t donor = 0;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                auto it = g_FamilyFrom.find(eq);
                if (it != g_FamilyFrom.end())
                    donor = it->second;
            }
            if (!MotionEntryHasSlot(donor) && subId < 514)
            {
                for (std::uint32_t v = 1; v != 0; v = NextDonorCandidate(v))
                {
                    if (v == eq)
                        continue;
                    const std::uint32_t vt = GetEquipTypeForEquipId(v) & 0x1F;
                    if (vt < 1 || vt > 8)
                        continue;
                    if (GetNativeSubId(v) == static_cast<std::uint32_t>(subId) &&
                        TppEquip_GetSubIdForEquipId(static_cast<int>(v)) == 0 &&
                        ReadMotionEntry(v))
                    {
                        donor = v;
                        break;
                    }
                }
            }
            if (!MotionEntryHasSlot(donor))
            {
                FamilyGb cus;
                if (GetGbForEquipId(eq, cus))
                {
                    const int sub =
                        TppEquip_GetSubIdForEquipId(static_cast<int>(eq));
                    const int logical = (sub > 0)
                        ? GunBasic_GetLogicalPart(sub, kVanillaSpace_Receiver)
                        : 0;
                    int rc = (logical > 0) ? logical : cus.gb[0];
                    if (rc >= kWideIdBase)
                    {
                        WidePartState* w =
                            WideStateFor(kVanillaSpace_Receiver, rc);
                        if (w && w->motionFrom > 0)
                            rc = w->motionFrom;
                    }
                    if (rc >= 234)
                    {
                        const auto itd = g_ReceiverMotionDonor.find(rc);
                        rc = (itd != g_ReceiverMotionDonor.end()) ? itd->second : 0;
                    }
                    if (rc > 0 && rc < 234)
                    {
                        EnsureRcvTypeExt();
                        const int ctype =
                            g_RcvTypeExtReady ? g_RcvTypeExt[rc] : -1;
                        std::string cusRoot;
                        ResolveDonorSoundRoot(rc, cusRoot);
                        std::uint32_t rootDonor = 0, typeDonor = 0;
                        for (std::uint32_t v = 1; v != 0; v = NextDonorCandidate(v))
                        {
                            if (v == eq)
                                continue;
                            const std::uint32_t vt = GetEquipTypeForEquipId(v) & 0x1F;
                            if (vt < 1 || vt > 8)
                                continue;
                            if (TppEquip_GetSubIdForEquipId(static_cast<int>(v)) != 0)
                                continue;
                            FamilyGb van;
                            if (!GetGbForEquipId(v, van))
                                continue;
                            if (!ReadMotionEntry(v))
                                continue;
                            if (van.gb[0] == static_cast<unsigned char>(rc))
                            {
                                donor = v;
                                break;
                            }
                            if (rootDonor == 0 && !cusRoot.empty() && van.gb[0] < 234)
                            {
                                std::string vroot;
                                if (ResolveDonorSoundRoot(van.gb[0], vroot) &&
                                    vroot == cusRoot)
                                    rootDonor = v;
                            }
                            if (typeDonor == 0 && ctype > 0 &&
                                g_RcvTypeExt[van.gb[0]] == ctype)
                                typeDonor = v;
                        }
                        if (!MotionEntryHasSlot(donor) && rootDonor != 0)
                            donor = rootDonor;
                        if (!MotionEntryHasSlot(donor) && typeDonor != 0)
                            donor = typeDonor;
                    }
                }
            }
            void* donorEntry = ReadMotionEntry(donor);
            if (!donorEntry)
            {
                static std::set<std::uint32_t> noDonorLogged;
                bool first = false;
                {
                    std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                    first = noDonorLogged.insert(eq).second;
                }
                FamilyGb cus;
                int rcRaw = -1, rcRes = -1, ctype = -1;
                std::string cusRoot;
                if (GetGbForEquipId(eq, cus))
                {
                    const int dsub =
                        TppEquip_GetSubIdForEquipId(static_cast<int>(eq));
                    const int dlogical = (dsub > 0)
                        ? GunBasic_GetLogicalPart(dsub, kVanillaSpace_Receiver)
                        : 0;
                    rcRaw = (dlogical > 0) ? dlogical : cus.gb[0];
                    rcRes = rcRaw;
                    if (rcRes >= 234)
                    {
                        const auto itd = g_ReceiverMotionDonor.find(rcRes);
                        rcRes = (itd != g_ReceiverMotionDonor.end()) ? itd->second : -1;
                    }
                    EnsureRcvTypeExt();
                    if (g_RcvTypeExtReady && rcRes >= 0 && rcRes < kPartTypeExtCount)
                        ctype = g_RcvTypeExt[rcRes];
                    if (rcRes > 0 && rcRes < 234)
                        ResolveDonorSoundRoot(rcRes, cusRoot);
                }
                if (!cusRoot.empty())
                {
                    static int hasherValidated = 0;
                    if (hasherValidated == 0)
                    {
                        void* known = ReadMotionEntry(27);
                        const std::uint64_t check = FoxHashes::PathCode64Ext(
                            "/Assets/tpp/motion/mtar/equip/chimera/assemble/ar00_asm.mtar");
                        hasherValidated =
                            (!known || check == reinterpret_cast<std::uint64_t>(known))
                                ? 1 : -1;
                        if (hasherValidated < 0)
                            LogError("WeaponKey: path hash %016llX != ar00 entry %p, motion entry synthesis off",
                                check, known);
                    }
                    if (hasherValidated > 0)
                    {
                        const std::string paths[2] = {
                            "/Assets/tpp/motion/mtar/equip/chimera/receiver/"
                                + cusRoot + "_default.mtar",
                            "/Assets/tpp/motion/mtar/equip/chimera/assemble/"
                                + cusRoot + "_asm.mtar"
                        };
                        for (const std::string& p : paths)
                        {
                            const std::uint64_t h = FoxHashes::PathCode64Ext(p);
                            if (!h)
                                continue;
                            if (WriteMotionEntry(eq,
                                                 reinterpret_cast<void*>(h)))
                            {
                                {
                                    std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                                    g_RemapEntryEq[h] = eq;
                                }
                                if (&p == &paths[0])
                                {
                                    const std::uint64_t ph =
                                        FindFamilyChimeraPack(0, cusRoot);
                                    if (ph)
                                    {
                                        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                                        g_MotionResidency[eq].familyPack = ph;
                                    }
                                    else if (first)
                                        LogWarn("WeaponKey: equipId=%u family '%s' has no receiver pack, its mtar is not mounted",
                                            eq, cusRoot.c_str());
                                }
                            }
                            break;
                        }
                        if (ReadMotionEntry(eq))
                            continue;
                    }
                }
                if (first)
                    LogWarn("WeaponKey: equipId=%u subId=%d rc=%d type=%d: no motion donor; set familyFrom or weaponMotion",
                        eq, subId, rcRaw, ctype);
                continue;
            }
            if (WriteMotionEntry(eq, donorEntry))
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                auto& plan = g_MotionResidency[eq];
                plan.donorEq = donor;
                plan.donorPack = GetNativeRowPackHash(donor);
                g_RemapEntryEq[reinterpret_cast<std::uint64_t>(donorEntry)] = eq;
            }
        }
    }

    static void FillCustomMotionEntriesEarly()
    {
        RegisterCustomFamilyFallbacks();
        FillCustomMotionEntriesTable();
    }

    static void FillCustomMotionEntries(void* self)
    {
        if (void* live = ReadPtrAtSEH(self, 0x70))
            g_EngineMotionTable.store(live, std::memory_order_release);
        FillCustomMotionEntriesTable();
    }

    static void* GetEquipMotionLoaderIface()
    {
        using QST_t = std::uint8_t*(__fastcall*)();
        auto qst = reinterpret_cast<QST_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!qst)
            return nullptr;
        __try
        {
            std::uint8_t* t = qst();
            if (!t)
                return nullptr;
            std::uint8_t* app = *reinterpret_cast<std::uint8_t**>(t + 0x98);
            if (!app)
                return nullptr;
            std::uint8_t* q = *reinterpret_cast<std::uint8_t**>(app + 0x1e8);
            if (!q)
                return nullptr;
            return *reinterpret_cast<void**>(q + 0x18);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static std::uint32_t CallMtarIdsSEH(void* fn, void* self, std::uint8_t* out,
                                        std::uint32_t* eq)
    {
        if (!fn)
            return 0;
        __try
        {
            return reinterpret_cast<
                std::uint32_t(__fastcall*)(void*, std::uint8_t*, std::uint32_t*)>(fn)(
                self, out, eq);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static std::uint64_t CallPathByIdSEH(void* fn, void* self, std::uint32_t id)
    {
        if (!fn)
            return 0;
        __try
        {
            std::uint64_t out[2] = {};
            auto* p = reinterpret_cast<
                std::uint64_t*(__fastcall*)(void*, std::uint64_t*, std::uint32_t)>(fn)(
                self, out, id);
            return p ? *p : out[0];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    using AddMtarBlockPackagePaths_t = void(__fastcall*)(void*, std::uint32_t);
    static AddMtarBlockPackagePaths_t g_OrigAddMtarBlockPackagePaths = nullptr;
    static void* g_AddMtarBlockPackagePathsAddr = nullptr;

    static uintptr_t AddMtarBlockPackagePathsAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x140a02290ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4: return 0x140a020a0ull;
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3: return 0x1463bb3c0ull;
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3: return 0x140a013c0ull;
        default:                                          return 0;
        }
    }

    static void __fastcall hkAddMtarBlockPackagePaths(void* arr, std::uint32_t engineOrRealId)
    {
        g_OrigAddMtarBlockPackagePaths(arr, engineOrRealId);
        const auto equipId =
            static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(engineOrRealId)));
        if (!arr || equipId == 0
            || equipId > static_cast<std::uint32_t>(EquipIdCompression::kWideEquipIdLast))
            return;
        if (TppEquip_GetSubIdForEquipId(static_cast<int>(equipId)) == 0)
            return;
        const std::uint32_t t = GetEquipTypeForEquipId(equipId) & 0x1F;
        if (t < 1 || t > 8)
            return;
        FillCustomMotionEntriesEarly();
        MotionResidencyPlan plan;
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            auto it = g_MotionResidency.find(equipId);
            if (it != g_MotionResidency.end())
                plan = it->second;
        }
        if (plan.donorEq)
        {
            g_OrigAddMtarBlockPackagePaths(arr, plan.donorEq);
            AppendPackPath(arr, plan.donorPack);
        }
        std::uint64_t engineEntry = 0;
        std::string engineRoot;
        {
            void* ml = GetEquipMotionLoaderIface();
            void* mlv = ml ? ReadPtrAtSEH(ml, 0) : nullptr;
            if (mlv)
            {
                std::uint8_t ids[16] = {};
                std::uint32_t eqIn = engineOrRealId;
                const std::uint32_t n =
                    CallMtarIdsSEH(ReadPtrAtSEH(mlv, 0x18), ml, ids, &eqIn);
                std::set<std::string> seenRoots;
                for (std::uint32_t i = 0; i < n && i < 16; ++i)
                {
                    const std::uint64_t pkg =
                        CallPathByIdSEH(ReadPtrAtSEH(mlv, 0x10), ml, ids[i]);
                    if ((pkg >> 56) != 0x52)
                        continue;
                    int cat = -1;
                    std::string root;
                    RecoverFamilyRoot(pkg, cat, root);
                    if (root.empty() || cat < 0 || !seenRoots.insert(root).second)
                        continue;
                    const std::uint64_t fpk = FindFamilyChimeraPack(cat, root);
                    const bool fpkOk = fpk != 0;
                    if (fpkOk)
                        AppendPackPath(arr, fpk);
                    const std::uint64_t defEntry =
                        RegisterFamilyArchives(root, equipId);
                    if (cat == 0 && engineRoot.empty() && defEntry && fpkOk)
                    {
                        engineRoot = root;
                        engineEntry = defEntry;
                    }
                }
            }
        }
        if (engineEntry && !plan.donorEq)
            WriteMotionEntry(equipId, reinterpret_cast<void*>(engineEntry));
        if (!engineEntry && plan.familyPack)
            AppendPackPath(arr, plan.familyPack);
    }

#ifdef _DEBUG
    using BoltBoneFn_t = unsigned long long(__fastcall*)(
        void*, unsigned long long, unsigned long long, unsigned long long);
    static BoltBoneFn_t g_OrigBoltBoneIdx = nullptr;
    static BoltBoneFn_t g_OrigBoltBoneWrite = nullptr;
    static void** g_BoltBoneVtbl = nullptr;
    static std::mutex g_BoltBoneMx;

    struct CtlInfo
    {
        unsigned eq = 0;
        void* iface = nullptr;
    };
    static std::map<void*, CtlInfo> g_CtlToEq;
    static std::map<unsigned long long, unsigned> g_CtlLastHash;

    using ChimSet_t = unsigned long long(__fastcall*)(
        void*, unsigned long long, unsigned long long, unsigned long long,
        unsigned long long, unsigned long long);
    static BoltBoneFn_t g_OrigChimBoneIdx = nullptr;
    static ChimSet_t g_OrigChimSet = nullptr;
    static void** g_ChimVtbl = nullptr;

    struct BoltModelSample
    {
        unsigned long long boneIdx = 0;
        unsigned eq = 0;
        void* writerCtl = nullptr;
        unsigned long long lastWriteTick = 0;
        bool haveRest = false;
        unsigned char mtx[0x40] = {};
        unsigned char rest[0x40] = {};
    };
    static std::map<void*, BoltModelSample> g_BoltModelSamples;

    static bool CopyBoltBytesSEH(void* dst, const void* src, size_t n)
    {
        __try
        {
            memcpy(dst, src, n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool ReadF32x3AtSEH(void* addr, float* out3)
    {
        __try
        {
            memcpy(out3, addr, 12);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static CtlInfo LookupCtlInfo(void* ctl)
    {
        std::lock_guard<std::mutex> lock(g_BoltBoneMx);
        auto it = g_CtlToEq.find(ctl);
        return it == g_CtlToEq.end() ? CtlInfo{} : it->second;
    }

    static void* BoltPoseSlotAddr(void* ctl, unsigned long long slot, unsigned long long boneIdx)
    {
        void* models = ReadPtrAtSEH(ctl, 0x58);
        if (!models)
            return nullptr;
        void* model = ReadPtrAtSEH(models, static_cast<size_t>(slot) * 8);
        if (!model)
            return nullptr;
        void* pose = ReadPtrAtSEH(model, 0xE0);
        if (!pose)
            return nullptr;
        return static_cast<std::uint8_t*>(pose) + boneIdx * 0x40;
    }

    static unsigned long long __fastcall hkBoltBoneIdx(
        void* ctl, unsigned long long slot, unsigned long long c, unsigned long long d)
    {
        const unsigned long long r = g_OrigBoltBoneIdx(ctl, slot, c, d);
        {
            std::lock_guard<std::mutex> lock(g_BoltBoneMx);
            g_CtlLastHash[reinterpret_cast<unsigned long long>(ctl) + slot] =
                static_cast<unsigned>(c & 0xffffffffull);
        }
        return r;
    }

    static unsigned long long __fastcall hkBoltBoneWrite(
        void* ctl, unsigned long long slot, unsigned long long boneIdx, unsigned long long mtx)
    {
        const CtlInfo info = LookupCtlInfo(ctl);
        const unsigned long long r = g_OrigBoltBoneWrite(ctl, slot, boneIdx, mtx);
        void* addr = BoltPoseSlotAddr(ctl, slot, boneIdx);
        float now[3] = { 0, 0, 0 };
        const bool haveNow = addr && ReadF32x3AtSEH(static_cast<std::uint8_t*>(addr) + 0x30, now);
        if (haveNow && info.eq && (info.eq - 0x367u) >= 6u &&
            TppEquip_GetSubIdForEquipId(static_cast<int>(info.eq)) != 0)
        {
            void* models = ReadPtrAtSEH(ctl, 0x58);
            void* model = models ? ReadPtrAtSEH(models, static_cast<size_t>(slot) * 8) : nullptr;
            if (model)
            {
                unsigned char m[0x40];
                if (CopyBoltBytesSEH(m, reinterpret_cast<const void*>(mtx), 0x40))
                {
                    std::lock_guard<std::mutex> lock(g_BoltBoneMx);
                    BoltModelSample& s = g_BoltModelSamples[model];
                    s.boneIdx = boneIdx;
                    s.eq = info.eq;
                    s.writerCtl = ctl;
                    s.lastWriteTick = GetTickCount64();
                    memcpy(s.mtx, m, 0x40);
                }
            }
        }
        if (info.iface && info.eq && (info.eq - 0x367u) >= 6u &&
            g_OrigChimBoneIdx && g_OrigChimSet &&
            TppEquip_GetSubIdForEquipId(static_cast<int>(info.eq)) != 0)
        {
            unsigned hash = 0;
            {
                std::lock_guard<std::mutex> lock(g_BoltBoneMx);
                auto it = g_CtlLastHash.find(
                    reinterpret_cast<unsigned long long>(ctl) + slot);
                if (it != g_CtlLastHash.end())
                    hash = it->second;
            }
            if (hash)
            {
                for (unsigned long long ch = 0; ch < 6; ++ch)
                {
                    const int cidx = static_cast<int>(
                        g_OrigChimBoneIdx(info.iface, ch, 0, hash));
                    if (cidx < 0)
                        continue;
                    g_OrigChimSet(info.iface, ch, 0,
                                  static_cast<unsigned long long>(cidx), mtx, 0);
                }
            }
        }
        return r;
    }

    static unsigned long long __fastcall hkChimBoneIdx(
        void* iface, unsigned long long ch, unsigned long long part, unsigned long long hash)
    {
        return g_OrigChimBoneIdx(iface, ch, part, hash);
    }

    static unsigned long long __fastcall hkChimSet(
        void* iface, unsigned long long ch, unsigned long long part,
        unsigned long long boneIdx, unsigned long long mtx, unsigned long long spare)
    {
        return g_OrigChimSet(iface, ch, part, boneIdx, mtx, spare);
    }

    static void TryArmChimeraHooks(void* obj)
    {
        static std::mutex s_armMx;
        std::lock_guard<std::mutex> armLock(s_armMx);
        if (g_ChimVtbl)
            return;
        void* iface = ReadPtrAtSEH(obj, 0x120);
        if (!iface)
            return;
        void** vtbl = static_cast<void**>(ReadPtrAtSEH(iface, 0));
        if (!vtbl)
            return;
        void* idxFn = ReadPtrAtSEH(vtbl, 0x170);
        void* setFn = ReadPtrAtSEH(vtbl, 0x188);
        if (!idxFn || !setFn)
            return;
        DWORD oldProt = 0;
        if (!VirtualProtect(&vtbl[0x170 / 8], sizeof(void*) * 4, PAGE_READWRITE, &oldProt))
            return;
        g_OrigChimBoneIdx = reinterpret_cast<BoltBoneFn_t>(idxFn);
        g_OrigChimSet = reinterpret_cast<ChimSet_t>(setFn);
        vtbl[0x170 / 8] = reinterpret_cast<void*>(&hkChimBoneIdx);
        vtbl[0x188 / 8] = reinterpret_cast<void*>(&hkChimSet);
        VirtualProtect(&vtbl[0x170 / 8], sizeof(void*) * 4, oldProt, &oldProt);
        g_ChimVtbl = vtbl;
    }

    static BoltBoneFn_t g_OrigPutMotion = nullptr;
    static BoltBoneFn_t g_OrigPutMotionAll = nullptr;

    static bool SlotHasAnimControl(void* ctl, unsigned long long slot)
    {
        void* arr = ReadPtrAtSEH(ctl, 0x60);
        return arr && ReadPtrAtSEH(arr, static_cast<size_t>(slot) * 8);
    }

    static void ReapplyBoltToModel(void* ctl, unsigned long long slot)
    {
        if (SlotHasAnimControl(ctl, slot))
            return;
        void* models = ReadPtrAtSEH(ctl, 0x58);
        if (!models)
            return;
        void* model = ReadPtrAtSEH(models, static_cast<size_t>(slot) * 8);
        if (!model)
            return;
        BoltModelSample s;
        {
            std::lock_guard<std::mutex> lock(g_BoltBoneMx);
            auto it = g_BoltModelSamples.find(model);
            if (it == g_BoltModelSamples.end())
                return;
            if (GetTickCount64() - it->second.lastWriteTick > 600)
            {
                g_BoltModelSamples.erase(it);
                return;
            }
            s = it->second;
        }
        void* pose = ReadPtrAtSEH(model, 0xE0);
        if (!pose)
            return;
        void* dst = static_cast<std::uint8_t*>(pose) + s.boneIdx * 0x40;
        unsigned char restNow[0x40];
        const bool gotRest = CopyBoltBytesSEH(restNow, dst, 0x40);
        if (!CopyBoltBytesSEH(dst, s.mtx, 0x40))
            return;
        if (gotRest)
        {
            std::lock_guard<std::mutex> lock(g_BoltBoneMx);
            auto it = g_BoltModelSamples.find(model);
            if (it != g_BoltModelSamples.end())
            {
                memcpy(it->second.rest, restNow, 0x40);
                it->second.haveRest = true;
            }
        }
    }

    static void RestoreBoltBaseline(void* ctl)
    {
        for (unsigned long long slot = 0; slot < 2; ++slot)
        {
            if (SlotHasAnimControl(ctl, slot))
                continue;
            void* models = ReadPtrAtSEH(ctl, 0x58);
            if (!models)
                return;
            void* model = ReadPtrAtSEH(models, static_cast<size_t>(slot) * 8);
            if (!model)
                continue;
            BoltModelSample s;
            {
                std::lock_guard<std::mutex> lock(g_BoltBoneMx);
                auto it = g_BoltModelSamples.find(model);
                if (it == g_BoltModelSamples.end() || !it->second.haveRest ||
                    it->second.writerCtl != ctl)
                    continue;
                s = it->second;
            }
            void* pose = ReadPtrAtSEH(model, 0xE0);
            if (!pose)
                continue;
            CopyBoltBytesSEH(static_cast<std::uint8_t*>(pose) + s.boneIdx * 0x40,
                             s.rest, 0x40);
        }
    }

    static unsigned long long __fastcall hkPutMotion(
        void* ctl, unsigned long long slot, unsigned long long c, unsigned long long d)
    {
        const unsigned long long r = g_OrigPutMotion(ctl, slot, c, d);
        ReapplyBoltToModel(ctl, slot);
        return r;
    }

    static unsigned long long __fastcall hkPutMotionAll(
        void* ctl, unsigned long long b, unsigned long long c, unsigned long long d)
    {
        const unsigned long long r = g_OrigPutMotionAll(ctl, b, c, d);
        const int count = ReadByteAtSEH(ctl, 0x50);
        for (int s = 0; s < count && s < 8; ++s)
            ReapplyBoltToModel(ctl, static_cast<unsigned long long>(s));
        return r;
    }

    static void TryArmBoltBoneHooks(void* obj)
    {
        static std::mutex s_armMx;
        std::lock_guard<std::mutex> armLock(s_armMx);
        if (g_BoltBoneVtbl)
            return;
        void* ctl = ReadPtrAtSEH(obj, 0x30);
        if (!ctl)
            return;
        void** vtbl = static_cast<void**>(ReadPtrAtSEH(ctl, 0));
        if (!vtbl)
            return;
        void* idxFn = ReadPtrAtSEH(vtbl, 0x140);
        void* writeFn = ReadPtrAtSEH(vtbl, 0x168);
        void* putAllFn = ReadPtrAtSEH(vtbl, 0x200);
        void* putFn = ReadPtrAtSEH(vtbl, 0x210);
        if (!idxFn || !writeFn || !putAllFn || !putFn)
            return;
        DWORD oldProt = 0;
        if (!VirtualProtect(&vtbl[0x140 / 8], sizeof(void*) * 6, PAGE_READWRITE, &oldProt))
            return;
        g_OrigBoltBoneIdx = reinterpret_cast<BoltBoneFn_t>(idxFn);
        g_OrigBoltBoneWrite = reinterpret_cast<BoltBoneFn_t>(writeFn);
        vtbl[0x140 / 8] = reinterpret_cast<void*>(&hkBoltBoneIdx);
        vtbl[0x168 / 8] = reinterpret_cast<void*>(&hkBoltBoneWrite);
        VirtualProtect(&vtbl[0x140 / 8], sizeof(void*) * 6, oldProt, &oldProt);
        if (VirtualProtect(&vtbl[0x200 / 8], sizeof(void*) * 3, PAGE_READWRITE, &oldProt))
        {
            g_OrigPutMotionAll = reinterpret_cast<BoltBoneFn_t>(putAllFn);
            g_OrigPutMotion = reinterpret_cast<BoltBoneFn_t>(putFn);
            vtbl[0x200 / 8] = reinterpret_cast<void*>(&hkPutMotionAll);
            vtbl[0x210 / 8] = reinterpret_cast<void*>(&hkPutMotion);
            VirtualProtect(&vtbl[0x200 / 8], sizeof(void*) * 3, oldProt, &oldProt);
        }
        g_BoltBoneVtbl = vtbl;
    }
#endif

    static unsigned long long __fastcall hkPostPutMotion(
        void* self, void* b, void* c, void* d)
    {
#ifdef _DEBUG
        {
            unsigned eq = 0;
            bool tracked = false;
            void* base = ReadPtrAtSEH(self, 0xe8);
            if (base)
            {
                const unsigned w4 = ReadRecWord(base, 0, 4);
                const unsigned hi = w4 & 0xF800u;
                if (w4 != 0xFFFFFFFFu && (hi == 0x1000u || hi == 0x1800u))
                {
                    eq = w4 & 0x7FFu;
                    tracked = true;
                }
            }
            void* ctl = ReadPtrAtSEH(self, 0x30);
            if (ctl)
            {
                RestoreBoltBaseline(ctl);
                if (tracked)
                {
                    TryArmBoltBoneHooks(self);
                    TryArmChimeraHooks(self);
                    CtlInfo info;
                    info.eq = eq;
                    info.iface = ReadPtrAtSEH(self, 0x120);
                    std::lock_guard<std::mutex> lock(g_BoltBoneMx);
                    g_CtlToEq[ctl] = info;
                }
            }
        }
#endif
        const unsigned long long r = g_OrigPostPutMotion(self, b, c, d);
        FeedCylinderVisibilitySEH(self);
        DriveCylinderRounds();
        DriveCylinderRotation();
        return r;
    }


    using PartsCtlCreateSlot_t =
        void(__fastcall*)(void*, std::uint32_t, void*, std::uint8_t);
    static PartsCtlCreateSlot_t g_OrigPartsCtlCreateSlot = nullptr;
    static void* g_PartsCtlCreateSlotAddr = nullptr;

    static constexpr std::uint8_t kPartsCtlCreateSlotPrologue[24] =
        { 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
          0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0xA0 };

    static void* PartsCtlCreateSlotTarget()
    {
        auto* vtbl = static_cast<void**>(
            reinterpret_cast<void*>(ResolveGameAddress(gAddr.SimplePartsControllerImpl_Vtable)));
        if (!vtbl)
            return nullptr;
        void* target = ReadPtrAtSEH(vtbl, 0x10);
        const auto addr = reinterpret_cast<uintptr_t>(target);
        if (!PartsSlotTargetInImage(addr)
            || !MotionBytesMatchSEH(addr, kPartsCtlCreateSlotPrologue,
                                    sizeof(kPartsCtlCreateSlotPrologue)))
        {
            LogError("EquipParam: parts-controller create slot %p mismatch on %s, bad part models can crash",
                target, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        return target;
    }

    struct RemapCreateFns
    {
        void* size;
        void* place;
        void* heap;
        void* dtorPool;
        void* dtorHeap;
        void* aux;
    };

    static bool ResolveRemapCreateFns(RemapCreateFns& f)
    {
        f.size = ResolveGameAddress(gAddr.Animx_GetControlSize);
        f.place = ResolveGameAddress(gAddr.Animx_SimpleControlCtorPool);
        f.heap = ResolveGameAddress(gAddr.Animx_SimpleControlCtorHeap);
        f.dtorPool = ResolveGameAddress(gAddr.Animx_SimpleControlDtorPool);
        f.dtorHeap = ResolveGameAddress(gAddr.Animx_SimpleControlDtorHeap);
        f.aux = ResolveGameAddress(gAddr.Animx_SimpleControlAux);
        return f.size && f.place && f.heap && f.dtorPool && f.dtorHeap && f.aux;
    }

    static int SnapshotControlsSEH(void* ctl, void** out, int cap)
    {
        __try
        {
            std::uint8_t* c = static_cast<std::uint8_t*>(ctl);
            int n = *reinterpret_cast<std::int32_t*>(c + 0x8);
            void** arr = *reinterpret_cast<void***>(c + 0x60);
            if (!arr || n <= 0)
                return 0;
            if (n > cap)
                n = cap;
            for (int i = 0; i < n; ++i)
                out[i] = arr[i];
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int FindChangedControlSEH(void* ctl, void** before, int n)
    {
        __try
        {
            void** arr = *reinterpret_cast<void***>(
                static_cast<std::uint8_t*>(ctl) + 0x60);
            if (!arr)
                return -1;
            for (int i = 0; i < n; ++i)
                if (arr[i] && arr[i] != before[i])
                    return i;
            return -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    struct RemapCreateParams
    {
        std::uint32_t flags;
        std::uint32_t count;
        void* mtar;
        void* model;
        void* helpBone;
    };

    static int RebuildControlWithRemapSEH(void* ctlIn, int idx, void* infoIn,
                                          const RemapCreateFns& f,
                                          unsigned* outSize, unsigned* outStride)
    {
        __try
        {
            std::uint8_t* ctl = static_cast<std::uint8_t*>(ctlIn);
            std::uint8_t* info = static_cast<std::uint8_t*>(infoIn);
            void** ctlArr = *reinterpret_cast<void***>(ctl + 0x60);
            void** mdlArr = *reinterpret_cast<void***>(ctl + 0x58);
            if (!ctlArr || !mdlArr)
                return -1;
            std::uint8_t* old = static_cast<std::uint8_t*>(ctlArr[idx]);
            void* model = mdlArr[idx];
            if (!old || !model)
                return -2;
            std::uint8_t* desc = *reinterpret_cast<std::uint8_t**>(info + 0x10);
            if (!desc)
                return -3;
            void* mtar = *reinterpret_cast<void**>(desc);
            if (!mtar)
                return -4;
            std::uint8_t* inner = old + *reinterpret_cast<std::int32_t*>(old + 0xc);
            if (*reinterpret_cast<std::int32_t*>(inner + 0x30) != 0)
                return 1;
            const std::uint8_t descFlags = desc[0xc];
            RemapCreateParams p{};
            p.flags = 0x200u
                      | (*reinterpret_cast<void**>(ctl + 0x78) ? 8u : 0u)
                      | ((descFlags & 2) ? 1u : 0u)
                      | 4u;
            p.count = *reinterpret_cast<std::uint32_t*>(desc + 8) + 1;
            p.mtar = mtar;
            p.model = model;
            p.helpBone = nullptr;
            if (descFlags & 1)
            {
                void* hb = *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(model) + 0xe0);
                if (hb)
                    p.helpBone = hb;
            }
            const std::uint64_t size =
                reinterpret_cast<std::uint64_t(__fastcall*)(void*)>(f.size)(&p);
            std::uint8_t* pool = *reinterpret_cast<std::uint8_t**>(ctl + 0x68);
            const std::uint32_t stride = *reinterpret_cast<std::uint32_t*>(ctl + 0x70);
            if (outSize)
                *outSize = static_cast<unsigned>(size);
            if (outStride)
                *outStride = pool ? stride : 0u;
            if (pool && size > stride)
                return -5;
            if (pool)
            {

                reinterpret_cast<void(__fastcall*)(void*)>(f.dtorPool)(old);
                void* mem = pool + static_cast<std::size_t>(stride)
                                       * static_cast<unsigned>(idx);
                reinterpret_cast<void*(__fastcall*)(void*, void*, std::uint64_t)>(
                    f.place)(mem, &p, size);
                ctlArr[idx] = mem;
                if (*reinterpret_cast<void**>(ctl + 0xd8))
                {
                    std::uint8_t* aux = *reinterpret_cast<std::uint8_t**>(ctl + 0xe8);
                    if (aux)
                        reinterpret_cast<void(__fastcall*)(void*, void*)>(f.aux)(
                            mem, aux + static_cast<std::size_t>(idx) * 0x50);
                }
                return 0;
            }

            bool remapped = true;
            void* fresh = reinterpret_cast<void*(__fastcall*)(void*)>(f.heap)(&p);
            if (!fresh)
            {
                p.flags &= ~4u;
                remapped = false;
                fresh = reinterpret_cast<void*(__fastcall*)(void*)>(f.heap)(&p);
            }
            if (!fresh)
                return -6;
            reinterpret_cast<void(__fastcall*)(void*)>(f.dtorHeap)(old);
            ctlArr[idx] = fresh;
            if (*reinterpret_cast<void**>(ctl + 0xd8))
            {
                std::uint8_t* aux = *reinterpret_cast<std::uint8_t**>(ctl + 0xe8);
                if (aux)
                    reinterpret_cast<void(__fastcall*)(void*, void*)>(f.aux)(
                        fresh, aux + static_cast<std::size_t>(idx) * 0x50);
            }
            return remapped ? 0 : -7;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -100;
        }
    }

    static void* DescMtarSEH(void* info)
    {
        __try
        {
            std::uint8_t* desc = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(info) + 0x10);
            return desc ? *reinterpret_cast<void**>(desc) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static int MtarMaxTracksSEH(void* mtar)
    {
        __try
        {
            std::uint8_t* dir = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(mtar) + 0x98);
            return dir ? *reinterpret_cast<std::uint16_t*>(dir + 8) : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static int ModelBoneCapSEH(void* ctl, int idx)
    {
        __try
        {
            void** mdlArr = *reinterpret_cast<void***>(
                static_cast<std::uint8_t*>(ctl) + 0x58);
            std::uint8_t* model =
                mdlArr ? static_cast<std::uint8_t*>(mdlArr[idx]) : nullptr;
            return model ? *reinterpret_cast<std::int16_t*>(model + 0xa0 + 0x58) : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static bool CallOrigCreateSlotSEH(void* ctl, std::uint32_t slot, void* info, std::uint8_t d)
    {
        __try
        {
            g_OrigPartsCtlCreateSlot(ctl, slot, info, d);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void __fastcall hkPartsCtlCreateSlot(void* ctl, std::uint32_t slot,
                                                void* info, std::uint8_t d)
    {
        void* before[32] = {};
        int n = 0;
        if (ctl && info)
            n = SnapshotControlsSEH(ctl, before, 32);
        if (!CallOrigCreateSlotSEH(ctl, slot, info, d))
        {
            static std::atomic<int> budget{ 8 };
            if (budget.fetch_sub(1) > 0)
                LogWarn("WeaponKey: parts slot %u create faulted (missing or bad model asset), slot left empty",
                    slot);
            return;
        }
        if (n <= 0)
            return;
        RemapCreateFns f{};
        if (!ResolveRemapCreateFns(f))
            return;
        const int idx = FindChangedControlSEH(ctl, before, n);
        void* mtar = (idx >= 0) ? DescMtarSEH(info) : nullptr;
        NoteLiveMtar(mtar);
        const int need = mtar ? MtarMaxTracksSEH(mtar) : -1;
        const int cap = (idx >= 0) ? ModelBoneCapSEH(ctl, idx) : -1;
        if (idx < 0 || !mtar)
            return;
        if (need <= 0 || cap < 0 || cap >= need - 1)
            return;
        std::uint32_t eq = 0;
        {
            std::uint64_t entries[64];
            std::uint32_t eqIds[64];
            int cnt = 0;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                for (const auto& kv : g_RemapEntryEq)
                {
                    if (cnt >= 64)
                        break;
                    entries[cnt] = kv.first;
                    eqIds[cnt] = kv.second;
                    ++cnt;
                }
            }
            for (int i = 0; i < cnt; ++i)
                if (ResolveMtarForEntrySEH(entries[i]) == mtar)
                {
                    eq = eqIds[i];
                    break;
                }
        }
        if (idx != static_cast<int>(slot))
        {
            static std::atomic<int> skipBudget{ 8 };
            if (skipBudget.fetch_sub(1) > 0)
                LogWarn("WeaponKey: equipId=%u slot %u bone remap skipped (control at index %d)", eq, slot, idx);
            return;
        }
        unsigned size = 0, stride = 0;
        const int rc = RebuildControlWithRemapSEH(ctl, idx, info, f, &size, &stride);
        static std::mutex mx;
        static std::set<std::uint64_t> logged;
        bool isNew;
        {
            std::lock_guard<std::mutex> lock(mx);
            isNew = logged.size() < 128 &&
                    logged.insert((static_cast<std::uint64_t>(eq) << 8)
                                  | static_cast<std::uint8_t>(rc)).second;
        }
        if (!isNew)
            return;
        if (rc == -5)
            LogWarn("WeaponKey: equipId=%u slot %u bone remap needs %u bytes, pool slot is %u; original kept",
                eq, slot, size, stride);
        else if (rc != 0 && rc != 1)
            LogWarn("WeaponKey: equipId=%u slot %u bone remap failed rc=%d, original %s",
                eq, slot, rc, (rc == -7) ? "recreated without remap" : "left as-is");
    }

    static unsigned long long __fastcall hkRealizedEquipRealize(
        void* self, unsigned int a, unsigned int b)
    {
        ArmMotionEntryShadow();
        FillCustomMotionEntries(self);
        const unsigned long long r = g_OrigRealizedEquipRealize(self, a, b);
        CaptureEquipDataProvider(self);
        if (r == 0)
        {
            unsigned failBase = 0;
            for (int i = 0; i < 4; ++i)
                failBase |= static_cast<unsigned>(
                    ReadByteAtSEH(self, 0x34 + static_cast<size_t>(i)) & 0xFF) << (8 * i);
            const unsigned failSlot = a - failBase;
            void* failRec = ReadPtrAtSEH(self, 0xc8);
            unsigned failEq = 0;
            if (failRec && failSlot < 2)
                failEq = ((ReadByteAtSEH(failRec, failSlot * 0x14 + 4) & 0xFF)
                          | ((ReadByteAtSEH(failRec, failSlot * 0x14 + 5) & 0xFF) << 8))
                         & 0x7FF;
            static std::atomic<int> failBudget{ 64 };
            if (failEq != 0 && failBudget.fetch_sub(1) > 0)
                LogWarn("WeaponKey: Realize failed for equipId=%u slot %u, no weapon model", failEq, failSlot);
        }
        return r;
    }

    static void ApplyKeyTypeSwaps(void* self, int slot, KeyTypeSwap& st)
    {
        st.n = 0;
        if (!self || !g_HasSwaps.load(std::memory_order_relaxed))
            return;
        std::uint32_t fromIds[8], donors[8];
        int m = 0;
        {
            std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
            for (const auto& kv : g_FamilyFrom)
            {
                if (m >= 8)
                    break;
                fromIds[m] = kv.first;
                donors[m] = kv.second;
                ++m;
            }
        }
        if (m == 0)
            return;
        std::uint32_t toTypes[8];
        for (int i = 0; i < m; ++i)
            toTypes[i] = GetEquipTypeForEquipId(donors[i]) & 0x1F;
        SwapKeyWordsSEH(self, slot, fromIds, toTypes, m, &st);
    }

    static std::uint32_t PartCodeCommon(int slot, void* self, std::uint32_t partId)
    {
        if (g_InSetupWeaponInfo && g_TlsVanillaEquip != 0)
        {
            FamilyGb van;
            if (!GetGbForEquipId(g_TlsVanillaEquip, van))
            {
                static unsigned long long lastMs = 0;
                const unsigned long long now = GetTickCount64();
                if (now - lastMs > 2000)
                {
                    lastMs = now;
                    LogWarn("WeaponKey: donor equipId=%u has no gunBasic row, part swap skipped (slot %d)",
                        g_TlsVanillaEquip, slot);
                }
            }
            else
            {
                std::uint32_t use = partId;
                if (g_PartCodeExact[slot])
                {
                    use = van.gb[kPartGbByte[slot]];
                    if ((slot == 3 || slot == 4) && use != 0)
                    {
                        FamilyGb cus;
                        if (GetGbForEquipId(g_TlsCustomEquip, cus)
                            && cus.gb[kPartGbByte[slot]] == 0)
                        {
                            use = partId;
                        }
                    }
                }
                else
                {
                    FamilyGb cus;
                    if (partId != 0 && GetGbForEquipId(g_TlsCustomEquip, cus))
                        for (int k = 0; k < 5; ++k)
                            if (partId == cus.gb[kPartGbByte[k]])
                            {
                                use = van.gb[kPartGbByte[k]];
                                break;
                            }
                }
                const std::uint32_t code = g_OrigPartCode[slot](self, use);
                if (slot == 0 && (code == 0 || code == 0x2f))
                {
                    static unsigned long long lastCodeMs = 0;
                    const unsigned long long nowMs = GetTickCount64();
                    if (nowMs - lastCodeMs > 2000)
                    {
                        lastCodeMs = nowMs;
                        LogWarn("WeaponKey: equipId=%u receiver part code 0x%X (rc %u->%u), weapon invalidated; pick another donor",
                            g_TlsCustomEquip, code, partId, use);
                    }
                }
                return code;
            }
        }
        return g_OrigPartCode[slot](self, partId);
    }

    static std::uint32_t __fastcall hkPartCode0(void* s, std::uint32_t p) { return PartCodeCommon(0, s, p); }
    static std::uint32_t __fastcall hkPartCode1(void* s, std::uint32_t p) { return PartCodeCommon(1, s, p); }
    static std::uint32_t __fastcall hkPartCode2(void* s, std::uint32_t p) { return PartCodeCommon(2, s, p); }
    static std::uint32_t __fastcall hkPartCode3(void* s, std::uint32_t p) { return PartCodeCommon(3, s, p); }
    static std::uint32_t __fastcall hkPartCode4(void* s, std::uint32_t p) { return PartCodeCommon(4, s, p); }
    static void* const kPartCodeDetours[5] = {
        reinterpret_cast<void*>(&hkPartCode0), reinterpret_cast<void*>(&hkPartCode1),
        reinterpret_cast<void*>(&hkPartCode2), reinterpret_cast<void*>(&hkPartCode3),
        reinterpret_cast<void*>(&hkPartCode4) };

    static std::uint32_t __fastcall hkResolver20(void* self, std::uint32_t key)
    {
        const std::uint32_t equipId = g_OrigResolver20(self, key);
        if (g_InSetupWeaponInfo)
        {
            g_TlsSetupEquip = equipId;
            const std::uint32_t vanilla = MapEquipId(equipId);
            if (vanilla != equipId)
            {
                g_TlsCustomEquip = equipId;
                g_TlsVanillaEquip = vanilla;
            }
        }
        return equipId;
    }

    static char __fastcall hkResolver40(void* self, std::uint32_t key)
    {
        return g_OrigResolver40(self, key);
    }

    static void* CallTemplateFinderSEH(void* self, std::uint32_t vanillaEquipId)
    {
        __try
        {
            return g_Finder148 ? g_Finder148(self, vanillaEquipId) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static int FixDescTemplateIndexSEH(void* self, std::uint32_t key, void* own)
    {
        __try
        {
            std::uint8_t* a = static_cast<std::uint8_t*>(self);
            std::uint8_t* pool = *reinterpret_cast<std::uint8_t**>(a + 0x1d8);
            if (!pool)
                return -1;
            std::uint8_t* op = static_cast<std::uint8_t*>(own);
            if (op < pool)
                return -1;
            const size_t diff = static_cast<size_t>(op - pool);
            if (diff % 0x90 != 0)
                return -1;
            const size_t idx = diff / 0x90;
            if (idx >= 48)
                return -1;
            std::uint8_t* t = *reinterpret_cast<std::uint8_t**>(a + 0x120);
            if (!t)
                return -1;
            std::uint8_t* table = *reinterpret_cast<std::uint8_t**>(t + 0x28);
            if (!table)
                return -1;
            std::uint8_t* d = table + static_cast<size_t>(key >> 16) * 0xe;
            if (d[8] == static_cast<std::uint8_t>(idx))
                return 0;
            d[8] = static_cast<std::uint8_t>(idx);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    static void* __fastcall hkResolver150(void* self, std::uint32_t key)
    {
        if (g_InSetupWeaponInfo && g_HasSwaps.load(std::memory_order_relaxed))
        {
            void* own = g_OrigResolver150(self, key);
            if (g_TlsVanillaEquip == 0)
            {
                if (own && g_OrigResolver20)
                {
                    const std::uint32_t equipId = g_OrigResolver20(self, key);
                    if (equipId != 0)
                    {
                        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                        g_TemplateByEquipId[equipId] = own;
                    }
                }
                return own;
            }
            const std::uint32_t vanilla = g_TlsVanillaEquip;
            if (own)
            {
                std::uint8_t cur = 0;
                std::uint8_t* op = static_cast<std::uint8_t*>(own);
                std::uint16_t cur16 = 0;
                ReadU16SEH(op + 0x78, cur16);
                cur = static_cast<std::uint8_t>(cur16 & 0xFF);
                const std::uint32_t vanType = GetEquipTypeForEquipId(vanilla) & 0x1f;
                bool patched = false;
                if (vanType >= 1 && vanType <= 8 && (cur & 0x1f) != vanType)
                    patched = WriteU8SEH(op + 0x78,
                        static_cast<std::uint8_t>((cur & ~0x1f) | vanType)) == 1;
                bool isNew;
                {
                    std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                    isNew = g_FamilyLogged.insert(g_TlsCustomEquip).second;
                }
                const bool failed = !patched && (cur & 0x1f) != vanType;
                if (isNew && failed)
                    LogWarn("WeaponKey: equipId=%u template family patch failed (eqpType %u -> %u), may handle wrong",
                        g_TlsCustomEquip, static_cast<unsigned>(cur & 0x1f), vanType);
                FixDescTemplateIndexSEH(self, key, own);
            }
            return own;
        }
        return g_OrigResolver150(self, key);
    }

    static int ReadU32AtSEH(const void* base, size_t off, std::uint32_t& out)
    {
        __try
        {
            out = *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(base) + off);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void* ReadPtrAtSEH(void* base, size_t off)
    {
        __try
        {
            return *reinterpret_cast<void**>(
                reinterpret_cast<std::uint8_t*>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void* FollowThunkSeh(void* at)
    {
        auto* p = static_cast<std::uint8_t*>(at);
        if (!p)
            return nullptr;
        __try
        {
            for (int hop = 0; hop < 4; ++hop)
            {
                if (p[0] != 0xE9)
                    return p;
                p = p + 5 + *reinterpret_cast<const std::int32_t*>(p + 1);
            }
            return p;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return at;
        }
    }

    static void* VtblSlotSEH(void* obj, size_t byteOff)
    {
        __try
        {
            void** vtable = *reinterpret_cast<void***>(obj);
            if (!vtable)
                return nullptr;
            return vtable[byteOff / sizeof(void*)];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void* ReadVtableSlotSEH(void* self, size_t byteOff)
    {
        void* resolver = ReadPtrAtSEH(self, 0x60);
        return resolver ? VtblSlotSEH(resolver, byteOff) : nullptr;
    }

    static void EnsureResolverMethodHook(void* self)
    {
        bool expected = false;
        if (!g_ResolverHookTried.compare_exchange_strong(expected, true))
            return;

        void* m20  = ReadVtableSlotSEH(self, 0x20);
        void* m40  = ReadVtableSlotSEH(self, 0x40);
        void* m150 = ReadVtableSlotSEH(self, 0x150);
        g_Finder148 = reinterpret_cast<Resolver150_t>(ReadVtableSlotSEH(self, 0x148));
        if (!m20)
        {
            LogError("WeaponKey: family resolver methods not found, familyFrom off");
            return;
        }
        bool ok20 = CreateAndEnableHook(m20, &hkResolver20,
                                        reinterpret_cast<void**>(&g_OrigResolver20));
        if (ok20) g_Resolver20Addr = m20;
        bool ok40 = false, ok150 = false;
        if (m40)
        {
            ok40 = CreateAndEnableHook(m40, &hkResolver40,
                                       reinterpret_cast<void**>(&g_OrigResolver40));
            if (ok40) g_Resolver40Addr = m40;
        }
        if (m150)
        {
            ok150 = CreateAndEnableHook(m150, &hkResolver150,
                                        reinterpret_cast<void**>(&g_OrigResolver150));
            if (ok150) g_Resolver150Addr = m150;
        }
        if (!ok20 || !ok40 || !ok150)
            LogError("WeaponKey: resolver hooks incomplete (+0x20 %s, +0x40 %s, +0x150 %s), familyFrom partial",
                ok20 ? "ok" : "failed", ok40 ? "ok" : "failed", ok150 ? "ok" : "failed");

        void* rA = ReadPtrAtSEH(self, 0x60);
        void* rB = rA ? ReadPtrAtSEH(rA, 0x18) : nullptr;
        g_GunInfoAccessor = rA;

        if (!rB)
        {
            LogWarn("WeaponKey: part-code resolver not reachable, familyFrom covers template only");
            return;
        }
        g_ResolverB = rB;
        void* rtHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetReceiverType);
        void* ubHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetUnderBarrelType);
        void* brHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetBarrelType);
        void* mgHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetMagazineType);
        void* stHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetStockType);
        for (int k = 0; k < 5; ++k)
        {
            void* mB = VtblSlotSEH(rB, kPartVtblOff[k]);
            int cs = -9, es = -9;
            const bool engineOwned = !mB
                || mB == m20 || mB == m40 || mB == m150
                || mB == rtHookTarget
                || (ubHookTarget && mB == ubHookTarget)
                || (brHookTarget && mB == brHookTarget)
                || (mgHookTarget && mB == mgHookTarget)
                || (stHookTarget && mB == stHookTarget);
            if (!engineOwned)
            {
                int prev = -1;
                for (int j = 0; j < k; ++j)
                    if (g_PartCodeAddr[j] == mB)
                    {
                        prev = j;
                        break;
                    }
                if (prev >= 0)
                {
                    g_OrigPartCode[k] = g_OrigPartCode[prev];
                    g_PartCodeExact[k] = false;
                    g_PartCodeExact[prev] = false;
                }
                else
                {
                    cs = MH_CreateHook(mB, kPartCodeDetours[k],
                                       reinterpret_cast<void**>(&g_OrigPartCode[k]));
                    es = (cs == MH_OK) ? MH_EnableHook(mB) : -9;
                    if (cs == MH_OK && (es == MH_OK || es == MH_ERROR_ENABLED))
                    {
                        g_PartCodeAddr[k] = mB;
                        g_PartCodeExact[k] = true;
                    }
                }
            }
            if (mB && cs != -9 && (cs != MH_OK || (es != MH_OK && es != MH_ERROR_ENABLED)))
                LogError("WeaponKey: part-code slot%d hook failed (cs=%d es=%d), donor part swap incomplete",
                    k, cs, es);
        }
    }

    unsigned int PartCode_DonorPartType(int gbByte, unsigned int partId,
                                        const std::uint8_t* ext)
    {
        if (partId == 0 || !ext || !g_InSetupWeaponInfo || g_TlsVanillaEquip == 0)
            return 0;
        FamilyGb van;
        if (!GetGbForEquipId(g_TlsVanillaEquip, van))
            return 0;
        const unsigned int donor = van.gb[gbByte];
        if (donor == 0 || donor >= kPartTypeExtCount)
            return 0;
        return ext[donor];
    }

    unsigned int PartCode_TapReceiverType(unsigned int receiverId, unsigned int result)
    {
        if (g_InSetupWeaponInfo && g_TlsVanillaEquip != 0)
        {
            FamilyGb van;
            if (GetGbForEquipId(g_TlsVanillaEquip, van) && van.gb[0] != 0)
            {
                FamilyGb cus;
                const bool isCustomRc =
                    GetGbForEquipId(g_TlsCustomEquip, cus) && receiverId == cus.gb[0];
                if (isCustomRc)
                {
                    unsigned int donorType = 0;
                    if (g_RcvTypeExtReady && van.gb[0] < 240)
                        donorType = g_RcvTypeExt[van.gb[0]];
                    const unsigned int finalType = result != 0 ? result : donorType;
                    static std::atomic<std::uint32_t> lastSig{ 0xFFFFFFFF };
                    const std::uint32_t sig =
                        (receiverId << 16) | ((result & 0xFF) << 8) | (finalType & 0xFF);
                    if (lastSig.exchange(sig, std::memory_order_relaxed) != sig && finalType == 0)
                        LogWarn("WeaponKey: receiverId=%u and donor rc=%u have no motion type, weapon invalidated; set motionFrom",
                            receiverId, static_cast<unsigned>(van.gb[0]));
                    return finalType;
                }
                if (receiverId >= 234)
                {
                    static std::atomic<std::uint32_t> lastMiss{ 0xFFFFFFFF };
                    if (lastMiss.exchange(receiverId, std::memory_order_relaxed) != receiverId)
                        LogWarn("WeaponKey: receiverId=%u does not match the mapped weapon's receiver, no donor swap",
                            receiverId);
                }
            }
        }
        return result;
    }

    static void CaptureRemoteMissileContextSEH(void* attackAction)
    {
        __try
        {
            void* playerContext =
                *reinterpret_cast<void**>(static_cast<std::uint8_t*>(attackAction) + 8);
            if (playerContext)
                shell::RemoteMissile_CaptureContext(playerContext);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
        }
    }

    static void LogReceiverMotionWithoutType(int receiverId, bool hasFamily)
    {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        if (!hasFamily && !g_ReceiverWrittenUntyped.count(receiverId))
            return;
        int type = -1;
        if (WidePartState* w = WideStateFor(kVanillaSpace_Receiver, receiverId))
            type = w->motionType;
        else if (g_RcvTypeExtReady && receiverId > 0 && receiverId < kPartTypeExtCount)
            type = g_RcvTypeExt[receiverId];
        if (type != 0 || !g_ReceiverMotionUntypedLogged.insert(receiverId).second)
            return;
        LogWarn("ReceiverMotion: receiverId=%d has no receiverType or motionFrom, weapon invalid on draw",
            receiverId);
    }

    static void __fastcall hkSetupWeaponInfo(void* self, void* work, int slot)
    {
        DeployGuard::OnPlayerLive();

        if (self)
        {
            EnsureResolverMethodHook(self);
            CaptureRemoteMissileContextSEH(self);
        }

        KeyTypeSwap keySwap;
        ApplyKeyTypeSwaps(self, slot, keySwap);
        g_TlsCustomEquip = 0;
        g_TlsVanillaEquip = 0;
        g_TlsSetupEquip = 0;
        g_InSetupWeaponInfo = true;
        g_OrigSetupWeaponInfo(self, work, slot);
        g_InSetupWeaponInfo = false;
        const std::uint32_t setupEquip = g_TlsSetupEquip;
        g_TlsSetupEquip = 0;
        if (setupEquip != 0)
        {
            const int setupReceiver =
                EquipParam_GetReceiverForEquipId(static_cast<int>(setupEquip));
            if (setupReceiver > 0)
                LogReceiverMotionWithoutType(setupReceiver,
                                             ReceiverMotion_HasFamily(setupReceiver));
        }
        RestoreKeyWordsSEH(&keySwap);
        const std::uint32_t mappedThis = g_TlsCustomEquip;
        g_TlsCustomEquip = 0;
        g_TlsVanillaEquip = 0;

        if (work)
        {
            std::uint8_t* wim = static_cast<std::uint8_t*>(work) + 0x1b0;
            std::uint32_t mintKey = 0;
            if (ReadU32AtSEH(wim, 0x90, mintKey) == 1 && mintKey != 0
                && ReadByteAtSEH(wim, 0x7a) == 0)
            {
                const std::uint32_t mintEq = (mintKey & 0xFFFF) >> 5;
                FamilyGb gbRow;
                if (GetGbForEquipId(mintEq, gbRow) && gbRow.gb[0] != 0)
                {
                    const int rc = gbRow.gb[0];
                    int rowVal = rc;
                    if (rc > 233)
                    {
                        const auto it = g_ReceiverMotionDonor.find(rc);
                        rowVal = (it != g_ReceiverMotionDonor.end()) ? it->second : 0;
                    }
                    if (rowVal > 0 && rowVal <= 233)
                        WriteByteAtSEH(wim, 0x7a, static_cast<std::uint8_t>(rowVal));
                }
            }
        }

        if (work && g_HasSwaps.load(std::memory_order_relaxed))
        {
            std::uint8_t* wi = static_cast<std::uint8_t*>(work) + 0x1b0;
            std::uint32_t key = 0;
            const int keyOk = ReadU32AtSEH(wi, 0x90, key);
            if (keyOk == 1 && key == 0)
            {
                std::uint16_t subId = 0;
                ReadU16SEH(wi + 0x70, subId);
                if (subId != 0 || mappedThis != 0)
                {
                    static unsigned long long lastMs = 0;
                    const unsigned long long now = GetTickCount64();
                    if (now - lastMs > 1500)
                    {
                        lastMs = now;
                        LogWarn("WeaponKey: engine invalidated weapon setup (slot=%d subId=%u equipId=%u); check motionFrom/familyFrom",
                            slot, static_cast<unsigned>(subId), mappedThis);
                    }
                }
            }
        }
    }
}

void EquipParam_BindAliasMotion(int aliasId, int wideId)
{
    if (void* entry = ReadMotionEntry(static_cast<std::uint32_t>(wideId)))
        WriteMotionEntry(static_cast<std::uint32_t>(aliasId), entry);
}

void EquipParam_SetWeaponHandling(std::uint32_t equipId, std::uint32_t familyFrom)
{
    if (equipId == 0)
        return;
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        g_GbByEquipId.clear();
        g_FamilyLogged.erase(equipId);
    }
    if (familyFrom == 0 || familyFrom == equipId)
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        g_FamilyFrom.erase(equipId);
        g_HasSwaps.store(!g_FamilyFrom.empty(), std::memory_order_relaxed);
        return;
    }
    FamilyGb van, cus;
    const bool vanOk = GetGbForEquipId(familyFrom, van);
    GetGbForEquipId(equipId, cus);
    const std::uint32_t vanType = GetEquipTypeForEquipId(familyFrom) & 0x1f;
    if (!vanOk || vanType < 1 || vanType > 8)
    {
        LogWarn("SetWeaponHandling equipId=%u: familyFrom=%u is not a vanilla weapon, ignored",
            equipId, familyFrom);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        g_FamilyFrom[equipId] = familyFrom;
        g_HasSwaps.store(true, std::memory_order_relaxed);
    }
    EnsureRcvTypeExt();
}

unsigned int EquipParam_GetWeaponHandlingDonor(unsigned int equipId)
{
    std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
    const auto it = g_FamilyFrom.find(
        static_cast<std::uint32_t>(equip::FromEngine(static_cast<std::int32_t>(equipId))));
    return it == g_FamilyFrom.end() ? 0 : it->second;
}

bool Install_WeaponKeyLog()
{
    const uintptr_t addr = SetupWeaponInfoAddr();
    if (!addr)
    {
        LogError("WeaponKey: SetupWeaponInfo address missing on this build, familyFrom off");
        return true;
    }
    void* target = ResolveGameAddress(addr);
    const bool ok = CreateAndEnableHook(
        target, &hkSetupWeaponInfo,
        reinterpret_cast<void**>(&g_OrigSetupWeaponInfo));
    if (!ok)
        LogError("WeaponKey: SetupWeaponInfo hook failed, familyFrom off");

    const uintptr_t realizeAddr = RealizedEquipRealizeAddr();
    if (realizeAddr)
    {
        g_RealizeAddr = ResolveGameAddress(realizeAddr);
        if (!MotionBytesMatchSEH(reinterpret_cast<std::uintptr_t>(g_RealizeAddr),
                                 kRealizePrologue, sizeof(kRealizePrologue)))
        {
            LogError("WeaponKey: Realize prologue mismatch at %p, custom equipIds get no motion entry",
                g_RealizeAddr);
            g_RealizeAddr = nullptr;
        }
        else
        {
            const bool rOk = CreateAndEnableHook(
                g_RealizeAddr, &hkRealizedEquipRealize,
                reinterpret_cast<void**>(&g_OrigRealizedEquipRealize));
            if (!rOk)
            {
                LogError("WeaponKey: Realize hook failed, custom weapons get no anim control");
                g_RealizeAddr = nullptr;
            }
        }
    }

#ifdef _DEBUG
    const uintptr_t ganiFixAddr = GetAnimFileFromGaniPathAddr();
    if (ganiFixAddr)
    {
        static const std::uint8_t kGaniPrologue[15] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10,
            0x57, 0x48, 0x83, 0xEC, 0x20 };
        static const std::uint8_t kGaniEntryFileLoad[5] = {
            0x4A, 0x8B, 0x4C, 0xC0, 0x18 };
        void* const ganiTarget = ResolveGameAddress(ganiFixAddr);
        const std::uintptr_t ganiBody =
            reinterpret_cast<std::uintptr_t>(FollowThunkSeh(ganiTarget));
        if (!ganiBody
            || !MotionBytesMatchSEH(ganiBody, kGaniPrologue, sizeof(kGaniPrologue))
            || !MotionBytesMatchSEH(ganiBody + 0x2E, kGaniEntryFileLoad,
                                    sizeof(kGaniEntryFileLoad)))
        {
            LogError("WeaponKey: GetAnimFileFromGaniPath mismatch at %p, outfit revert may miss anim controls",
                ganiTarget);
        }
        else
        {
            g_GetAnimFromGaniAddr = ganiTarget;
            const bool ganiOk = AnimControl_GetAnimFileFromGaniPath_Watch(&outfit::NoteAnimControl);   // shares FrameWorkMotionArchive's hook: one MinHook per target
            if (!ganiOk)
            {
                LogError("WeaponKey: GetAnimFileFromGaniPath hook failed, outfit revert may miss anim controls");
                g_GetAnimFromGaniAddr = nullptr;
            }
        }
    }

    const uintptr_t ppmAddr = PostPutMotionAddr();
    if (ppmAddr)
    {
        g_PostPutMotionAddr = ResolveGameAddress(ppmAddr);
        if (!MotionBytesMatchSEH(reinterpret_cast<std::uintptr_t>(g_PostPutMotionAddr),
                                 kPostPutMotionPrologue, sizeof(kPostPutMotionPrologue)))
        {
            LogError("WeaponKey: PostPutMotionExecute prologue mismatch at %p, revolver cylinders static",
                g_PostPutMotionAddr);
            g_PostPutMotionAddr = nullptr;
        }
        else
        {
            const bool pOk = CreateAndEnableHook(
                g_PostPutMotionAddr, &hkPostPutMotion,
                reinterpret_cast<void**>(&g_OrigPostPutMotion));
            if (!pOk)
            {
                LogError("WeaponKey: PostPutMotionExecute hook failed, revolver cylinders static");
                g_PostPutMotionAddr = nullptr;
            }
        }
    }
#endif
    return ok;
}

void Uninstall_WeaponKeyLog()
{
    if (g_PostPutMotionAddr)
    {
        DisableAndRemoveHook(g_PostPutMotionAddr);
        g_PostPutMotionAddr = nullptr;
    }
    g_OrigPostPutMotion = nullptr;

#ifdef _DEBUG
    if (g_BoltBoneVtbl)
    {
        DWORD oldProt = 0;
        if (VirtualProtect(&g_BoltBoneVtbl[0x140 / 8], sizeof(void*) * 6, PAGE_READWRITE, &oldProt))
        {
            g_BoltBoneVtbl[0x140 / 8] = reinterpret_cast<void*>(g_OrigBoltBoneIdx);
            g_BoltBoneVtbl[0x168 / 8] = reinterpret_cast<void*>(g_OrigBoltBoneWrite);
            VirtualProtect(&g_BoltBoneVtbl[0x140 / 8], sizeof(void*) * 6, oldProt, &oldProt);
        }
        if (g_OrigPutMotionAll && g_OrigPutMotion &&
            VirtualProtect(&g_BoltBoneVtbl[0x200 / 8], sizeof(void*) * 3, PAGE_READWRITE, &oldProt))
        {
            g_BoltBoneVtbl[0x200 / 8] = reinterpret_cast<void*>(g_OrigPutMotionAll);
            g_BoltBoneVtbl[0x210 / 8] = reinterpret_cast<void*>(g_OrigPutMotion);
            VirtualProtect(&g_BoltBoneVtbl[0x200 / 8], sizeof(void*) * 3, oldProt, &oldProt);
        }
        g_BoltBoneVtbl = nullptr;
    }
    if (g_ChimVtbl)
    {
        DWORD oldProt = 0;
        if (VirtualProtect(&g_ChimVtbl[0x170 / 8], sizeof(void*) * 4, PAGE_READWRITE, &oldProt))
        {
            g_ChimVtbl[0x170 / 8] = reinterpret_cast<void*>(g_OrigChimBoneIdx);
            g_ChimVtbl[0x188 / 8] = reinterpret_cast<void*>(g_OrigChimSet);
            VirtualProtect(&g_ChimVtbl[0x170 / 8], sizeof(void*) * 4, oldProt, &oldProt);
        }
        g_ChimVtbl = nullptr;
    }
#endif
    if (g_Resolver20Addr)  { DisableAndRemoveHook(g_Resolver20Addr);  g_Resolver20Addr  = nullptr; }
    if (g_Resolver40Addr)  { DisableAndRemoveHook(g_Resolver40Addr);  g_Resolver40Addr  = nullptr; }
    if (g_Resolver150Addr) { DisableAndRemoveHook(g_Resolver150Addr); g_Resolver150Addr = nullptr; }
    g_OrigResolver20 = nullptr;
    g_OrigResolver40 = nullptr;
    g_OrigResolver150 = nullptr;
    g_Finder148 = nullptr;
    for (int k = 0; k < 5; ++k)
    {
        if (g_PartCodeAddr[k])
            DisableAndRemoveHook(g_PartCodeAddr[k]);
        g_PartCodeAddr[k] = nullptr;
        g_OrigPartCode[k] = nullptr;
        g_PartCodeExact[k] = false;
    }
    g_ResolverB = nullptr;

    const uintptr_t addr = SetupWeaponInfoAddr();
    if (addr && g_OrigSetupWeaponInfo)
        DisableAndRemoveHook(ResolveGameAddress(addr));
    g_OrigSetupWeaponInfo = nullptr;

    if (g_GetAnimFromGaniAddr)
    {
        AnimControl_GetAnimFileFromGaniPath_Watch(nullptr);
        g_GetAnimFromGaniAddr = nullptr;
    }

    if (g_RealizeAddr)
    {
        DisableAndRemoveHook(g_RealizeAddr);
        g_RealizeAddr = nullptr;
    }
    g_OrigRealizedEquipRealize = nullptr;
}

namespace
{
    using BulletEffectSpawn_t = void(__fastcall*)(void*, unsigned long long, unsigned int,
                                                  void*, unsigned int);
    static BulletEffectSpawn_t g_OrigBulletEffectSpawn = nullptr;

    enum class BulletEffectRange { InRange, OutOfRange, Unverifiable };

    static BulletEffectRange BulletEffectIndexRangeSEH(void* emitter,
                                                       unsigned int index,
                                                       unsigned long long& outCount)
    {
        outCount = 0;
        __try
        {
            const unsigned char* base =
                *reinterpret_cast<const unsigned char* const*>(
                    reinterpret_cast<const unsigned char*>(emitter) + 0x50);
            if (!base)
                return BulletEffectRange::Unverifiable;
            const unsigned long long count =
                *reinterpret_cast<const unsigned long long*>(base - 8);
            outCount = count;
            if (count == 0 || count > 0x1000)
                return BulletEffectRange::Unverifiable;
            return static_cast<unsigned long long>(index) < count
                ? BulletEffectRange::InRange
                : BulletEffectRange::OutOfRange;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return BulletEffectRange::Unverifiable;
        }
    }

    static void __fastcall hkBulletEffectSpawn(void* emitter, unsigned long long a2,
                                               unsigned int index, void* a4, unsigned int a5)
    {
        if (emitter && index != 0)
        {
            unsigned long long count = 0;
            const BulletEffectRange r =
                BulletEffectIndexRangeSEH(emitter, index, count);

            if (r == BulletEffectRange::OutOfRange)
            {
                static std::atomic<bool> s_Warned{ false };
                if (!s_Warned.exchange(true))
                    LogWarn("EquipParam: bullet effect index %u past the effect array (count=%llu), effect skipped",
                        index, count);
                return;
            }
        }
        g_OrigBulletEffectSpawn(emitter, a2, index, a4, a5);
    }
}

bool Install_BulletEffectGuard()
{
    void* target = ResolveGameAddress(gAddr.BulletEffectController_CreateEffect);
    if (!target)
        return true;
    const bool ok = CreateAndEnableHook(
        target, &hkBulletEffectSpawn,
        reinterpret_cast<void**>(&g_OrigBulletEffectSpawn));
    if (!ok)
        LogError("EquipParam: bullet effect guard hook failed, bad effect indices can crash");
    return ok;
}

void Uninstall_BulletEffectGuard()
{
    if (gAddr.BulletEffectController_CreateEffect && g_OrigBulletEffectSpawn)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.BulletEffectController_CreateEffect));
    g_OrigBulletEffectSpawn = nullptr;
}

bool Install_GunInfoGuard()
{
    void* target = ResolveGameAddress(gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc);
    if (!target)
        return true;
    const bool ok = CreateAndEnableHook(
        target, &hkSetUpGunInfo,
        reinterpret_cast<void**>(&g_OrigSetUpGunInfo));
    if (!ok)
        LogError("EquipParam: SetUpGunInfo hook failed, custom weapon parts off");
    const uintptr_t addMtar = AddMtarBlockPackagePathsAddr();
    if (addMtar)
    {
        g_AddMtarBlockPackagePathsAddr = ResolveGameAddress(addMtar);
        const bool ok2 = CreateAndEnableHook(
            g_AddMtarBlockPackagePathsAddr, &hkAddMtarBlockPackagePaths,
            reinterpret_cast<void**>(&g_OrigAddMtarBlockPackagePaths));
        if (!ok2)
        {
            LogError("EquipParam: AddMtarBlockPackagePaths hook failed, custom weapon motion packs not mounted");
            g_AddMtarBlockPackagePathsAddr = nullptr;
        }
        else
        {
            WarmFamilyRootTable();
        }
    }
    ChimeraMotion_InstallNativeHooks();
    g_ReloadChimeraPartsAddr = ResolveGameAddress(gAddr.Equip_ReloadChimeraPartsInfoTable);
    if (!ReloadChimeraPartsTargetVerified(g_ReloadChimeraPartsAddr))
    {
        LogWarn("EquipParam: ReloadChimeraPartsInfoTable mismatch at %p on %s, using the engine pack table",
            g_ReloadChimeraPartsAddr, GetGameBuildName(gGameBuild));
        g_ReloadChimeraPartsAddr = nullptr;
    }
    else if (!CreateAndEnableHook(
                 g_ReloadChimeraPartsAddr, &hkReloadChimeraPartsInfoTable,
                 reinterpret_cast<void**>(&g_OrigReloadChimeraParts)))
    {
        LogWarn("EquipParam: ReloadChimeraPartsInfoTable hook failed, using the engine pack table");
        g_ReloadChimeraPartsAddr = nullptr;
    }
    MtarRegistry_SetUnloadHook(&ForgetLiveMtar);

    static constexpr std::uint8_t kGetAnimFilePrologue[] = {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x81, 0x98, 0x00, 0x00, 0x00 };
    g_GetAnimFileAddr = FollowThunkSeh(ResolveGameAddress(gAddr.Mtar_GetAnimFile));
    if (g_GetAnimFileAddr
        && !MotionBytesMatchSEH(reinterpret_cast<std::uintptr_t>(g_GetAnimFileAddr),
                                kGetAnimFilePrologue, sizeof(kGetAnimFilePrologue)))
    {
        LogError("EquipParam: MtarFile::GetAnimFile mismatch at %p on %s, outfit motionMtars and clip fallback off",
            g_GetAnimFileAddr, GetGameBuildName(gGameBuild));
        g_GetAnimFileAddr = nullptr;
    }
    else
    {
        const bool okG = CreateAndEnableHook(
            g_GetAnimFileAddr, &hkGetAnimFile,
            reinterpret_cast<void**>(&g_OrigGetAnimFile));
        if (!okG)
        {
            LogError("EquipParam: MtarFile::GetAnimFile hook failed, outfit motionMtars and clip fallback off");
            g_GetAnimFileAddr = nullptr;
        }
    }
    g_HideMagazineAddr = ResolveGameAddress(gAddr.EquipSystemImpl_HideMagazine);
    const bool okH = CreateAndEnableHook(
        g_HideMagazineAddr, &hkHideMagazine,
        reinterpret_cast<void**>(&g_OrigHideMagazine));
    if (!okH)
    {
        LogError("EquipParam: HideMagazine hook failed, under-barrel clips can hide the main magazine");
        g_HideMagazineAddr = nullptr;
    }
    g_PartsCtlCreateSlotAddr = PartsCtlCreateSlotTarget();
    if (g_PartsCtlCreateSlotAddr)
    {
        const bool ok3 = CreateAndEnableHook(
            g_PartsCtlCreateSlotAddr, &hkPartsCtlCreateSlot,
            reinterpret_cast<void**>(&g_OrigPartsCtlCreateSlot));
        if (!ok3)
        {
            LogError("EquipParam: parts-control create hook failed, bad part models can crash");
            g_PartsCtlCreateSlotAddr = nullptr;
        }
    }
    g_SetMotionByPathRealAddr =
        reinterpret_cast<void*>(ResolveGameAddress(gAddr.SimplePartsControllerImpl_SetMotionDataByPath));
    if (g_SetMotionByPathRealAddr)
    {
        const bool okS = CreateAndEnableHook(
            g_SetMotionByPathRealAddr, &hkSetMotionByPathSafe,
            reinterpret_cast<void**>(&g_OrigSetMotionByPathReal));
        if (!okS)
        {
            LogError("EquipParam: SetMotionDataByPath guard hook failed, empty part slots can crash");
            g_SetMotionByPathRealAddr = nullptr;
        }
    }
    InstallPartsSlotModelGuards();
    ArmClipArchiveFallback();
    return ok;
}

void Uninstall_GunInfoGuard()
{
    if (!PartIdWiden_Uninstall())
    {
        LogWarn("EquipParam: GunInfo guard kept, wide part hooks could not be removed safely");
        return;
    }
    MtarRegistry_Uninstall();
    DisarmClipArchiveFallback();
    RemovePartsSlotModelGuards();
    if (g_SetMotionByPathRealAddr)
    {
        DisableAndRemoveHook(g_SetMotionByPathRealAddr);
        g_SetMotionByPathRealAddr = nullptr;
    }
    g_OrigSetMotionByPathReal = nullptr;
    if (g_PartsCtlCreateSlotAddr)
    {
        DisableAndRemoveHook(g_PartsCtlCreateSlotAddr);
        g_PartsCtlCreateSlotAddr = nullptr;
    }
    g_OrigPartsCtlCreateSlot = nullptr;
    if (gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc && g_OrigSetUpGunInfo)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc));
    g_OrigSetUpGunInfo = nullptr;
    if (g_AddMtarBlockPackagePathsAddr)
    {
        DisableAndRemoveHook(g_AddMtarBlockPackagePathsAddr);
        g_AddMtarBlockPackagePathsAddr = nullptr;
    }
    ChimeraMotion_UninstallNativeHooks();
    g_OrigAddMtarBlockPackagePaths = nullptr;
    if (g_ReloadChimeraPartsAddr)
    {
        DisableAndRemoveHook(g_ReloadChimeraPartsAddr);
        g_ReloadChimeraPartsAddr = nullptr;
    }
    g_OrigReloadChimeraParts = nullptr;
    if (g_GetAnimFileAddr)
    {
        DisableAndRemoveHook(g_GetAnimFileAddr);
        g_GetAnimFileAddr = nullptr;
    }
    g_OrigGetAnimFile = nullptr;
    if (g_HideMagazineAddr)
    {
        DisableAndRemoveHook(g_HideMagazineAddr);
        g_HideMagazineAddr = nullptr;
    }
    g_OrigHideMagazine = nullptr;
}

namespace
{

    using DefineFireSound_t = void(__fastcall*)(void*, unsigned int, unsigned __int64,
                                                const char*, char, char, unsigned int*);
    static DefineFireSound_t g_OrigDefineFireSound = nullptr;

    static bool ApplyEventSoundSEH(void* sys, const char* name,
                                   unsigned int* outHashes, bool alsoSuppressed)
    {
        __try
        {
            std::uint8_t* obj =
                *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(sys) + 0x1d8);
            if (!obj || !outHashes)
                return false;
            std::uint8_t* vtbl = *reinterpret_cast<std::uint8_t**>(obj);
            if (!vtbl)
                return false;
            using Hasher_t = unsigned int(__fastcall*)(void*, const char*);
            Hasher_t fn = *reinterpret_cast<Hasher_t*>(vtbl + 0x18);
            if (!fn)
                return false;
            const unsigned int h = fn(obj, name);
            outHashes[0] = h;
            outHashes[2] = h;
            if (alsoSuppressed)
            {
                outHashes[1] = h;
                outHashes[3] = h;
            }
            return true;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static bool ApplySupSoundSEH(void* sys, const char* name, const char* enemyName,
                                 unsigned int* outHashes)
    {
        __try
        {
            std::uint8_t* obj =
                *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(sys) + 0x1d8);
            if (!obj || !outHashes)
                return false;
            std::uint8_t* vtbl = *reinterpret_cast<std::uint8_t**>(obj);
            if (!vtbl)
                return false;
            using Hasher_t = unsigned int(__fastcall*)(void*, const char*);
            Hasher_t fn = *reinterpret_cast<Hasher_t*>(vtbl + 0x18);
            if (!fn)
                return false;
            const unsigned int h = fn(obj, name);
            outHashes[1] = h;
            outHashes[3] = enemyName ? fn(obj, enemyName) : h;
            return true;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static bool RootTerminatedWithinSEH(const char* root, size_t maxLen)
    {
        __try
        {
            for (size_t i = 0; i < maxLen; ++i)
                if (root[i] == '\0')
                    return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void __fastcall hkDefineFireSound(void* sys, unsigned int eqpType,
                                             unsigned __int64 r8, const char* root,
                                             char suffixSel, char buildSup,
                                             unsigned int* outHashes)
    {
        FireSoundSpec spec;
        bool found = false;
        if (root)
        {
            const char* base = reinterpret_cast<const char*>(PartCurrentBuf(g_Snd.pb));
            if (base && root >= base)
            {
                const std::uintptr_t off = static_cast<std::uintptr_t>(root - base);
                if ((off & 7) == 0)
                {
                    const int idx = static_cast<int>(off >> 3);
                    std::lock_guard<std::mutex> lock(g_FireSoundMutex);
                    auto it = g_FireSoundByRow.find(idx);
                    if (it != g_FireSoundByRow.end())
                    {
                        spec = it->second;
                        found = true;
                    }
                }
            }
        }

        const char* safeRoot = root;
        if (!root || !RootTerminatedWithinSEH(root, 40))
        {
            safeRoot = "ar00";
            static std::atomic<bool> s_BadRootWarned{ false };
            if (!s_BadRootWarned.exchange(true))
                LogWarn("EquipParam: fire-sound root missing (eqpType=%u), using ar00", eqpType);
        }

        const bool haveSup = found && !spec.supText.empty();

        bool handled = false;
        bool origRan = false;
        if (found && !spec.text.empty())
        {
            if (spec.isEvent)
            {
                if (!haveSup)
                {
                    g_OrigDefineFireSound(sys, eqpType, r8, safeRoot, suffixSel,
                                          buildSup, outHashes);
                    origRan = true;
                }
                handled = ApplyEventSoundSEH(sys, spec.text.c_str(), outHashes,
                                             haveSup);
                if (!handled && origRan)
                    handled = true;
            }
            else
            {
                const unsigned int et = (spec.mSeg >= 0)
                    ? static_cast<unsigned int>(spec.mSeg) : eqpType;
                g_OrigDefineFireSound(sys, et, r8, spec.text.c_str(),
                                      suffixSel, buildSup, outHashes);
                handled = true;
            }
        }
        if (!handled)
            g_OrigDefineFireSound(sys, eqpType, r8, safeRoot, suffixSel, buildSup,
                                  outHashes);
        if (found && !spec.supText.empty())
        {
            std::string supName;
            std::string enemySupName;
            if (spec.supIsEvent)
                supName = spec.supText;
            else
            {
                supName = "sfx_w_p_";
                supName += spec.supText;
                supName += "_sup";
                supName += suffixSel != 0 ? "_nonact" : "_active";
                enemySupName = supName;
                enemySupName[6] = 'e';
            }
            ApplySupSoundSEH(sys, supName.c_str(),
                             enemySupName.empty() ? nullptr : enemySupName.c_str(), outHashes);
        }
    }
}

bool Install_FireSoundOverride_Hook()
{
    void* target = ResolveGameAddress(gAddr.WeaponSystem_DefineWeaponFireSound);
    if (!target)
        return true;
    const bool ok = CreateAndEnableHook(
        target, &hkDefineFireSound,
        reinterpret_cast<void**>(&g_OrigDefineFireSound));
    if (!ok)
        LogError("EquipParam: DefineWeaponFireSound hook failed, custom fire sounds off");
    return ok;
}

void Uninstall_FireSoundOverride_Hook()
{
    if (gAddr.WeaponSystem_DefineWeaponFireSound && g_OrigDefineFireSound)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.WeaponSystem_DefineWeaponFireSound));
    g_OrigDefineFireSound = nullptr;
    std::lock_guard<std::mutex> lock(g_FireSoundMutex);
    g_FireSoundByRow.clear();
}

bool Install_GetAttackIdGuard()
{
    void* target = ResolveGameAddress(gAddr.EquipParams_GetAttackIdByEquipId);
    if (!target)
        return true;
    const bool ok = CreateAndEnableHook(
        target, &hkGetAttackIdByEquipId,
        reinterpret_cast<void**>(&g_OrigGetAttackId));
    if (!ok)
        LogError("EquipParam: GetAttackIdByEquipId hook failed, custom receiver attackIds off");
    return ok;
}

void Uninstall_GetAttackIdGuard()
{
    if (gAddr.EquipParams_GetAttackIdByEquipId && g_OrigGetAttackId)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipParams_GetAttackIdByEquipId));
    g_OrigGetAttackId = nullptr;
}

namespace
{
    using UpdateLoadoutRequest_t = void(__fastcall*)(void*, std::uint32_t);
    static UpdateLoadoutRequest_t g_OrigUpdateLoadoutRequest = nullptr;


    static thread_local bool t_LoadoutBuildActive = false;

    using GetGunInfoById_t = void*(__fastcall*)(void* self, unsigned int equipId);
    static GetGunInfoById_t g_OrigGetGunInfoById = nullptr;

    static thread_local unsigned char t_GunInfoRing[8][0x90];
    static thread_local int t_GunInfoRingIdx = 0;

    static void* BuildLoadoutGunInfoOnMissSEH(void* self, unsigned int equipId)
    {
        unsigned char* buf = t_GunInfoRing[t_GunInfoRingIdx & 7];
        t_GunInfoRingIdx = (t_GunInfoRingIdx + 1) & 7;
        __try
        {
            std::memset(buf, 0, 0x90);
            *reinterpret_cast<std::uint16_t*>(buf + 0x5e) =
                static_cast<std::uint16_t>(equipId);
            void** vt = *reinterpret_cast<void***>(self);
            using BuildGunInfo_t = void(__fastcall*)(void*, unsigned int, void*);
            BuildGunInfo_t builder =
                reinterpret_cast<BuildGunInfo_t>(vt[(0x158 + 0xF0) / 8]);
            builder(self, equipId, buf);
            return buf;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            std::memset(buf, 0, 0x90);
            *reinterpret_cast<std::uint16_t*>(buf + 0x5e) =
                static_cast<std::uint16_t>(equipId);
            buf[0x7a] = 1;
            return buf;
        }
    }

    static void* __fastcall hkGetGunInfoById(void* self, unsigned int equipId)
    {
        void* r = g_OrigGetGunInfoById(self, equipId);
        if (r != nullptr)
            return r;
        if (!t_LoadoutBuildActive || self == nullptr)
            return nullptr;
        return BuildLoadoutGunInfoOnMissSEH(self, equipId);
    }

    static bool WeaponEquipIdIsPhantom(std::int32_t equipId)
    {
        if (equipId == 0)
            return false;
        const std::int32_t idx = EquipIdCompression::ComputeCompressed(equipId);
        if (!EquipIdCompression::IsCompressedInBounds(idx))
        {
            if (equipId < EquipIdCompression::kExtendedAllocFirst)
                return false;
            V_ExtendedEquipRow row{};
            if (!TppEquip_GetExtendedEquipRow(equipId, &row))
                return true;
        }
        else
        {
            std::uint16_t* tw = static_cast<std::uint16_t*>(
                ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
            if (!tw)
                return false;
            if (tw[idx] == 0)
                return true;
        }
        const bool costResolvedWithoutGunBasic =
            (equipId >= 0x230 && equipId <= 0x366) || (equipId >= 0x500 && equipId <= 0x525);
        if (costResolvedWithoutGunBasic)
            return false;
        const std::uint32_t type =
            GetEquipTypeForEquipId(static_cast<std::uint32_t>(equipId)) & 0x1F;
        return type < 1 || type > 8;
    }

    static void BlankPhantomLoadoutWeaponsSEH(void* self, std::uint32_t slot,
                                              std::int32_t* outPinned,
                                              int* outPinnedCount)
    {
        __try
        {
            std::uint8_t* self8 = static_cast<std::uint8_t*>(self);
            std::uint8_t* loadoutSubsys = *reinterpret_cast<std::uint8_t**>(self8 + 0x1a0);
            if (!loadoutSubsys) return;
            std::uint8_t* l138 = *reinterpret_cast<std::uint8_t**>(loadoutSubsys + 0x138);
            if (!l138) return;
            std::uint8_t* sourceBase = *reinterpret_cast<std::uint8_t**>(l138 + 0x98);
            if (!sourceBase) return;
            std::uint8_t* sourceReq = sourceBase + static_cast<std::size_t>(slot) * 0xa0;

            std::uint8_t* workingReq = nullptr;
            std::uint8_t* workingBase = *reinterpret_cast<std::uint8_t**>(self8 + 0x1e0);
            if (workingBase)
            {
                const std::int32_t bias = *reinterpret_cast<std::int32_t*>(loadoutSubsys + 0xc);
                workingReq = workingBase
                    + static_cast<std::size_t>(static_cast<std::int32_t>(slot) - bias) * 0xb0;
            }

            for (int i = 0; i < 3; ++i)
            {
                std::int32_t* srcW =
                    reinterpret_cast<std::int32_t*>(sourceReq + static_cast<std::size_t>(i) * 0x18);
                const std::int32_t id = *srcW;
                const std::int32_t movedTo = equip::IsLeasedAlias(id) ? id : equip::ResolveReal(id);
                if (movedTo != id && !WeaponEquipIdIsPhantom(movedTo))
                {
                    *srcW = movedTo;
                    if (workingReq)
                        *reinterpret_cast<std::int32_t*>(
                            workingReq + 0x10 + static_cast<std::size_t>(i) * 0x18) = movedTo;
                    continue;
                }
                if (!WeaponEquipIdIsPhantom(id))
                {
                    if (id > 0 && id < 0x289 && *outPinnedCount < 3)
                        outPinned[(*outPinnedCount)++] = id;
                    continue;
                }
                *srcW = 0;
                if (workingReq)
                    *reinterpret_cast<std::int32_t*>(
                        workingReq + 0x10 + static_cast<std::size_t>(i) * 0x18) = 0;
                LogWarn("EquipParam: loadout cell %d held equipId=%d that is no longer a weapon, cleared", i, id);
            }
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
        }
    }

    static int ReadLoadoutSlotIdsSEH(void* self, std::uint32_t slot,
                                     std::int32_t* src, std::int32_t* work)
    {
        __try
        {
            std::uint8_t* self8 = static_cast<std::uint8_t*>(self);
            std::uint8_t* loadoutSubsys = *reinterpret_cast<std::uint8_t**>(self8 + 0x1a0);
            if (!loadoutSubsys) return 0;
            std::uint8_t* l138 = *reinterpret_cast<std::uint8_t**>(loadoutSubsys + 0x138);
            if (!l138) return 0;
            std::uint8_t* sourceBase = *reinterpret_cast<std::uint8_t**>(l138 + 0x98);
            if (!sourceBase) return 0;
            std::uint8_t* sourceReq = sourceBase + static_cast<std::size_t>(slot) * 0xa0;
            for (int i = 0; i < 3; ++i)
                src[i] = *reinterpret_cast<std::int32_t*>(
                    sourceReq + static_cast<std::size_t>(i) * 0x18);

            std::uint8_t* workingBase = *reinterpret_cast<std::uint8_t**>(self8 + 0x1e0);
            if (!workingBase)
                return 1;
            const std::int32_t bias = *reinterpret_cast<std::int32_t*>(loadoutSubsys + 0xc);
            std::uint8_t* workingReq = workingBase
                + static_cast<std::size_t>(static_cast<std::int32_t>(slot) - bias) * 0xb0;
            for (int i = 0; i < 3; ++i)
                work[i] = *reinterpret_cast<std::int32_t*>(
                    workingReq + 0x10 + static_cast<std::size_t>(i) * 0x18);
            return 2;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return 0;
        }
    }

    static std::atomic<int> g_DropLogged{ 0 };

    static std::int32_t* LoadoutSourceCellSEH(void* self, std::uint32_t slot, int sub)
    {
        __try
        {
            std::uint8_t* self8 = static_cast<std::uint8_t*>(self);
            std::uint8_t* loadoutSubsys = *reinterpret_cast<std::uint8_t**>(self8 + 0x1a0);
            if (!loadoutSubsys) return nullptr;
            std::uint8_t* l138 = *reinterpret_cast<std::uint8_t**>(loadoutSubsys + 0x138);
            if (!l138) return nullptr;
            std::uint8_t* sourceBase = *reinterpret_cast<std::uint8_t**>(l138 + 0x98);
            if (!sourceBase) return nullptr;
            std::uint8_t* sourceReq = sourceBase + static_cast<std::size_t>(slot) * 0xa0;
            return reinterpret_cast<std::int32_t*>(
                sourceReq + static_cast<std::size_t>(sub) * 0x18);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return nullptr;
        }
    }

    static std::int32_t* LoadoutWorkingCellSEH(void* self, std::uint32_t slot, int sub)
    {
        __try
        {
            std::uint8_t* self8 = static_cast<std::uint8_t*>(self);
            std::uint8_t* loadoutSubsys = *reinterpret_cast<std::uint8_t**>(self8 + 0x1a0);
            std::uint8_t* workingBase = *reinterpret_cast<std::uint8_t**>(self8 + 0x1e0);
            if (!loadoutSubsys || !workingBase) return nullptr;
            const std::int32_t bias = *reinterpret_cast<std::int32_t*>(loadoutSubsys + 0xc);
            std::uint8_t* workingReq = workingBase
                + static_cast<std::size_t>(static_cast<std::int32_t>(slot) - bias) * 0xb0;
            return reinterpret_cast<std::int32_t*>(
                workingReq + 0x10 + static_cast<std::size_t>(sub) * 0x18);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return nullptr;
        }
    }

    struct EngineIdSwap
    {
        std::int32_t* cell = nullptr;
        std::int32_t  realId = 0;
        std::int32_t  engineId = 0;
    };

    static bool ReadCellSEH(const std::int32_t* cell, std::int32_t& out)
    {
        __try
        {
            out = *cell;
            return true;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static void WriteCellIfSEH(std::int32_t* cell, std::int32_t expect, std::int32_t value)
    {
        __try
        {
            if (*cell == expect)
                *cell = value;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
        }
    }

    static int SwapLoadoutWeaponsToEngineIds(void* self, std::uint32_t slot, EngineIdSwap* swaps)
    {
        int count = 0;
        for (int sub = 0; sub < 3; ++sub)
        {
            std::int32_t* cells[2] = { LoadoutSourceCellSEH(self, slot, sub),
                                       LoadoutWorkingCellSEH(self, slot, sub) };
            for (std::int32_t* cell : cells)
            {
                std::int32_t value = 0;
                if (!cell || !ReadCellSEH(cell, value) || value <= 0)
                    continue;
                const std::int32_t realId = equip::ResolveReal(value);
                const std::int32_t engineId = equip::ToEngine(realId);
                if (engineId == value)
                    continue;
                WriteCellIfSEH(cell, value, engineId);
                swaps[count++] = { cell, realId, engineId };
            }
        }
        return count;
    }

    static constexpr std::size_t kLoadoutListOffsets[] = { 0x48, 0x68 };   // LoadoutRequest: support u32 x8, items u32 x8
    static constexpr int         kLoadoutListCells     = 8;

    static int SwapLoadoutSupportItemsToEngineIds(void* self, std::uint32_t slot, EngineIdSwap* swaps, int count)
    {
        std::uint8_t* requests[2] = {
            reinterpret_cast<std::uint8_t*>(LoadoutSourceCellSEH(self, slot, 0)),
            reinterpret_cast<std::uint8_t*>(LoadoutWorkingCellSEH(self, slot, 0)) };
        for (std::uint8_t* request : requests)
        {
            if (!request)
                continue;
            for (const std::size_t list : kLoadoutListOffsets)
                for (int i = 0; i < kLoadoutListCells; ++i)
                {
                    auto* cell = reinterpret_cast<std::int32_t*>(request + list + i * sizeof(std::int32_t));
                    std::int32_t value = 0;
                    if (!ReadCellSEH(cell, value) || value <= 0)
                        continue;
                    const std::int32_t realId = equip::ResolveReal(value);
                    std::int32_t engineId = equip::ToEngine(realId);
                    if (engineId == EquipIdCompression::kNoEquipId)
                        engineId = 0;
                    if (engineId == value)
                        continue;
                    WriteCellIfSEH(cell, value, engineId);
                    swaps[count++] = { cell, realId, engineId };
                }
        }
        return count;
    }

    static std::uint16_t* LoadoutSourceMaskSEH(void* self, std::uint32_t slot)
    {
        __try
        {
            std::uint8_t* self8 = static_cast<std::uint8_t*>(self);
            std::uint8_t* loadoutSubsys = *reinterpret_cast<std::uint8_t**>(self8 + 0x1a0);
            if (!loadoutSubsys) return nullptr;
            std::uint8_t* l138 = *reinterpret_cast<std::uint8_t**>(loadoutSubsys + 0x138);
            if (!l138) return nullptr;
            std::uint8_t* sourceBase = *reinterpret_cast<std::uint8_t**>(l138 + 0x98);
            if (!sourceBase) return nullptr;
            return reinterpret_cast<std::uint16_t*>(
                sourceBase + static_cast<std::size_t>(slot) * 0xa0 + 0x9c);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return nullptr;
        }
    }

    static constexpr std::uint32_t kLoadoutSlotCount = 4;
    static std::atomic<std::uint32_t> g_HiddenSubBits[kLoadoutSlotCount] = {};
    static std::atomic<bool> g_HideWasArmed{ false };

    static bool DirtySourceSubSlotsSEH(void* self, std::uint32_t slot,
                                       std::uint32_t bits)
    {
        std::uint16_t* mask = LoadoutSourceMaskSEH(self, slot);
        if (!mask || !bits)
            return false;
        __try
        {
            *mask = static_cast<std::uint16_t>(*mask | bits);
            return true;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static void ReDirtyHiddenLoadoutIdsOnLift(void* self)
    {
        const bool armed = DeployGuard::ShouldDropExtendedIds();
        if (!g_HideWasArmed.exchange(armed, std::memory_order_relaxed) || armed)
            return;

        for (std::uint32_t s = 0; s < kLoadoutSlotCount; ++s)
        {
            const std::uint32_t bits =
                g_HiddenSubBits[s].exchange(0, std::memory_order_relaxed);
            if (bits)
                DirtySourceSubSlotsSEH(self, s, bits);
        }
    }
    static int SuppressExtendedLoadoutIdsSEH(void* self, std::uint32_t slot,
                                             std::int32_t* saved)
    {
        int hidden = 0;
        for (int i = 0; i < 3; ++i)
        {
            std::int32_t* cell = LoadoutSourceCellSEH(self, slot, i);
            if (!cell)
                continue;
            __try
            {
                const std::int32_t id = *cell;
                V_ExtendedEquipRow row{};
                if (!TppEquip_GetExtendedEquipRow(id, &row))
                    continue;
                saved[i] = id;
                *cell = 0;
                ++hidden;
            }
            __except (SehAvOnly(GetExceptionCode()))
            {
                saved[i] = -1;
                continue;
            }
            if (slot < kLoadoutSlotCount)
                g_HiddenSubBits[slot].fetch_or(1u << i, std::memory_order_relaxed);
            if (g_DropLogged.fetch_add(1) < 24)
                LogWarn("DeployGuard: loadout slot=%u sub=%d extended equipId %d hidden this deploy (%s), save untouched",
                    slot, i, saved[i],
                    DeployGuard::IsExtendedDropPermanent()
                        ? "extended InfoList unavailable" : "previous run died mid-load");
        }
        return hidden;
    }

    static void RestoreExtendedLoadoutIdsSEH(void* self, std::uint32_t slot,
                                             const std::int32_t* saved)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (saved[i] < 0)
                continue;
            std::int32_t* cell = LoadoutSourceCellSEH(self, slot, i);
            if (!cell)
                continue;
            __try
            {
                if (*cell == 0)
                    *cell = saved[i];
            }
            __except (SehAvOnly(GetExceptionCode()))
            {
            }
        }
    }

    static void __fastcall hkUpdateLoadoutRequest(void* self, std::uint32_t slot)
    {
        HealSupplyAmmoCountTable();
        FillCustomMotionEntriesEarly();
        std::int32_t pinned[3] = {};
        int pinnedCount = 0;
        BlankPhantomLoadoutWeaponsSEH(self, slot, pinned, &pinnedCount);
        for (int i = 0; i < pinnedCount; ++i)
            V_FrameWorkState::NoteStickyPinnedEquipId(pinned[i]);
        ReDirtyHiddenLoadoutIdsOnLift(self);

        std::int32_t sBefore[3] = {}, wBefore[3] = {};
        const int okBefore = ReadLoadoutSlotIdsSEH(self, slot, sBefore, wBefore);
        std::int32_t suppressed[3] = { -1, -1, -1 };
        int suppressedCount = 0;
        if (okBefore >= 1)
        {
            std::int32_t realBefore[3] = {};
            for (int i = 0; i < 3; ++i)
                realBefore[i] = equip::FromEngine(sBefore[i]);
            DeployGuard::NoteLoadoutSlot(slot, realBefore);
            if (DeployGuard::ShouldDropExtendedIds())
                suppressedCount = SuppressExtendedLoadoutIdsSEH(self, slot, suppressed);
        }

        EngineIdSwap swaps[48];
        int swapCount = SwapLoadoutWeaponsToEngineIds(self, slot, swaps);
        swapCount = SwapLoadoutSupportItemsToEngineIds(self, slot, swaps, swapCount);

        t_LoadoutBuildActive = true;
        __try
        {
            g_OrigUpdateLoadoutRequest(self, slot);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 5000)
            {
                lastMs = now;
                LogWarn("EquipParam: UpdateLoadoutRequest fault caught, request skipped");
            }
        }
        t_LoadoutBuildActive = false;

        for (int i = 0; i < swapCount; ++i)
            WriteCellIfSEH(swaps[i].cell, swaps[i].engineId, swaps[i].realId);

        if (suppressedCount > 0)
            RestoreExtendedLoadoutIdsSEH(self, slot, suppressed);
    }
}

bool Install_LoadoutRequestGuard()
{
    void* target = ResolveGameAddress(gAddr.CorePlugin_UpdateLoadoutRequest);
    if (!target)
        return true;
    const bool ok = CreateAndEnableHook(
        target, &hkUpdateLoadoutRequest,
        reinterpret_cast<void**>(&g_OrigUpdateLoadoutRequest));
    if (!ok)
        LogError("EquipParam: UpdateLoadoutRequest hook failed, stale loadout weapons can crash deploy");
    return ok;
}

void Uninstall_LoadoutRequestGuard()
{
    if (gAddr.CorePlugin_UpdateLoadoutRequest && g_OrigUpdateLoadoutRequest)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.CorePlugin_UpdateLoadoutRequest));
    g_OrigUpdateLoadoutRequest = nullptr;
}

bool Install_LoadoutGunInfoGetOrBuild()
{
    void* target = ResolveGameAddress(gAddr.EquipSystem_GetGunInfoById);
    if (!target)
    {
        LogError("EquipParam: GetGunInfoById address missing, non-resident loadout weapons can crash");
        return true;
    }
    const bool ok = CreateAndEnableHook(
        target, &hkGetGunInfoById,
        reinterpret_cast<void**>(&g_OrigGetGunInfoById));
    if (!ok)
        LogError("EquipParam: GetGunInfoById hook failed, non-resident loadout weapons can crash");
    return ok;
}

void Uninstall_LoadoutGunInfoGetOrBuild()
{
    if (gAddr.EquipSystem_GetGunInfoById && g_OrigGetGunInfoById)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipSystem_GetGunInfoById));
    g_OrigGetGunInfoById = nullptr;
}

namespace
{
    using GetSuppressorAmount_t = unsigned char(__fastcall*)(void* self, std::uintptr_t developRow);
    static GetSuppressorAmount_t g_OrigGetSuppressorAmount = nullptr;

    static int ReadSuppressorRowSEH(const std::uint8_t* gunBasic, const std::uint8_t* muzzleOpt,
                                    int subId, int* outSeg)
    {
        __try
        {
            const int muzzleOptionId = gunBasic[static_cast<size_t>(subId - 1) * 0x0c + 5];
            if (muzzleOptionId == 0)
            {
                *outSeg = 0;
                return 1;
            }
            const std::uint8_t* mrow = muzzleOpt + static_cast<size_t>(muzzleOptionId - 1) * 3;
            if ((mrow[2] & 1) == 0)
            {
                *outSeg = 0;
                return 1;
            }
            const unsigned char dur = mrow[1];
            int seg;
            if (dur == 0xff)      seg = 4;
            else if (dur < 0x10)  seg = 1;
            else                  seg = (dur > 0x1e) ? 3 : 2;
            *outSeg = seg;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int CallDevResolverSEH(void* self, unsigned int developRow, int* outVal)
    {
        __try
        {
            void** vtbl = *reinterpret_cast<void***>(self);
            using DevResolver_t = unsigned short(__fastcall*)(void*, std::uintptr_t);
            DevResolver_t fn = reinterpret_cast<DevResolver_t>(
                *reinterpret_cast<void**>(reinterpret_cast<char*>(vtbl) + 0x130));
            *outVal = static_cast<int>(fn(self, developRow));
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static unsigned char __fastcall hkGetSuppressorAmount(void* self, std::uintptr_t developRow)
    {
        const unsigned char orig =
            g_OrigGetSuppressorAmount ? g_OrigGetSuppressorAmount(self, developRow) : 0;
        if (orig != 0)
            return orig;

        const unsigned int row = static_cast<unsigned int>(developRow & 0xFFFF);

        int equipId = -1;
        CallDevResolverSEH(self, row, &equipId);
        const int subId = (equipId > 0) ? TppEquip_GetSubIdForEquipId(equipId) : 0;

        int seg = -1;
        const std::uint8_t* gunBasic = ImplBufPtr(0x08);
        const std::uint8_t* muzzleOpt = ImplBufPtr(0x28);
        if (subId > 0 && gunBasic && muzzleOpt)
            ReadSuppressorRowSEH(gunBasic, muzzleOpt, subId, &seg);

        if (subId > 0 && seg > 0)
            return static_cast<unsigned char>(seg);
        return orig;
    }
}

bool Install_SuppressorGauge_Hook()
{
    void* target = ResolveGameAddress(gAddr.EquipDevelopControllerImpl_GetSuppressorAmount);
    if (!target)
    {
        LogError("SuppressorGauge: GetSuppressorAmount address missing, custom suppressor gauge off");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetSuppressorAmount,
        reinterpret_cast<void**>(&g_OrigGetSuppressorAmount));
    if (!ok)
        LogError("SuppressorGauge: GetSuppressorAmount hook failed, custom suppressor gauge off");
    return ok;
}

void Uninstall_SuppressorGauge_Hook()
{
    if (gAddr.EquipDevelopControllerImpl_GetSuppressorAmount)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipDevelopControllerImpl_GetSuppressorAmount));
    g_OrigGetSuppressorAmount = nullptr;
}

namespace
{
    static const int kDamageStride     = 0x1a;
    static const int kDamageCapacity   = 489;
    static const int kDamageCustomBase = kDamageCapacity;

    static const int kDamageCustomHardMax = 0xFFFE;
    static const int kDamageCustomCap  = kDamageCustomHardMax - kDamageCustomBase;

    struct DamageRow
    {
        int damageId;
        int lethalDamage, staminaDamage, impactForce;
        int oldLethalDamageVsSoldier, oldLethalDamageVsPlayer, oldLethalDamageVsVehicle;
        int oldStaminaDamageVsSoldier, oldStaminaDamageVsPlayer, blowPower, shieldDamage;
        int flags;
        int damageSource, injureType, injurePart, injureMaxDistance, injureRate;
    };

    static std::vector<DamageRow> g_DamageRows;
    static std::map<std::string, int> g_DamageNameToId;
    static int g_DamageCustomNextFree = kDamageCustomBase;

    static std::vector<std::uint8_t> g_CustomDamageBuffer;

    static bool DamageIdIsCustom(int id)
    {
        return id >= kDamageCustomBase && id < kDamageCustomBase + kDamageCustomCap;
    }

    static std::uint8_t* CustomDamageRowPtr(int id)
    {
        if (!DamageIdIsCustom(id))
            return nullptr;
        const size_t off = static_cast<size_t>(id - kDamageCustomBase) * kDamageStride;
        if (off + kDamageStride > g_CustomDamageBuffer.size())
            return nullptr;
        return g_CustomDamageBuffer.data() + off;
    }

    static std::uint8_t* CustomDamageRowPtrGrow(int id)
    {
        if (!DamageIdIsCustom(id))
            return nullptr;
        const size_t need = (static_cast<size_t>(id - kDamageCustomBase) + 1) * kDamageStride;
        if (need > g_CustomDamageBuffer.size())
        {
            const size_t chunk = static_cast<size_t>(256) * kDamageStride;
            const size_t grown = ((need + chunk - 1) / chunk) * chunk;
            g_CustomDamageBuffer.resize(grown, 0);
        }
        return g_CustomDamageBuffer.data() +
               static_cast<size_t>(id - kDamageCustomBase) * kDamageStride;
    }

    static std::uint8_t* DamageBufBase()
    {
        void** loc = static_cast<void**>(
            ResolveGameAddress(gAddr.DamageParameterTable_Instance));
        if (!loc)
            return nullptr;
        std::uint8_t* inst = ReadPtrSEH(loc);
        if (!inst)
            return nullptr;
        return inst + 8;
    }

    static void WriteDamageRowSEH(std::uint8_t* buf, const DamageRow& r)
    {
        std::uint8_t* p = CustomDamageRowPtrGrow(r.damageId);
        if (!p)
        {
            if (!buf || r.damageId <= 0 || r.damageId >= kDamageCapacity)
                return;
            p = buf + static_cast<size_t>(r.damageId) * kDamageStride;
        }
        __try
        {
            *reinterpret_cast<std::uint16_t*>(p + 0x00) = static_cast<std::uint16_t>(r.lethalDamage & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x02) = static_cast<std::uint16_t>(r.staminaDamage & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x04) = static_cast<std::uint16_t>(r.impactForce & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x06) = static_cast<std::uint16_t>(r.oldLethalDamageVsSoldier & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x08) = static_cast<std::uint16_t>(r.oldLethalDamageVsPlayer & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0a) = static_cast<std::uint16_t>(r.oldLethalDamageVsVehicle & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0c) = static_cast<std::uint16_t>(r.oldStaminaDamageVsSoldier & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0e) = static_cast<std::uint16_t>(r.oldStaminaDamageVsPlayer & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x10) = static_cast<std::uint16_t>(r.blowPower & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x12) = static_cast<std::uint16_t>(r.shieldDamage & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x14) = static_cast<std::uint16_t>(r.flags & 0xFFFF);
            p[0x16] = static_cast<std::uint8_t>(r.damageSource & 0xFF);
            p[0x17] = static_cast<std::uint8_t>((r.injureType & 0xF) | ((r.injurePart & 0xF) << 4));
            p[0x18] = static_cast<std::uint8_t>(r.injureMaxDistance & 0xFF);
            p[0x19] = static_cast<std::uint8_t>(r.injureRate & 0xFF);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    using ReloadDamageParameter_t = int(__fastcall*)(lua_State* L);
    static ReloadDamageParameter_t g_OrigReloadDamage = nullptr;

    static void ReapplyDamageRows()
    {
        std::vector<DamageRow> snapshot;
        {
            std::lock_guard<std::recursive_mutex> lock(g_Mutex);
            snapshot = g_DamageRows;
        }
        std::uint8_t* buf = DamageBufBase();
        for (const auto& r : snapshot)
            WriteDamageRowSEH(buf, r);
    }

    static int __fastcall hkReloadDamageParameter(lua_State* L)
    {
        const int ret = g_OrigReloadDamage ? g_OrigReloadDamage(L) : 0;
        ReapplyDamageRows();
        return ret;
    }

    using GetDamageParameter_t = std::uint8_t*(__fastcall*)(std::uint8_t* self, int attackId);
    static GetDamageParameter_t g_OrigGetDamageParameter = nullptr;

    static std::uint8_t g_DamageZeroRow[kDamageStride] = {};

    static std::uint8_t* __fastcall hkGetDamageParameter(std::uint8_t* self, int attackId)
    {
        std::uint8_t* custom = CustomDamageRowPtr(attackId);
        if (custom)
            return custom;

        if (DamageIdIsCustom(attackId))
            return g_DamageZeroRow;
        if (g_OrigGetDamageParameter)
            return g_OrigGetDamageParameter(self, attackId);
        return self + 8 + static_cast<size_t>(static_cast<unsigned>(attackId)) * kDamageStride;
    }
}

int EquipParam_AllocateDamageSlotForName(const char* name)
{
    if (!name || !name[0])
        return 0;

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    auto it = g_DamageNameToId.find(name);
    if (it != g_DamageNameToId.end())
        return it->second;

    if (g_DamageCustomNextFree >= kDamageCustomBase + kDamageCustomCap)
    {
        LogError("DeclareDamages '%s': custom damage ids exhausted (%d), not allocated",
            name, kDamageCustomCap);
        return 0;
    }

    const int id = g_DamageCustomNextFree++;
    g_DamageNameToId[name] = id;

    CustomDamageRowPtrGrow(id);
    return id;
}

int __cdecl l_SetDamage(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        LogWarn("SetDamage: argument #1 must be a table");
        return 0;
    }

    int damageId = 0;
    if (!ReadNamedInt(L, 1, "damageId", damageId) || damageId <= 0)
    {
        LogWarn("SetDamage: missing or invalid damageId");
        return 0;
    }
    if (damageId >= kDamageCustomBase + kDamageCustomCap)
    {
        LogWarn("SetDamage: damageId=%d out of range 1..%d, not written",
            damageId, kDamageCustomBase + kDamageCustomCap - 1);
        return 0;
    }

    DamageRow r{};
    r.damageId = damageId;
    ReadNamedInt(L, 1, "lethalDamage",   r.lethalDamage);
    ReadNamedInt(L, 1, "staminaDamage",  r.staminaDamage);
    ReadNamedInt(L, 1, "impactForce",    r.impactForce);
    ReadNamedInt(L, 1, "lethalDamageUI", r.oldLethalDamageVsSoldier);   // old names keep the stored value as given
    ReadNamedInt(L, 1, "unk3", r.oldLethalDamageVsPlayer);
    ReadNamedInt(L, 1, "unk4", r.oldLethalDamageVsVehicle);
    ReadNamedInt(L, 1, "unk12", r.injureRate);

    auto readVanillaLethal = [&](const char* name, int& out)   // DamageParameterTables.lua units: the game's loader keeps a tenth
    {
        double value = 0.0;
        if (ReadNamedFloat(L, 1, name, value))
            out = static_cast<int>(static_cast<long long>(static_cast<float>(value) * 0.1f));
    };
    readVanillaLethal("oldLethalDamageVsSoldier", r.oldLethalDamageVsSoldier);
    readVanillaLethal("oldLethalDamageVsPlayer",  r.oldLethalDamageVsPlayer);
    readVanillaLethal("oldLethalDamageVsVehicle", r.oldLethalDamageVsVehicle);
    double injureRatePercent = 0.0;
    if (ReadNamedFloat(L, 1, "injureRate", injureRatePercent))
        r.injureRate = static_cast<int>(static_cast<long long>(static_cast<float>(injureRatePercent) * 0.01f * 200.0f));   // percent, stored doubled like the game's loader

    ReadNamedIntAlias(L, 1, "oldStaminaDamageVsSoldier", "unk5", r.oldStaminaDamageVsSoldier);
    ReadNamedIntAlias(L, 1, "oldStaminaDamageVsPlayer",  "unk6", r.oldStaminaDamageVsPlayer);
    ReadNamedIntAlias(L, 1, "blowPower",                 "unk7", r.blowPower);
    ReadNamedIntAlias(L, 1, "shieldDamage",              "unk8", r.shieldDamage);
    ReadNamedIntAlias(L, 1, "injureMaxDistance",         "unk11", r.injureMaxDistance);
    ReadNamedInt(L, 1, "damageSource", r.damageSource);
    ReadNamedInt(L, 1, "injureType",   r.injureType);
    ReadNamedInt(L, 1, "injurePart",   r.injurePart);

    auto warnWidth = [&](const char* name, int v, int maxV)
    {
        if (v < 0 || v > maxV)
            LogWarn("SetDamage damageId=%d: %s=%d out of 0..%d, wraps to %d",
                damageId, name, v, maxV, v & maxV);
    };
    warnWidth("lethalDamage",   r.lethalDamage,   0xFFFF);
    warnWidth("staminaDamage",  r.staminaDamage,  0xFFFF);
    warnWidth("impactForce",    r.impactForce,    0xFFFF);
    warnWidth("oldLethalDamageVsSoldier (stored)",  r.oldLethalDamageVsSoldier,  0xFFFF);
    warnWidth("oldLethalDamageVsPlayer (stored)",   r.oldLethalDamageVsPlayer,   0xFFFF);
    warnWidth("oldLethalDamageVsVehicle (stored)",  r.oldLethalDamageVsVehicle,  0xFFFF);
    warnWidth("oldStaminaDamageVsSoldier", r.oldStaminaDamageVsSoldier, 0xFFFF);
    warnWidth("oldStaminaDamageVsPlayer",  r.oldStaminaDamageVsPlayer,  0xFFFF);
    warnWidth("blowPower",                 r.blowPower,                 0xFFFF);
    warnWidth("shieldDamage",              r.shieldDamage,              0xFFFF);
    warnWidth("damageSource", r.damageSource, 0xFF);
    warnWidth("injureType",   r.injureType,   0xF);
    warnWidth("injurePart",   r.injurePart,   0xF);
    warnWidth("injureMaxDistance",   r.injureMaxDistance, 0xFF);
    warnWidth("injureRate (stored)", r.injureRate,        0xFF);

    auto readFlag = [&](const char* name, const char* legacy) -> int
    {
        int v = 0;
        if (ReadNamedInt(L, 1, name, v) && v)
            return 1;
        if (legacy && ReadNamedInt(L, 1, legacy, v) && v)
            return 1;
        return 0;
    };
    const int isBullet     = readFlag("isBullet", "hitNPC");
    const int isSniper     = readFlag("isSniper", "unk14");
    const int isShotgun    = readFlag("isShotgun", "unk15");
    const int isTranq      = readFlag("isTranq", nullptr);
    const int isStun       = readFlag("isStun", nullptr);
    const int isExplosive  = readFlag("isExplosive", "unk18");
    const int isMelee      = readFlag("isMelee", "unk19");
    const int isBlade      = readFlag("isBlade", "unk20");
    const int isFire       = readFlag("isFire", nullptr);
    const int isWater      = readFlag("isWater", "unk27");
    const int isElectric   = readFlag("isElectric", nullptr);
    const int isParasite   = readFlag("isParasite", "unk22");
    const int isGas        = readFlag("isGas", nullptr);
    const int isVehicleHit = readFlag("isVehicleHit", "unk24");
    const int unk25        = readFlag("unk25", nullptr);
    const int isPenetrating = readFlag("isPenetrating", "unk28");
    r.flags = (isBullet ? 0x1 : 0) | (isSniper ? 0x2 : 0)        | (isShotgun ? 0x4 : 0)    | (isTranq ? 0x8 : 0)
            | (isStun ? 0x10 : 0)  | (isExplosive ? 0x20 : 0)    | (isMelee ? 0x40 : 0)     | (isBlade ? 0x80 : 0)
            | (isFire ? 0x100 : 0) | (isWater ? 0x200 : 0)       | (isElectric ? 0x400 : 0) | (isParasite ? 0x800 : 0)
            | (isGas ? 0x1000 : 0) | (isVehicleHit ? 0x2000 : 0) | (unk25 ? 0x4000 : 0)     | (isPenetrating ? 0x8000 : 0);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    std::uint8_t* buf = DamageBufBase();
    if (!buf && damageId < kDamageCapacity)
    {
        LogError("SetDamage damageId=%d: damage table unavailable on this build, skipped", damageId);
        return 0;
    }

    bool replaced = false;
    for (auto& e : g_DamageRows)
        if (e.damageId == damageId) { e = r; replaced = true; break; }
    if (!replaced)
        g_DamageRows.push_back(r);

    const bool vanillaRow = damageId < kDamageCustomBase;
    const std::uint8_t* rowPtr = (buf && vanillaRow)
        ? buf + static_cast<size_t>(damageId) * kDamageStride : nullptr;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Damage, damageId, rowPtr,
                                   kDamageStride);

    WriteDamageRowSEH(buf, r);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Damage, damageId, rowPtr,
                                    kDamageStride);

    return 0;
}

int __cdecl l_DeclareDamages(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    auto declareOne = [&](const char* name) -> int
    {
        if (!name || !name[0])
            return 0;
        const int id = EquipParam_AllocateDamageSlotForName(name);
        if (id > 0)
            TppDamageConst_SetValue(name, static_cast<std::uint32_t>(id), L);
        return id;
    };

    if (g_lua_type(L, 1) == LUA_TTABLE)
    {
        const int n = static_cast<int>(g_lua_objlen(L, 1));
        int declared = 0;
        for (int i = 1; i <= n; ++i)
        {
            g_lua_rawgeti(L, 1, i);
            std::string nm;
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                size_t len = 0;
                const char* s = g_lua_tolstring(L, -1, &len);
                if (s)
                    nm.assign(s, len);
            }
            g_lua_settop(L, -2);
            if (!nm.empty() && declareOne(nm.c_str()) > 0)
                ++declared;
        }
        g_lua_pushnumber(L, static_cast<lua_Number>(declared));
        return 1;
    }

    std::string nm;
    if (g_lua_type(L, 1) == LUA_TSTRING)
    {
        size_t len = 0;
        const char* s = g_lua_tolstring(L, 1, &len);
        if (s)
            nm.assign(s, len);
    }
    const int id = declareOne(nm.empty() ? nullptr : nm.c_str());
    g_lua_pushnumber(L, static_cast<lua_Number>(id));
    return 1;
}

bool Install_DamageParameter_Hook()
{
    bool ok = true;

    void* reloadTarget = ResolveGameAddress(gAddr.DamageParameterTable_ReloadDamageParameter);
    if (reloadTarget)
    {
        const bool r = CreateAndEnableHook(
            reloadTarget, &hkReloadDamageParameter,
            reinterpret_cast<void**>(&g_OrigReloadDamage));
        if (!r)
            LogError("Damage: ReloadDamageParameter hook failed, damage overrides lost on reload");
        ok = ok && r;
    }
    else
    {
        LogError("Damage: ReloadDamageParameter address missing, damage overrides lost on reload");
    }

    void* getterTarget = ResolveGameAddress(gAddr.DamageParameterTable_GetDamageParameter);
    if (getterTarget)
    {
        const bool r = CreateAndEnableHook(
            getterTarget, &hkGetDamageParameter,
            reinterpret_cast<void**>(&g_OrigGetDamageParameter));
        if (!r)
            LogError("Damage: GetDamageParameter hook failed, custom damage ids off");
        ok = ok && r;
    }
    else
    {
        LogError("Damage: GetDamageParameter address missing, custom damage ids off");
    }

    return ok;
}

void Uninstall_DamageParameter_Hook()
{
    if (gAddr.DamageParameterTable_ReloadDamageParameter)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.DamageParameterTable_ReloadDamageParameter));
    g_OrigReloadDamage = nullptr;

    if (gAddr.DamageParameterTable_GetDamageParameter)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.DamageParameterTable_GetDamageParameter));
    g_OrigGetDamageParameter = nullptr;
}

namespace
{
    static std::mutex g_VanillaTaintMutex;
    static std::set<int> g_VanillaTaint[kVanillaSpace_Count];
    static std::map<int, std::vector<std::uint8_t>> g_VanillaPreImage[kVanillaSpace_Count];
    static bool g_VanillaTaintAny = false;

    static const int kVanillaRowMax = 32;

    static const char* const kVanillaSpaceNames[kVanillaSpace_Count] =
    {
        "receiver", "barrel", "magazine", "bullet", "stock", "muzzleOption",
        "sight", "underBarrel", "option", "gunBasic weapon", "damage"
    };

    static int CopyBytesSEH(const std::uint8_t* src, std::uint8_t* dst, int n)
    {
        __try
        {
            for (int k = 0; k < n; ++k)
                dst[k] = src[k];
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool VanillaTaintHas(int space, int id)
    {
        if (id <= 0)
            return false;
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        return g_VanillaTaint[space].find(id) != g_VanillaTaint[space].end();
    }

    static bool VanillaTaintSpaceUsed(int space)
    {
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        return !g_VanillaTaint[space].empty();
    }

    static void* ResolveEquipIdTableObj()
    {
        using GetQuark_t = std::uint8_t* (__fastcall*)();
        auto getQuark = reinterpret_cast<GetQuark_t>(
            ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        __try
        {
            std::uint8_t* quark = getQuark();
            if (!quark)
                return nullptr;
            std::uint8_t* app = *reinterpret_cast<std::uint8_t**>(quark + 0x98);
            if (!app)
                return nullptr;
            std::uint8_t* holder = *reinterpret_cast<std::uint8_t**>(app + 0x1e8);
            if (!holder)
                return nullptr;
            return *reinterpret_cast<void**>(holder + 8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static int EquipObjTypeSEH(void* obj, int equipId)
    {
        __try
        {
            void** vt = *reinterpret_cast<void***>(obj);
            using Fn = int(__fastcall*)(void*, int);
            return reinterpret_cast<Fn>(vt[0])(obj, equipId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int EquipObjWeaponIdSEH(void* obj, int equipId)
    {
        __try
        {
            void** vt = *reinterpret_cast<void***>(obj);
            using Fn = unsigned short(__fastcall*)(void*, int);
            return reinterpret_cast<Fn>(vt[2])(obj, equipId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int ReadPartByteSEH(const std::uint8_t* p, std::uint8_t& out)
    {
        __try
        {
            out = *p;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int PartRowByte(const PartBuffer& pb, int id, int byteOff)
    {
        if (id <= 0)
            return 0;
        const int bound = pb.active ? pb.maxId : pb.stockCount;
        if (id > bound)
            return 0;
        std::uint8_t* buf = ReadPtrSEH(PartPtrLoc(pb));
        if (!buf)
            return 0;
        std::uint8_t v = 0;
        if (ReadPartByteSEH(buf + static_cast<size_t>(id - 1) * pb.stride + byteOff, v) != 1)
            return 0;
        return v;
    }

    static unsigned short SafeGetAttackIdSEH(void* self, int equipId)
    {
        if (GunBasic_WeaponHasNoRow(equipId))
            return 0;
        __try
        {
            return g_OrigGetAttackId(self, equipId);
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return 0;
        }
    }

    static bool MagazineChainTainted(int ammoId)
    {
        if (ammoId <= 0)
            return false;
        if (VanillaTaintHas(kVanillaSpace_Magazine, ammoId))
            return true;
        const int bulletId = PartRowByte(g_Magazine, ammoId, 6);
        return bulletId > 0 && VanillaTaintHas(kVanillaSpace_Bullet, bulletId);
    }

    static int ReadChimeraDescSEH(const std::uint8_t* table, int slot,
                                  unsigned char* out11)
    {
        __try
        {
            const std::uint8_t* d = table + 0x12 + static_cast<size_t>(slot) * 0xe;
            for (int k = 0; k < 11; ++k)
                out11[k] = d[k];
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool PartsBytesTainted(const unsigned char* b)
    {
        if (VanillaTaintHas(kVanillaSpace_Receiver, b[0]))
            return true;
        if (VanillaTaintHas(kVanillaSpace_Barrel, b[1]))
            return true;
        if (VanillaTaintHas(kVanillaSpace_Stock, b[3]))
            return true;
        if (VanillaTaintHas(kVanillaSpace_MuzzleOption, b[5]))
            return true;
        if (VanillaTaintHas(kVanillaSpace_Sight, b[6])
            || VanillaTaintHas(kVanillaSpace_Sight, b[7]))
            return true;
        if (VanillaTaintHas(kVanillaSpace_Option, b[8])
            || VanillaTaintHas(kVanillaSpace_Option, b[9]))
            return true;
        if (MagazineChainTainted(b[2]))
            return true;
        if (b[10])
        {
            if (VanillaTaintHas(kVanillaSpace_UnderBarrel, b[10]))
                return true;
            const int ubReceiver = PartRowByte(g_UnderBarrel, b[10], 0);
            if (ubReceiver > 0 && VanillaTaintHas(kVanillaSpace_Receiver, ubReceiver))
                return true;
            if (MagazineChainTainted(PartRowByte(g_UnderBarrel, b[10], 1)))
                return true;
        }
        return false;
    }
}

void EquipParam_RefillMotionEntries()
{
    FillCustomMotionEntriesEarly();
}

void EquipParam_VanillaPreWrite(int space, int id, const unsigned char* row, int stride)
{
    if (space < 0 || space >= kVanillaSpace_Count || id <= 0 || !row
        || stride <= 0 || stride > kVanillaRowMax)
        return;

    {
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        if (g_VanillaPreImage[space].find(id) != g_VanillaPreImage[space].end())
            return;
    }

    std::uint8_t pre[kVanillaRowMax];
    if (CopyBytesSEH(row, pre, stride) != 1)
        return;

    std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
    g_VanillaPreImage[space].emplace(
        id, std::vector<std::uint8_t>(pre, pre + stride));
}

void EquipParam_VanillaPostWrite(int space, int id, const unsigned char* row, int stride)
{
    if (space < 0 || space >= kVanillaSpace_Count || id <= 0
        || stride <= 0 || stride > kVanillaRowMax)
        return;

    std::uint8_t now[kVanillaRowMax];
    const bool haveNow = row && CopyBytesSEH(row, now, stride) == 1;

    int transition = 0;
    {
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        bool matchesVanilla = false;
        auto it = g_VanillaPreImage[space].find(id);
        if (haveNow && it != g_VanillaPreImage[space].end()
            && it->second.size() == static_cast<size_t>(stride)
            && std::memcmp(it->second.data(), now, static_cast<size_t>(stride)) == 0)
            matchesVanilla = true;

        const bool was = g_VanillaTaint[space].count(id) != 0;
        if (!matchesVanilla && !was)
        {
            g_VanillaTaint[space].insert(id);
            transition = 1;
        }
        else if (matchesVanilla && was)
        {
            g_VanillaTaint[space].erase(id);
            transition = -1;
        }
        if (transition != 0)
        {
            bool any = false;
            for (int s = 0; s < kVanillaSpace_Count && !any; ++s)
                any = !g_VanillaTaint[s].empty();
            g_VanillaTaintAny = any;
        }
    }

    if (transition > 0)
        LogWarn("FobGuard: vanilla %s id %d modified, weapons using it blocked from FOB",
            kVanillaSpaceNames[space], id);
}

void EquipParam_VanillaForceTaint(int space, int id, const char* why)
{
    if (space < 0 || space >= kVanillaSpace_Count || id <= 0)
        return;

    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        inserted = g_VanillaTaint[space].insert(id).second;
        g_VanillaTaintAny = true;
    }
    if (inserted)
        LogWarn("FobGuard: vanilla %s id %d given %s, weapons using it blocked from FOB",
            kVanillaSpaceNames[space], id, why ? why : "modified behavior");
}

bool EquipParam_IsEquipIdFobTainted(unsigned int equipId, int isWeaponSlot)
{
    {
        std::lock_guard<std::mutex> lock(g_VanillaTaintMutex);
        if (!g_VanillaTaintAny)
            return false;
    }
    if (equipId == 0)
        return false;

    if (VanillaTaintSpaceUsed(kVanillaSpace_Damage))
    {
        void* self = ResolveGameAddress(gAddr.EquipParameterTablesImpl_Instance);
        if (self && g_OrigGetAttackId)
        {
            const int attackId = SafeGetAttackIdSEH(self, static_cast<int>(equipId));
            if (attackId > 0 && VanillaTaintHas(kVanillaSpace_Damage, attackId))
                return true;
        }
    }

    if (!isWeaponSlot)
        return false;

    unsigned char b[12] = {};

    if (equipId >= 0x367 && equipId <= 0x36C)
    {
        auto* chimera = static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.EquipSystem_ChimeraPartsSetWork));
        if (!chimera)
            return false;
        if (ReadChimeraDescSEH(chimera, static_cast<int>(equipId) - 0x367, b) != 1)
            return false;
    }
    else
    {
        void* obj = ResolveEquipIdTableObj();
        if (!obj)
            return false;
        const int eqpType = EquipObjTypeSEH(obj, static_cast<int>(equipId));
        if (eqpType < 1 || eqpType > 8)
            return false;
        const int weaponId = EquipObjWeaponIdSEH(obj, static_cast<int>(equipId));
        if (weaponId <= 0)
            return false;

        if (VanillaTaintHas(kVanillaSpace_Weapon, weaponId))
            return true;

        if (!GunBasic_ReadRowBytes(weaponId, b))
            return false;
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return PartsBytesTainted(b);
}

static std::uint8_t* MagazineRowPtr(int ammoId)
{
    if (ammoId <= 0 || ammoId > g_Magazine.maxId || !EnsurePartShadow(g_Magazine))
        return nullptr;
    if (ammoId >= kWideIdBase)
    {
        WidePartState* w = WideStateFor(kVanillaSpace_Magazine, ammoId);
        if (!w || !w->rowSet || w->row.size() != static_cast<size_t>(g_Magazine.stride))
            return nullptr;
        return w->row.data();
    }
    std::uint8_t* buf = PartCurrentBuf(g_Magazine);
    return buf ? buf + static_cast<size_t>(ammoId - 1) * g_Magazine.stride : nullptr;
}

int EquipParam_GetMagazineEquipAmmoId(int ammoId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    std::uint8_t* row = MagazineRowPtr(ammoId);
    std::uint16_t v = 0;
    if (!row || !ReadU16SEH(row, v))
        return 0;
    return static_cast<int>(v);
}

bool EquipParam_WideMagazinesReady()
{
    return WideMagazine_IsActive() && PartIdWiden_IsActive()
        && g_OrigSetUpGunInfo && TppEquip_WideAmmoRootReady();
}

std::uint32_t EquipParam_GetEquipType(std::uint32_t equipId)
{
    return GetEquipTypeForEquipId(equipId);
}

bool EquipParam_IsBulletIdTaken(int bulletId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    EnsurePartPersistLoaded(g_Bullet);
    return bulletId <= g_Bullet.stockCount
        || g_Bullet.persistReserved.count(bulletId) != 0
        || PartIdTakenThisSession(g_Bullet, bulletId);
}

bool EquipParam_GetMagazineParameters(int ammoId, MagazineParameters& parameters)
{
    parameters = {};
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const std::uint8_t* row = MagazineRowPtr(ammoId);
    if (!row || !ReadU16SEH(row, parameters.equipId)
        || !ReadU16SEH(row + 2, parameters.capacity)
        || !ReadU16SEH(row + 4, parameters.carryMax)
        || !ReadU16SEH(row + 6, parameters.bulletId))
        return false;
    parameters.id = ammoId;
    return true;
}

bool EquipParam_GetWeaponMagazines(int equipId, MagazineParameters& primary,
                                   MagazineParameters& underbarrel)
{
    primary = {};
    underbarrel = {};
    if (!EquipParam_WideMagazinesReady())
        return false;
    const int weaponId = TppEquip_GetSubIdForEquipId(equipId);
    int parts[11]{};
    if (weaponId <= 0 || !GunBasic_GetLogicalParts(weaponId, parts))
        return false;
    int secondaryId = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(g_Mutex);
        if (parts[8] > 0)
        {
            const auto it = g_UnderBarrelMagazine.find(parts[8]);
            secondaryId = it != g_UnderBarrelMagazine.end()
                ? it->second : PartRowByte(g_UnderBarrel, parts[8], 1);
        }
    }
    if (parts[2] < kWideIdBase && secondaryId < kWideIdBase)
        return false;
    if (parts[2] > 0 && !EquipParam_GetMagazineParameters(parts[2], primary))
        return false;
    if (secondaryId > 0 && !EquipParam_GetMagazineParameters(secondaryId, underbarrel))
        return false;
    return true;
}

int EquipParam_GetMagazineBulletId(int ammoId)
{
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    std::uint8_t* row = MagazineRowPtr(ammoId);
    if (!row)
        return 0;
    const int b = ReadByteAtSEH(row, 6);
    return b < 0 ? 0 : b;
}

int EquipParam_GetBulletType(int bulletId)
{
    if (bulletId <= 0)
        return -1;
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    const int rows = g_Bullet.active ? g_Bullet.maxId : g_Bullet.stockCount;
    if (bulletId > rows)
        return -1;
    std::uint8_t* buf = PartCurrentBuf(g_Bullet);
    if (!buf)
        return -1;
    return ReadByteAtSEH(buf + static_cast<size_t>(bulletId - 1) * 14, 0x0A); // bullet row byte 0x0A is the bullet type
}

bool EquipParam_IsSuppressorMuzzleOption(int muzzleOptionId)
{
    if (muzzleOptionId <= 0)
        return false;
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (muzzleOptionId >= kWideIdBase)
    {
        const WidePartState* w = WideStateFor(kVanillaSpace_MuzzleOption, muzzleOptionId);
        return w && w->rowSet && w->row.size() >= 3 && (w->row[2] & 1) != 0;
    }
    const int rows = g_Muzzle.active ? g_Muzzle.maxId : g_Muzzle.stockCount;
    if (muzzleOptionId > rows)
        return false;
    std::uint8_t* buf = PartCurrentBuf(g_Muzzle);
    if (!buf)
        return false;
    const int b = ReadByteAtSEH(buf + static_cast<size_t>(muzzleOptionId - 1) * 3, 2); // muzzle option row: grouping, durability, suppressor
    return b > 0 && (b & 1) != 0;
}

int EquipParam_FindVanillaMagazineByBullet(int bulletId, int excludeAmmoId)
{
    if (bulletId <= 0)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    if (!EnsurePartShadow(g_Magazine))
        return 0;
    const int last = g_Magazine.stockCount > 0 ? g_Magazine.stockCount : g_Magazine.maxId;
    for (int id = 1; id <= last; ++id)
    {
        if (id == excludeAmmoId)
            continue;
        std::uint8_t* row = MagazineRowPtr(id);
        if (!row)
            continue;
        if (ReadByteAtSEH(row, 6) == bulletId)
            return id;
    }
    return 0;
}
