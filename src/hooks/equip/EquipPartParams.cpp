#include "pch.h"
#include "EquipPartParams.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "EquipIdCompression.h"
#include "FoxHashes.h"
#include "GunBasicInject.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaApi.h"
#include "BulletLockOn.h"
#include "BulletMultiShot.h"
#include "TppEquip_ReloadEquipIdTable.h"
#include "TppEquipConstRegistry.h"
#include "../../core/V_FrameWorkState.h"

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
    };

    static PartBuffer g_Magazine = { "magazine", 0x20, 8, 191, 255, {}, false, 192, {} };
    static PartBuffer g_Stock    = { "stock",    0x40, 2, 42,  255, {}, false, 43,  {} };
    static PartBuffer g_Muzzle   = { "muzzleOption", 0x28, 3, 39, 255, {}, false, 40, {} };
    static PartBuffer g_Sight    = { "sight",    0x38, 5, 24,  255, {}, false, 25,  {} };
    static PartBuffer g_Barrel   = { "barrel",   0x18, 2, 114, 255, {}, false, 115, {} };
    static PartBuffer g_UnderBarrel = { "underBarrel", 0x48, 3, 22, 255, {}, false, 23, {} };
    static PartBuffer g_Option   = { "option",   0x30, 1, 9,  255, {}, false, 10,  {} };
    static PartBuffer g_Bullet   = { "bullet",   0x50, 14, 112, 255, {}, false, 113, {} };

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

    static bool EnsurePartShadow(PartBuffer& pb)
    {
        if (pb.active)
            return true;

        void** loc = PartPtrLoc(pb);
        if (!loc)
            return false;

        if (pb.shadow.empty())
            pb.shadow.assign(static_cast<size_t>(pb.maxId) * pb.stride, 0);

        if (InitShadowSEH(loc, pb.shadow.data(),
                          static_cast<size_t>(pb.stockCount) * pb.stride) != 1)
        {
            pb.shadow.clear();
            return false;
        }

        pb.active = true;
        Log("[EquipParam] %s shadow active (stock %d -> %d slots; custom ids %d..%d)\n",
            pb.name, pb.stockCount, pb.maxId, pb.stockCount + 1, pb.maxId);
        return true;
    }

    static std::uint8_t* PartCurrentBuf(PartBuffer& pb)
    {
        return ReadPtrSEH(PartPtrLoc(pb));
    }

    static int AllocatePartSlot(PartBuffer& pb, const char* name)
    {
        if (!name || !name[0])
            return 0;

        auto it = pb.nameToId.find(name);
        if (it != pb.nameToId.end())
            return it->second;

        if (!EnsurePartShadow(pb))
            return 0;

        if (pb.nextId > pb.maxId)
        {
            Log("[EquipParam] %s custom id space exhausted (%d..%d) for '%s'\n",
                pb.name, pb.stockCount + 1, pb.maxId, name);
            return 0;
        }

        const int id = pb.nextId++;
        pb.nameToId[name] = id;
        Log("[EquipParam] '%s' -> %s id %d (shadow slot)\n", name, pb.name, id);
        return id;
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
            p[6] = static_cast<std::uint8_t>(bulletId & 0xFF);
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
    static PartBuffer g_RcvIdBuf = { "receiverBuffer", 0x10, 6, 233, 255, {}, false, 0, {} };

    struct RcvPool
    {
        PartBuffer pb;
        int rowByteOffset;
        bool counted;
    };
    static RcvPool g_Base = { { "receiverParamSetsBase",     0x58, 12, 0, 1024, {}, false, 0, {} }, 2, false };
    static RcvPool g_Wob  = { { "receiverParamSetsWobbling", 0x60, 14, 0, 1024, {}, false, 0, {} }, 3, false };
    static RcvPool g_Sys  = { { "receiverParamSetsSystem",   0x68,  3, 0, 1024, {}, false, 0, {} }, 4, false };
    static RcvPool g_Snd  = { { "receiverParamSetsSound",    0x70,  8, 0, 1024, {}, false, 0, {} }, 5, false };

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
    };
    static std::mutex g_FireSoundMutex;
    static std::map<int, FireSoundSpec> g_FireSoundByRow;

    static void RegisterFireSoundOverride(int idx, const std::string& text,
                                          int mSeg, bool isEvent, const char* field)
    {
        const bool needOverride = isEvent || text.size() > 7 || mSeg >= 0;
        if (!needOverride)
            return;
        if (gAddr.WeaponSystem_DefineWeaponFireSound == 0)
        {
            if (isEvent || text.size() > 7)
                Log("[EquipParam] SetReceiver: %s '%s' needs the fire-sound override, "
                    "unavailable on this build - falling back to the 7-char row\n",
                    field, text.c_str());
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_FireSoundMutex);
            g_FireSoundByRow[idx] = FireSoundSpec{ text, mSeg, isEvent };
        }
        if (isEvent)
            Log("[EquipParam] SetReceiver: %s fire-sound event='%s' (played verbatim)\n",
                field, text.c_str());
        else
            Log("[EquipParam] SetReceiver: %s fire-sound override root='%s' _m=%s\n",
                field, text.c_str(),
                mSeg == 1 ? "on" : mSeg == 2 ? "off" : "weapon-default");
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
    // barrelParamSetsBase (impl+0x78, 7 bytes) <- barrel buffer (impl+0x18, stride 2) byte 1
    static RefPool g_BarrelBase = { { "barrelParamSetsBase", 0x78, 7,  0, 1024, {}, false, 0, {} }, 0x18, 2, 114, 1, false };
    // bulletParamSetsBase (impl+0x80, 22 bytes) <- bullet buffer (impl+0x50, stride 14) byte 6
    static RefPool g_BulletBase = { { "bulletParamSetsBase", 0x80, 22, 0, 1024, {}, false, 0, {} }, 0x50, 14, 112, 6, false };

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

    static std::map<std::string, int> g_RcvNameToId;
    static std::set<int> g_RcvClaimed;

    static std::uint8_t* ReceiverTypeTable()
    {
        return static_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.MotionLoaderImpl_ReceiverTypeTable));
    }

    static std::uint8_t g_RcvTypeExt[256];
    static bool g_RcvTypeExtReady = false;

    static int CopyRcvTypeExtSEH(const std::uint8_t* src)
    {
        __try
        {
            for (int i = 0; i < 240; ++i) g_RcvTypeExt[i] = src[i];
            for (int i = 240; i < 256; ++i) g_RcvTypeExt[i] = 0;
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
        if (g_RcvTypeExtReady && receiverId < 256)
            r = g_RcvTypeExt[receiverId];
        else
            r = g_OrigGetReceiverType ? g_OrigGetReceiverType(self, receiverId) : 0;
        return PartCode_TapReceiverType(receiverId, r);
    }

    static bool WriteReceiverType(int receiverId, int type)
    {
        if (receiverId < 0 || receiverId >= 256)
            return false;
        EnsureRcvTypeExt();
        if (!g_RcvTypeExtReady)
            return false;
        g_RcvTypeExt[receiverId] = static_cast<std::uint8_t>(type);
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
                Log("[EquipParam] %s: could not determine vanilla row count\n", rp.pb.name);
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
            Log("[EquipParam] %s pool exhausted (max %d rows)\n", rp.pb.name, rp.pb.maxId);
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
                Log("[EquipParam] %s: could not determine vanilla row count\n", rp.pb.name);
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
            Log("[EquipParam] %s pool exhausted (max %d rows)\n", rp.pb.name, rp.pb.maxId);
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
            Log("[EquipParam] SetReceiver: '%s' must be a number (game index) or a table (custom values)\n", field);
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
                for (int c = 0; c < 7 && c < static_cast<int>(text.size()); ++c)
                    label[c] = text[c];
            }

            g_lua_settop(L, -2);

            const int idx = AllocateRcvPoolRow(rp);
            if (idx < 0) return -2;
            std::uint8_t* pool = PartCurrentBuf(rp.pb);
            if (!pool) return -2;
            WriteSndRowSEH(pool, idx, label);
            RegisterFireSoundOverride(idx, text, mSeg, isEvent, field);
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
        else // POOL_SYS
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

int __cdecl l_SetOption(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetOption: argument #1 must be a table\n");
        return 0;
    }

    int optionId = 0;
    if (!ReadNamedInt(L, 1, "optionId", optionId) || optionId <= 0)
    {
        Log("[EquipParam] SetOption: missing/invalid optionId (declare one via V_TppEquip.DeclareLTLS)\n");
        return 0;
    }

    int isLight = 0, isLaser = 0;
    ReadNamedInt(L, 1, "isLight", isLight);
    ReadNamedInt(L, 1, "isLaser", isLaser);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Option))
    {
        Log("[EquipParam] SetOption: buffer not available for this build - skipped\n");
        return 0;
    }
    if (optionId > g_Option.maxId)
    {
        Log("[EquipParam] SetOption: optionId=%d out of range [1,%d] - not written\n",
            optionId, g_Option.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Option);
    if (!buf)
        return 0;

    const bool vanillaRow = optionId <= g_Option.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(optionId - 1);
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Option, optionId, rowPtr, 1);

    WriteOptionSEH(buf, optionId, isLight, isLaser);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Option, optionId, rowPtr, 1);

#ifdef _DEBUG
    Log("[EquipParam] SetOption optionId=%d isLight=%d isLaser=%d -> native slot\n",
        optionId, isLight, isLaser);
#endif
    return 0;
}

int __cdecl l_SetUnderBarrel(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetUnderBarrel: argument #1 must be a table\n");
        return 0;
    }

    int underBarrelId = 0;
    if (!ReadNamedInt(L, 1, "underBarrelId", underBarrelId) || underBarrelId <= 0)
    {
        Log("[EquipParam] SetUnderBarrel: missing/invalid underBarrelId (declare one via V_TppEquip.DeclareUBs)\n");
        return 0;
    }

    int receiverId = 0, magazineId = 0, underBarrelGrade = 0;
    const bool hasReceiver = ReadNamedInt(L, 1, "receiverId", receiverId);
    ReadNamedInt(L, 1, "magazineId", magazineId);
    ReadNamedInt(L, 1, "underBarrelGrade", underBarrelGrade);

    if (!hasReceiver || receiverId <= 0)
    {
        Log("[EquipParam] SetUnderBarrel underBarrelId=%d: requires a receiverId (the underbarrel is a "
            "sub-weapon - its receiverId picks its firing behavior from the receiver buffer, magazineId "
            "its ammo). Row rejected.\n", underBarrelId);
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_UnderBarrel))
    {
        Log("[EquipParam] SetUnderBarrel: buffer not available for this build - skipped\n");
        return 0;
    }
    if (underBarrelId > g_UnderBarrel.maxId)
    {
        Log("[EquipParam] SetUnderBarrel: underBarrelId=%d out of range [1,%d] - not written\n",
            underBarrelId, g_UnderBarrel.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_UnderBarrel);
    if (!buf)
        return 0;

    const bool vanillaRow = underBarrelId <= g_UnderBarrel.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(underBarrelId - 1) * 3;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_UnderBarrel, underBarrelId, rowPtr, 3);

    WriteUnderBarrelSEH(buf, underBarrelId, receiverId, magazineId, underBarrelGrade);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_UnderBarrel, underBarrelId, rowPtr, 3);

#ifdef _DEBUG
    Log("[EquipParam] SetUnderBarrel underBarrelId=%d receiver=%d magazine=%d grade=%d -> native slot\n",
        underBarrelId, receiverId, magazineId, underBarrelGrade);
#endif
    return 0;
}

int __cdecl l_SetBarrel(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetBarrel: argument #1 must be a table\n");
        return 0;
    }

    int barrelId = 0;
    if (!ReadNamedInt(L, 1, "barrelId", barrelId) || barrelId <= 0)
    {
        Log("[EquipParam] SetBarrel: missing/invalid barrelId (declare one via V_TppEquip.DeclareBAs)\n");
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
    {
        Log("[EquipParam] SetBarrel barrelId=%d: base=%d out of range [0,255] - the barrel "
            "ballistics/range index (barrelParamSetsBase curve) is a full byte. Masked.\n",
            barrelId, base);
    }
    if (barrelLength < 0 || barrelLength > 15)
    {
        Log("[EquipParam] SetBarrel barrelId=%d: barrelLength=%d out of range [0,15] - "
            "BarrelLengthType is a 4-bit field. Masked.\n", barrelId, barrelLength);
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Barrel))
    {
        Log("[EquipParam] SetBarrel: buffer not available for this build - skipped\n");
        return 0;
    }
    if (barrelId > g_Barrel.maxId)
    {
        Log("[EquipParam] SetBarrel: barrelId=%d out of range [1,%d] - not written\n",
            barrelId, g_Barrel.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Barrel);
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
                Log("[EquipParam] SetBarrel barrelId=%d: barrelParamSetsBase[%d] "
                    "invalid value - zeroed\n", barrelId, k + 1);
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
                Log("[EquipParam] SetBarrel barrelId=%d: barrelParamSetsBase[%d] "
                    "invalid value - zeroed\n", barrelId, k + 1);
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
            Log("[EquipParam] SetBarrel barrelId=%d: unk2 clamped to 2.55 - no "
                "known engine consumer to post-scale\n", barrelId);
        if (baseCurve[6] > 2.555)
            Log("[EquipParam] SetBarrel barrelId=%d: percentOverride clamped to "
                "2.55 - GunInfo+0x83 is a byte\n", barrelId);

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
                    Log("[EquipParam] SetBarrel barrelId=%d: curve row %d is past "
                        "the 254-slot direct window - served through the overflow "
                        "swap at gun setup\n", barrelId, idx);
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
                    Log("[EquipParam] SetBarrel barrelId=%d: multiplier(s) above "
                        "the 2.55x byte ceiling - remainder applied at gun setup "
                        "(fireRate x%.2f aim x%.2f range x%.2f rangeUI x%.2f "
                        "spreadMax x%.2f)\n", barrelId,
                        ex.rem[0], ex.rem[2], ex.rem[3], ex.rem[4], ex.rem[5]);
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
            Log("[EquipParam] SetBarrel barrelId=%d: could not append custom "
                "barrelParamSetsBase curve - any >2.55x request dropped\n",
                barrelId);
        }
    }
    else
    {
        g_BarrelOverflow.erase(barrelId);
        g_BarrelExtraMult.erase(barrelId);
    }

    const bool vanillaRow = barrelId <= g_Barrel.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(barrelId - 1) * 2;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Barrel, barrelId, rowPtr, 2);

    WriteBarrelSEH(buf, barrelId, base, barrelLength, scopeMount, unkFlag, sideMount, underMount);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Barrel, barrelId, rowPtr, 2);

#ifdef _DEBUG
    Log("[EquipParam] SetBarrel barrelId=%d base=%d scope=%d side=%d under=%d len=%d -> native slot\n",
        barrelId, base, scopeMount, sideMount, underMount, barrelLength);
#endif
    return 0;
}

int __cdecl l_SetMagazine(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetMagazine: argument #1 must be a table\n");
        return 0;
    }

    int ammoId = 0;
    if (!ReadNamedInt(L, 1, "ammoId", ammoId) || ammoId <= 0)
    {
        Log("[EquipParam] SetMagazine: missing/invalid ammoId\n");
        return 0;
    }

    int eqpAmmoId = 0;
    int capacity = 0;
    int totalCarry = 0;
    int bulletId = 0;
    ReadNamedInt(L, 1, "equipAmmoId", eqpAmmoId);
    ReadNamedInt(L, 1, "capacity", capacity);
    ReadNamedInt(L, 1, "totalCarry", totalCarry);
    ReadNamedInt(L, 1, "bulletId", bulletId);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Magazine))
    {
        Log("[EquipParam] SetMagazine: buffer not available for this build - skipped\n");
        return 0;
    }
    if (ammoId > g_Magazine.maxId)
    {
        Log("[EquipParam] SetMagazine: ammoId=%d out of range [1,%d] - not written\n",
            ammoId, g_Magazine.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Magazine);
    if (!buf)
        return 0;

    const bool vanillaRow = ammoId <= g_Magazine.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(ammoId - 1) * 8;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Magazine, ammoId, rowPtr, 8);

    WriteMagazineSEH(buf, ammoId, eqpAmmoId, capacity, totalCarry, bulletId);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Magazine, ammoId, rowPtr, 8);

#ifdef _DEBUG
    Log("[EquipParam] SetMagazine ammoId=%d eqpAmmo=%d cap=%d total=%d bullet=%d -> native slot\n",
        ammoId, eqpAmmoId, capacity, totalCarry, bulletId);
#endif
    return 0;
}

int __cdecl l_SetBullet(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetBullet: argument #1 must be a table\n");
        return 0;
    }

    int bulletId = 0;
    if (!ReadNamedInt(L, 1, "bulletId", bulletId) || bulletId <= 0)
    {
        Log("[EquipParam] SetBullet: missing/invalid bulletId (declare one via V_TppEquip.DeclareBLs)\n");
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
                Log("[EquipParam] SetBullet: bulletTrailEffect path '%s' not registered "
                    "(trail list unavailable or index>255)\n", trailPath.c_str());
        }
    }
    ReadNamedInt(L, 1, "unk09", u8v[3]);
    ReadNamedInt(L, 1, "bulletType",   u8v[4]);
    ReadNamedInt(L, 1, "ricochetSize", u8v[5]);
    ReadNamedInt(L, 1, "blastId", u8v[6]);
    ReadNamedIntAlias(L, 1, "isLethal", "unk11", unk11);
    ReadNamedInt(L, 1, "eqpType", eqpType);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Bullet))
    {
        Log("[EquipParam] SetBullet: buffer not available for this build - skipped\n");
        return 0;
    }
    if (bulletId > g_Bullet.maxId)
    {
        Log("[EquipParam] SetBullet: bulletId=%d out of range [1,%d] - not written\n",
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
                    Log("[EquipParam] SetBullet bulletId=%d: falloff row %d is past "
                        "the 254-slot direct window - served through the overflow "
                        "swap at fire time\n", bulletId, idx);
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
            Log("[EquipParam] SetBullet bulletId=%d: could not append custom bulletParamSetsBase curve\n",
                bulletId);
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
                    Log("[EquipParam] SetBullet bulletId=%d: NPC falloff row %d is "
                        "past the direct window and NPC-fired shots cannot use the "
                        "overflow swap - NPC shots fall back to curve 0\n",
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
            Log("[EquipParam] SetBullet bulletId=%d: could not append custom npcBulletParamSetsBase curve\n",
                bulletId);
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

#ifdef _DEBUG
    Log("[EquipParam] SetBullet bulletId=%d ricochet=%d type=%d eqpType=%d -> native slot\n",
        bulletId, u8v[5], u8v[4], eqpType);
#endif
    return 0;
}

int __cdecl l_SetStock(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetStock: argument #1 must be a table\n");
        return 0;
    }

    int stockId = 0;
    if (!ReadNamedInt(L, 1, "stockId", stockId) || stockId <= 0)
    {
        Log("[EquipParam] SetStock: missing/invalid stockId\n");
        return 0;
    }

    double spreadRecovery = 1.0;
    double movementSway = 1.0;
    ReadNamedFloat(L, 1, "spreadRecovery", spreadRecovery);
    ReadNamedFloat(L, 1, "movementSway", movementSway);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Stock))
    {
        Log("[EquipParam] SetStock: buffer not available for this build - skipped\n");
        return 0;
    }
    if (stockId > g_Stock.maxId)
    {
        Log("[EquipParam] SetStock: stockId=%d out of range [1,%d] - not written\n",
            stockId, g_Stock.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Stock);
    if (!buf)
        return 0;

    const bool vanillaRow = stockId <= g_Stock.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(stockId - 1) * 2;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Stock, stockId, rowPtr, 2);

    WriteStockSEH(buf, stockId, ScaleByte(spreadRecovery, 100.0), ScaleByte(movementSway, 100.0));

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Stock, stockId, rowPtr, 2);

#ifdef _DEBUG
    Log("[EquipParam] SetStock stockId=%d spreadRecovery=%.2f movementSway=%.2f -> native slot\n",
        stockId, spreadRecovery, movementSway);
#endif
    return 0;
}

int __cdecl l_SetMuzzle(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetMuzzle: argument #1 must be a table\n");
        return 0;
    }

    int muzzleOptionId = 0;
    if (!ReadNamedInt(L, 1, "muzzleOptionId", muzzleOptionId) || muzzleOptionId <= 0)
    {
        Log("[EquipParam] SetMuzzle: missing/invalid muzzleOptionId\n");
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
        Log("[EquipParam] SetMuzzle: buffer not available for this build - skipped\n");
        return 0;
    }
    if (muzzleOptionId > g_Muzzle.maxId)
    {
        Log("[EquipParam] SetMuzzle: muzzleOptionId=%d out of range [1,%d] - not written\n",
            muzzleOptionId, g_Muzzle.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Muzzle);
    if (!buf)
        return 0;

    const bool vanillaRow = muzzleOptionId <= g_Muzzle.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(muzzleOptionId - 1) * 3;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_MuzzleOption, muzzleOptionId, rowPtr, 3);

    WriteMuzzleSEH(buf, muzzleOptionId, ScaleByte(grouping, 100.0), durability, suppressor);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_MuzzleOption, muzzleOptionId, rowPtr, 3);

#ifdef _DEBUG
    Log("[EquipParam] SetMuzzle muzzleOptionId=%d grouping=%.2f durability=%d suppressor=%d -> native slot\n",
        muzzleOptionId, grouping, durability, suppressor);
#endif
    return 0;
}

int __cdecl l_SetSight(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetSight: argument #1 must be a table\n");
        return 0;
    }

    int scopeId = 0;
    if (!ReadNamedInt(L, 1, "scopeId", scopeId) || scopeId <= 0)
    {
        Log("[EquipParam] SetSight: missing/invalid scopeId (declare one via V_TppEquip.DeclareSTs)\n");
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

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    if (!EnsurePartShadow(g_Sight))
    {
        Log("[EquipParam] SetSight: buffer not available for this build - skipped\n");
        return 0;
    }
    if (scopeId > g_Sight.maxId)
    {
        Log("[EquipParam] SetSight: scopeId=%d out of range [1,%d] - not written\n",
            scopeId, g_Sight.maxId);
        return 0;
    }

    std::uint8_t* buf = PartCurrentBuf(g_Sight);
    if (!buf)
        return 0;

    const int flags = (booster ? 0x01 : 0) | (nvg ? 0x02 : 0) | (builtIn ? 0x04 : 0)
                    | (rangeFinder ? 0x08 : 0) | (rangeFinderBulletDrop ? 0x10 : 0);
    const bool vanillaRow = scopeId <= g_Sight.stockCount;
    const std::uint8_t* rowPtr = buf + static_cast<size_t>(scopeId - 1) * 5;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Sight, scopeId, rowPtr, 5);

    WriteSightSEH(buf, scopeId, Clamp(zoom1, 0, 255), Clamp(zoom2, 0, 255),
                  Clamp(zoom3, 0, 255), Clamp(scopeUiId, 0, 255), flags);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Sight, scopeId, rowPtr, 5);

#ifdef _DEBUG
    Log("[EquipParam] SetSight scopeId=%d zoom=%d/%d/%d ui=%d flags=0x%02X -> native slot\n",
        scopeId, zoom1, zoom2, zoom3, scopeUiId, flags);
#endif
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

    const bool grown = EnsurePartShadow(g_RcvIdBuf);
    std::uint8_t* rbuf = grown ? PartCurrentBuf(g_RcvIdBuf) : ImplBufPtr(kReceiverImplOffset);
    if (!rbuf)
        return 0;

    const int cap = grown ? kReceiverMaxId : kReceiverCapacity;
    for (int idx0 = cap - 1; idx0 >= 1; --idx0)
    {
        const int receiverId = idx0 + 1;
        if (g_RcvClaimed.count(receiverId))
            continue;
        if (RcvRowIsZeroSEH(rbuf, idx0) != 1)
            continue;

        g_RcvClaimed.insert(receiverId);
        g_RcvNameToId[name] = receiverId;
        Log("[EquipParam] '%s' -> receiverId %d (%s slot)\n",
            name, receiverId, (receiverId > kReceiverCapacity) ? "grown" : "free native");
        return receiverId;
    }

    Log("[EquipParam] no free receiver slot for '%s' (all %d used) - custom receiver "
        "unavailable\n", name, kReceiverMaxId);
    return 0;
}

int __cdecl l_SetReceiver(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetReceiver: argument #1 must be a table\n");
        return 0;
    }

    int receiverId = 0;
    if (!ReadNamedInt(L, 1, "receiverId", receiverId) || receiverId <= 0)
    {
        Log("[EquipParam] SetReceiver: missing/invalid receiverId (declare one via V_TppEquip.DeclareRCs)\n");
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    std::uint8_t* rbuf = ImplBufPtr(kReceiverImplOffset);
    if (!rbuf)
    {
        Log("[EquipParam] SetReceiver: receiver buffer not available for this build - skipped\n");
        return 0;
    }
    if (receiverId > kReceiverMaxId)
    {
        Log("[EquipParam] SetReceiver: receiverId=%d out of range [1,%d] - not written\n",
            receiverId, kReceiverMaxId);
        return 0;
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
        Log("[EquipParam] SetReceiver receiverId=%d: requires receiverParamSetsBase + "
            "receiverParamSetsWobbling + receiverParamSetsSystem + receiverParamSetsSound; "
            "each is a game index (number) or a {custom values} table\n", receiverId);
        return 0;
    }
    if (!hasAttack)
    {
        attackId = 0;
        Log("[EquipParam] SetReceiver receiverId=%d: no valid attackId (nil?) - writing receiver "
            "with attackId=0 so the weapon still aims/fires (but deals ATK_Push/0 damage). Set "
            "attackId to a vanilla TppDamage.ATK_ value for real damage.\n", receiverId);
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
            Log("[EquipParam] SetReceiver receiverId=%d: pool row(s) past the "
                "254-slot direct window (base=%d wob=%d sys=%d snd=%d) - served "
                "through the overflow swap at gun setup\n",
                receiverId, base, wob, sys, snd);
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

    const bool vanillaRow = receiverId <= kReceiverCapacity
        && !g_RcvClaimed.count(receiverId);
    const std::uint8_t* rowPtr =
        rbuf + static_cast<size_t>(receiverId - 1) * kReceiverStride;
    if (vanillaRow)
        EquipParam_VanillaPreWrite(kVanillaSpace_Receiver, receiverId, rowPtr,
                                   kReceiverStride);

    WriteReceiverRowSEH(rbuf, receiverId, attackId, base, wob, sys, snd);

    if (vanillaRow)
        EquipParam_VanillaPostWrite(kVanillaSpace_Receiver, receiverId, rowPtr,
                                    kReceiverStride);

    int motionType = -1;
    if (hasMotionFrom && motionFrom > 0 && motionFrom < 256)
    {
        EnsureRcvTypeExt();
        if (g_RcvTypeExtReady)
        {
            motionType = g_RcvTypeExt[motionFrom];
            if (motionType == 0)
                Log("[EquipParam] SetReceiver receiverId=%d: WARNING motionFrom=%d has "
                    "motion type 0 (not a holdable gun's receiver) - without a valid type "
                    "the engine invalidates the weapon on draw; pick the receiver of a "
                    "real vanilla gun\n", receiverId, motionFrom);
        }
    }

    if (motionType < 0)
    {
        motionType = 0;
        Log("[EquipParam] SetReceiver receiverId=%d: no motionFrom - animation defaulted to "
            "type 0; set motionFrom=<vanilla RC> for proper animations\n", receiverId);
    }
    EnsureRcvTypeExt();
    const int prevType = (g_RcvTypeExtReady && receiverId < 256)
        ? g_RcvTypeExt[receiverId] : -1;
    if (!WriteReceiverType(receiverId, motionType))
    {
        Log("[EquipParam] SetReceiver receiverId=%d: motion type write failed (table unavailable)\n",
            receiverId);
    }
    else if (vanillaRow && prevType >= 0 && motionType != prevType)
    {
        EquipParam_VanillaForceTaint(kVanillaSpace_Receiver, receiverId,
                                     "a different motion/animation type");
    }

#ifdef _DEBUG
    Log("[EquipParam] SetReceiver receiverId=%d attackId=%d base=%d wob=%d sys=%d snd=%d "
        "motionType=%d -> native\n",
        receiverId, attackId, base, wob, sys, snd, motionType);
#endif
    return 0;
}

bool Install_MotionLoader_ReceiverTypeHook()
{
    EnsureRcvTypeExt();

    void* target = ResolveGameAddress(gAddr.MotionLoaderImpl_GetReceiverType);
    if (!target)
    {
        Log("[EquipParam] GetReceiverType address not set for this build - extended receiver-type table skipped\n");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetReceiverType,
        reinterpret_cast<void**>(&g_OrigGetReceiverType));
    if (!ok)
        Log("[EquipParam] GetReceiverType hook Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[EquipParam] GetReceiverType hook Install -> OK (target=%p, extended type table ready=%d)\n",
            target, g_RcvTypeExtReady ? 1 : 0);
#endif
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

    using GetAttackId_t = unsigned short(__fastcall*)(void* self, int equipId);
    static GetAttackId_t g_OrigGetAttackId = nullptr;

    static int SehAvOnly(unsigned long code)
    {
        return (code == EXCEPTION_ACCESS_VIOLATION)
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    static unsigned short __fastcall hkGetAttackIdByEquipId(void* self, int equipId)
    {
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
                Log("[EquipParam] GetAttackIdByEquipId guarded a crash: equipId=%d "
                    "resolves to an empty gunBasic row (module missing SetGunBasic "
                    "for that weapon?) - returning attackId 0\n", equipId);
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
                Log("[EquipParam] SetUpGunInfoFromGunPartsDesc guarded a crash: "
                    "equipId=%u has an empty gunBasic row (module missing "
                    "SetGunBasic for that weapon?) - GunInfo left at defaults\n",
                    equipId);
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
        const int stock = static_cast<int>(gAddr.GunBasicParameters2SlotCount);
        const int hi = (stock > 0 && stock <= 1022) ? stock : 514;
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
        if (ReadByteAtSEH(desc, 0) != 0)   // has a receiver -> real weapon, leave untouched
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

    static void __fastcall hkSetUpGunInfo(void* self, void* desc,
                                          unsigned int equipId, void* gunInfo,
                                          void* a5, void* a6, void* a7,
                                          void* a8, void* a9)
    {
        std::uint8_t descSaved[12], descMask[12];
        const int substituted = SubstituteEmptyDesc(desc, descSaved, descMask);
        if (substituted)
        {
            static unsigned long long lastMs = 0;
            const unsigned long long now = GetTickCount64();
            if (now - lastMs > 2000)
            {
                lastMs = now;
                Log("[EquipParam] equipId=%u has no gunBasic row - substituted a safe "
                    "vanilla weapon for setup so it renders as a harmless dummy instead "
                    "of crashing. Fix: give this weapon a valid SetGunBasic.\n", equipId);
            }
        }

        PoolSwapState st;
        PrepareGunInfoSwap(desc, st);
        const int ok = CallOrigGunInfoSEH(self, desc, equipId, gunInfo,
                                          a5, a6, a7, a8, a9);
        RestoreGunInfoSwap(st);

        if (substituted)
            RestoreSubstitutedDesc(desc, descSaved, descMask);

        if (ok == 1 && gunInfo && !g_BarrelExtraMult.empty())
        {
            const int ba = ReadByteAtSEH(desc, 1);
            if (ba > 0)
            {
                const auto it = g_BarrelExtraMult.find(ba);
                if (it != g_BarrelExtraMult.end()
                    && ApplyBarrelExtraSEH(gunInfo, it->second) != 1)
                    Log("[EquipParam] barrel extra multiplier: GunInfo write "
                        "faulted for barrelId=%d - engine base values kept\n", ba);
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

    //   vtbl+0x20 -> family block code ; vtbl+0x40 -> validity ; vtbl+0x150 -> template.
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
    static std::set<std::uint32_t> g_PartLogged;

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

    static std::mutex g_WeaponKeyMutex;
    static std::set<std::uint32_t> g_WeaponKeysLogged;

    static std::map<std::uint32_t, std::uint32_t> g_FamilyFrom;
    static std::atomic<bool> g_HasSwaps{ false };
    static std::set<std::uint32_t> g_FamilyLogged;
    static std::map<std::uint32_t, void*> g_TemplateByEquipId;
    static std::set<std::uint32_t> g_WeaponInfoDumped;

    static bool IsFamilyMappedEither(std::uint32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        if (g_FamilyFrom.count(equipId))
            return true;
        for (const auto& kv : g_FamilyFrom)
            if (kv.second == equipId)
                return true;
        return false;
    }

    static uintptr_t SetupWeaponInfoAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x14105fdb0ull;
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
        const auto it = g_FamilyFrom.find(equipId);
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

    static bool GetGbForEquipId(std::uint32_t equipId, FamilyGb& out)
    {
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
        const std::int32_t idx =
            EquipIdCompression::ComputeCompressed(static_cast<std::int32_t>(equipId));
        if (EquipIdCompression::IsCompressedInBounds(idx))
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

    static std::atomic<std::uint32_t> g_PartCallCount{ 0 };

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

 
    using ExecStateChange_t = void(__fastcall*)(void*, void*, unsigned int, int);
    static ExecStateChange_t g_OrigExecStateChange = nullptr;
    static void* g_ExecStateChangeAddr = nullptr;
    static int ReadU32AtSEH(const void* base, size_t off, std::uint32_t& out);

    static uintptr_t AttackFsmStateChangeAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141040a00ull;
        default:                                          return 0;
        }
    }

    static const char* AtkStateName(int s)
    {
        static const char* const kNames[] = {
            "None", "BareHoldStart", "BareHold", "BareHoldEnd", "CrawlBareHoldMove",
            "Void5", "GunHoldStart", "GunHold", "GunHoldEnd", "GunFire", "GunFire2",
            "GunReload", "GunReload2", "GunCock", "CrawlGunHoldMove", "DisableGunHold",
            "GunHoldChangeDirection", "SnatchWeapon", "ShieldHoldStart", "ShieldHold",
            "ShieldHoldEnd", "GunHoldToShieldHold", "ShieldHoldToGunHold",
            "SpecialGunHold", "SpecialGunFire", "SpecialGunFire2", "SpecialGunAction",
            "SpecialMissionPrepare"
        };
        if (s >= 0 && s < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])))
            return kNames[s];
        return "?";
    }

    static void __fastcall hkExecStateChange(void* work, void* impl,
                                             unsigned int player, int state)
    {
        std::uint32_t eq = 0, prevWord = 0;
        const int eqOk = ReadU32AtSEH(work, 0x24c, eq);
        const int prevOk = ReadU32AtSEH(work, 0x2c0, prevWord);
        if (eqOk && prevOk)
        {
            const int prev = static_cast<int>(prevWord & 0xFF);
            static std::atomic<std::uint64_t> lastKey{ 0xFFFFFFFFFFFFFFFFull };
            const std::uint64_t key =
                (static_cast<std::uint64_t>(eq) << 16) | (static_cast<std::uint64_t>(prev & 0xFF) << 8)
                | static_cast<std::uint64_t>(state & 0xFF);
            if (lastKey.exchange(key, std::memory_order_relaxed) != key)
                Log("[WeaponKey] FSM eq=%u %s(%d) -> %s(%d)\n",
                    eq, AtkStateName(prev), prev, AtkStateName(state), state);
        }
        g_OrigExecStateChange(work, impl, player, state);
    }

    using SearchMotionData_t = void*(__fastcall*)(void*, unsigned long long, void*);
    static SearchMotionData_t g_OrigSearchMotionData = nullptr;
    static void* g_SearchMotionDataAddr = nullptr;

    static uintptr_t AnimSearchMotionDataAddr()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4: return 0x141a94710ull;
        default:                                          return 0;
        }
    }

    static void* __fastcall hkSearchMotionData(void* self, unsigned long long hash,
                                               void* pkg)
    {
        void* r = g_OrigSearchMotionData(self, hash, pkg);
        if (!r)
        {
            static std::mutex mx;
            static std::set<unsigned long long> seen;
            bool isNew = false;
            {
                std::lock_guard<std::mutex> lock(mx);
                if (seen.size() < 64)
                    isNew = seen.insert(hash).second;
            }
            if (isNew)
                Log("[WeaponKey] motion clip MISS: hash=%016llX not found in any "
                    "loaded mtar\n", hash);
        }
        return r;
    }

    static std::set<std::uint32_t> g_KeyTypeLogged;

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
        if (SwapKeyWordsSEH(self, slot, fromIds, toTypes, m, &st) > 0)
        {
            bool isNew;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                isNew = g_KeyTypeLogged.insert(fromIds[0] ^ (slot << 16)).second;
            }
#ifdef _DEBUG
            if (isNew)
                Log("[WeaponKey] key eqpType swapped to donor family for %d word(s) "
                    "slot=%d (menu type untouched)\n", st.n, slot);
#else
            (void)isNew;
#endif
        }
    }

    static std::uint32_t PartCodeCommon(int slot, void* self, std::uint32_t partId)
    {
        g_PartCallCount.fetch_add(1, std::memory_order_relaxed);
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
                    Log("[WeaponKey] WARNING: donor equipId=%u gunBasic unresolved at "
                        "draw time - part substitution skipped (slot %d); the weapon "
                        "may not animate correctly\n", g_TlsVanillaEquip, slot);
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
#ifdef _DEBUG
                            static std::atomic<int> logged{ 0 };
                            const int bit = 1 << slot;
                            if (!(logged.fetch_or(bit, std::memory_order_relaxed) & bit))
                                Log("[WeaponKey] donor %s (part %u) suppressed: "
                                    "custom weapon has none - hold would abort otherwise\n",
                                    slot == 3 ? "stock" : "underbarrel",
                                    van.gb[kPartGbByte[slot]]);
#endif
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
                    Log("[WeaponKey] WARNING: receiver part code came out %u (0x%X) for "
                        "equipId=%u (rc %u->%u) - the engine will invalidate this "
                        "weapon; pick a donor whose receiver has a real motion type\n",
                        code, code, g_TlsCustomEquip, partId, use);
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

    static void DumpDescriptorSEH(void* self, std::uint32_t key)
    {
        __try
        {
            std::uint8_t* a = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + 0x120);
            if (!a) return;
            std::uint8_t* table = *reinterpret_cast<std::uint8_t**>(a + 0x28);
            if (!table) return;
            const unsigned hw = key >> 16;
            std::uint8_t* d = table + static_cast<size_t>(hw) * 0xe;
            Log("[WeaponKey] desc key=0x%X hw=%u @%p: %02X %02X %02X %02X %02X %02X %02X "
                "%02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X (block=%u tmplIdx=%u)\n",
                key, hw, d,
                d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
                d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15], d[16], d[17],
                static_cast<unsigned>(*reinterpret_cast<std::uint16_t*>(d)), d[8]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static std::uint32_t __fastcall hkResolver20(void* self, std::uint32_t key)
    {
        const std::uint32_t equipId = g_OrigResolver20(self, key);
        std::uint32_t vanilla = equipId;
        if (g_InSetupWeaponInfo)
        {
            vanilla = MapEquipId(equipId);
            if (vanilla != equipId)
            {
                g_TlsCustomEquip = equipId;
                g_TlsVanillaEquip = vanilla;
            }
        }
#ifdef _DEBUG
        if (g_InSetupWeaponInfo && key != 0)
        {
            bool isNew;
            {
                std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                isNew = g_WeaponKeysLogged.insert(key).second;
            }
            if (isNew)
            {
                const unsigned eqpType = key & 0x1f;
                if (vanilla != equipId)
                    Log("[WeaponKey] KEY=%u (0x%X) eqpType=%u -> equipId=%u  FAMILY-FROM "
                        "equipId=%u (template+part codes only; identity stays %u)\n",
                        key, key, eqpType, equipId, vanilla, equipId);
                else
                    Log("[WeaponKey] KEY=%u (0x%X) eqpType=%u -> equipId=%u\n",
                        key, key, eqpType, equipId);
                DumpDescriptorSEH(self, key);
            }
        }
#endif
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
                std::uint16_t tag = 0;
                std::uint8_t* op = static_cast<std::uint8_t*>(own);
                ReadU16SEH(op + 0x5e, tag);
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
                    Log("[WeaponKey] WARNING: template family patch failed for "
                        "equipId=%u (eqpType %u -> %u) - the weapon may handle as "
                        "the wrong family\n",
                        g_TlsCustomEquip, cur & 0x1f, vanType);
#ifdef _DEBUG
                if (isNew && !failed)
                    Log("[WeaponKey] template equipId=%u familyFrom=%u: own block @%p "
                        "tag=0x%X eqpType %u -> %u%s\n",
                        g_TlsCustomEquip, vanilla, own, tag, cur & 0x1f, vanType,
                        patched ? " (patched)" : " (already matches)");
#endif
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

    static int CopyBytesSEH(const void* src, std::uint8_t* dst, size_t n)
    {
        __try
        {
            std::memcpy(dst, src, n);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void DumpWeaponInfo(void* work, std::uint32_t key, std::uint32_t equipIdFromKey)
    {
        std::uint8_t w[0xF0];
        if (CopyBytesSEH(static_cast<std::uint8_t*>(work) + 0x1b0, w, sizeof(w)) != 1)
            return;
        char line[176];
        for (int row = 0; row < 5; ++row)
        {
            int pos = 0;
            for (int i = 0; i < 0x30; ++i)
                pos += std::snprintf(line + pos, sizeof(line) - pos, "%02X ",
                                     w[row * 0x30 + i]);
            Log("[WeaponKey] winfo eq=%u key=0x%X +0x%02X: %s\n",
                equipIdFromKey, key, row * 0x30, line);
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
            Log("[WeaponKey] could not resolve family-resolver methods - key log/swap off\n");
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
            Log("[WeaponKey] WARNING: resolver hooks incomplete: block(+0x20)@%p=%s "
                "validity(+0x40)@%p=%s template(+0x150)@%p=%s\n",
                m20, ok20 ? "OK" : "FAIL", m40, ok40 ? "OK" : "FAIL",
                m150, ok150 ? "OK" : "FAIL");
#ifdef _DEBUG
        else
            Log("[WeaponKey] resolver hooks: block(+0x20)@%p=OK validity(+0x40)@%p=OK "
                "template(+0x150)@%p=OK\n", m20, m40, m150);
#endif

        void* rA = ReadPtrAtSEH(self, 0x60);
        void* rB = rA ? ReadPtrAtSEH(rA, 0x18) : nullptr;

#ifdef _DEBUG
        void* pool = rA ? ReadPtrAtSEH(rA, 0x1d8) : nullptr;
        if (pool)
        {
            char tag[176];
            for (int row = 0; row < 4; ++row)
            {
                int pos = 0;
                for (int i = 0; i < 12; ++i)
                {
                    const int e = row * 12 + i;
                    std::uint16_t v5e = 0xFFFF;
                    std::uint8_t* ep = static_cast<std::uint8_t*>(pool)
                        + static_cast<size_t>(e) * 0x90;
                    ReadU16SEH(ep + 0x5e, v5e);
                    std::uint16_t v78 = 0xFF;
                    ReadU16SEH(ep + 0x78, v78);
                    pos += std::snprintf(tag + pos, sizeof(tag) - pos, "%d:%X/%u ",
                                         e, v5e, v78 & 0xFF);
                }
                Log("[WeaponKey] template pool tags (+0x5e/+0x78): %s\n", tag);
            }
        }
#endif

        if (!rB)
        {
            Log("[WeaponKey] part-code resolver (B) not reachable - familyFrom covers "
                "template only\n");
            return;
        }
        g_ResolverB = rB;
        void* rtHookTarget = ResolveGameAddress(gAddr.MotionLoaderImpl_GetReceiverType);
        for (int k = 0; k < 5; ++k)
        {
            void* mB = VtblSlotSEH(rB, kPartVtblOff[k]);
            int cs = -9, es = -9;
            const char* note = "";
            if (!mB)
                note = "null";
            else if (mB == m20 || mB == m40 || mB == m150)
                note = "=resolverA";
            else if (mB == rtHookTarget)
                note = "=GetReceiverType";
            else
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
                    note = "shared";
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
#ifdef _DEBUG
            Log("[WeaponKey] part-code slot%d (+0x%zX) method=%p cs=%d es=%d %s\n",
                k, kPartVtblOff[k], mB, cs, es, note);
#else
            if (mB && cs != -9 && (cs != MH_OK || (es != MH_OK && es != MH_ERROR_ENABLED)))
                Log("[WeaponKey] WARNING: part-code slot%d hook failed (cs=%d es=%d) - "
                    "donor part substitution incomplete for this slot\n", k, cs, es);
#endif
        }
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
                    if (lastSig.exchange(sig, std::memory_order_relaxed) != sig)
                    {
                        if (finalType == 0)
                            Log("[WeaponKey] WARNING: rc=%u has NO motion type and "
                                "donor rc=%u has none either - the engine will INVALIDATE "
                                "this weapon; point motionFrom at a receiver with a real "
                                "motion type\n", receiverId, van.gb[0]);
#ifdef _DEBUG
                        else
                            Log("[WeaponKey] receiverType tap: rc=%u motionFrom type=%u, "
                                "donor rc=%u type=%u -> using %u\n",
                                receiverId, result, van.gb[0], donorType, finalType);
#endif
                    }
                    return finalType;
                }
                if (receiverId >= 234)
                {
                    static std::atomic<std::uint32_t> lastMiss{ 0xFFFFFFFF };
                    if (lastMiss.exchange(receiverId, std::memory_order_relaxed) != receiverId)
                        Log("[WeaponKey] WARNING: custom-range rc=%u queried during "
                            "mapped setup but does not match the mapped weapon's receiver - "
                            "no donor substitution applied\n", receiverId);
                }
            }
        }
        return result;
    }

    static void __fastcall hkSetupWeaponInfo(void* self, void* work, int slot)
    {
        if (self)
            EnsureResolverMethodHook(self);

        const std::uint32_t callsBefore =
            g_PartCallCount.load(std::memory_order_relaxed);
        KeyTypeSwap keySwap;
        ApplyKeyTypeSwaps(self, slot, keySwap);
        g_TlsCustomEquip = 0;
        g_TlsVanillaEquip = 0;
        g_InSetupWeaponInfo = true;
        g_OrigSetupWeaponInfo(self, work, slot);
        g_InSetupWeaponInfo = false;
        RestoreKeyWordsSEH(&keySwap);
        const std::uint32_t partCalls =
            g_PartCallCount.load(std::memory_order_relaxed) - callsBefore;
        (void)partCalls;
        const std::uint32_t mappedThis = g_TlsCustomEquip;
        g_TlsCustomEquip = 0;
        g_TlsVanillaEquip = 0;

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
                        Log("[WeaponKey] WARNING: the engine invalidated a custom "
                            "weapon's setup (slot=%d subId=%u equipId=%u) - the weapon "
                            "will not function; check that motionFrom points at a real "
                            "gun's receiver and familyFrom at a usable donor\n",
                            slot, subId, mappedThis);
                    }
                }
            }
#ifdef _DEBUG
            else if (keyOk == 1)
            {
                const std::uint32_t eq = (key & 0xFFFF) >> 5;
                if (IsFamilyMappedEither(eq))
                {
                    std::uint8_t b[16] = {};
                    std::uint32_t cc = 0, d0 = 0;
                    CopyBytesSEH(wi + 0xb0, b, 16);
                    ReadU32AtSEH(wi, 0xcc, cc);
                    ReadU32AtSEH(wi, 0xd0, d0);
                    std::uint16_t t78 = 0;
                    ReadU16SEH(wi + 0x78, t78);
                    Log("[WeaponKey] POST-SETUP eq=%u VALID: eqpType78=%u b9..bd="
                        "%02X %02X %02X %02X %02X  cc=%08X d0=%08X partCalls=%u\n",
                        eq, t78 & 0x1f, b[9], b[10], b[11], b[12], b[13], cc, d0,
                        partCalls);
                    bool doDump = false;
                    {
                        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
                        doDump = g_WeaponInfoDumped.insert(eq).second;
                    }
                    if (doDump)
                        DumpWeaponInfo(work, key, eq);
                }
            }
#endif
        }
    }
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
        Log("[WeaponKey] SetWeaponHandling: mapping for equipId=%u cleared\n", equipId);
        return;
    }
    FamilyGb van, cus;
    const bool vanOk = GetGbForEquipId(familyFrom, van);
    const bool cusOk = GetGbForEquipId(equipId, cus);
    const std::uint32_t vanType = GetEquipTypeForEquipId(familyFrom) & 0x1f;
    if (!vanOk || vanType < 1 || vanType > 8)
    {
        Log("[WeaponKey] SetWeaponHandling REFUSED: familyFrom=%u does not resolve to a "
            "native weapon (subId=%u eqpType=%u). Vanilla EQP_WP_* Lua constants are NOT "
            "equip-table ids - draw the donor weapon once and use the number from its "
            "'[WeaponKey] ... equipId=N' log line. Mapping NOT registered so the weapon "
            "keeps its previous behavior.\n", familyFrom, van.subId, vanType);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_WeaponKeyMutex);
        g_FamilyFrom[equipId] = familyFrom;
        g_HasSwaps.store(true, std::memory_order_relaxed);
    }
    int donorMotionType = -1;
    EnsureRcvTypeExt();
    if (g_RcvTypeExtReady && van.gb[0] < 240)
        donorMotionType = g_RcvTypeExt[van.gb[0]];
    Log("[WeaponKey] SetWeaponHandling: equipId=%u borrows the animation family of "
        "vanilla equipId=%u (eqpType=%u subId=%u rc=%u motionType=%d ba=%u am=%u "
        "sk=%u ub=%u; custom subId=%u %s)\n",
        equipId, familyFrom, vanType, van.subId, van.gb[0], donorMotionType,
        van.gb[1], van.gb[2], van.gb[3], van.gb[10], cus.subId,
        cusOk ? "ok" : "resolves at draw");
}

bool Install_WeaponKeyLog()
{
#ifdef _DEBUG
    Log("[WeaponKey] diagnostics build " __DATE__ " " __TIME__ "\n");
#endif
    const uintptr_t addr = SetupWeaponInfoAddr();
    if (!addr)
    {
        Log("[WeaponKey] SetupWeaponInfo not pinned for this build - key log skipped\n");
        return true;
    }
    void* target = ResolveGameAddress(addr);
    const bool ok = CreateAndEnableHook(
        target, &hkSetupWeaponInfo,
        reinterpret_cast<void**>(&g_OrigSetupWeaponInfo));
    if (!ok)
        Log("[WeaponKey] SetupWeaponInfo key-log Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[WeaponKey] SetupWeaponInfo key-log Install -> OK (target=%p)\n", target);
#endif
#ifdef _DEBUG
    const uintptr_t fsmAddr = AttackFsmStateChangeAddr();
    if (fsmAddr)
    {
        g_ExecStateChangeAddr = ResolveGameAddress(fsmAddr);
        const bool fsmOk = CreateAndEnableHook(
            g_ExecStateChangeAddr, &hkExecStateChange,
            reinterpret_cast<void**>(&g_OrigExecStateChange));
        Log("[WeaponKey] attack-FSM tracer %s (ExecStateChangeImpl=%p)\n",
            fsmOk ? "armed" : "FAILED", g_ExecStateChangeAddr);
        if (!fsmOk)
            g_ExecStateChangeAddr = nullptr;
    }
    const uintptr_t missAddr = AnimSearchMotionDataAddr();
    if (missAddr)
    {
        g_SearchMotionDataAddr = ResolveGameAddress(missAddr);
        const bool missOk = CreateAndEnableHook(
            g_SearchMotionDataAddr, &hkSearchMotionData,
            reinterpret_cast<void**>(&g_OrigSearchMotionData));
        Log("[WeaponKey] motion-miss logger %s (SearchMotionData=%p)\n",
            missOk ? "armed" : "FAILED", g_SearchMotionDataAddr);
        if (!missOk)
            g_SearchMotionDataAddr = nullptr;
    }
#endif
    return ok;
}

void Uninstall_WeaponKeyLog()
{
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

    if (g_ExecStateChangeAddr)
    {
        DisableAndRemoveHook(g_ExecStateChangeAddr);
        g_ExecStateChangeAddr = nullptr;
    }
    g_OrigExecStateChange = nullptr;

    if (g_SearchMotionDataAddr)
    {
        DisableAndRemoveHook(g_SearchMotionDataAddr);
        g_SearchMotionDataAddr = nullptr;
    }
    g_OrigSearchMotionData = nullptr;
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
        Log("[EquipParam] SetUpGunInfo guard Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[EquipParam] SetUpGunInfo guard Install -> OK (target=%p)\n", target);
#endif
    return ok;
}

void Uninstall_GunInfoGuard()
{
    if (gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc && g_OrigSetUpGunInfo)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.EquipSystem_SetUpGunInfoFromGunPartsDesc));
    g_OrigSetUpGunInfo = nullptr;
}

namespace
{

    using DefineFireSound_t = void(__fastcall*)(void*, unsigned int, unsigned __int64,
                                                const char*, char, char, unsigned int*);
    static DefineFireSound_t g_OrigDefineFireSound = nullptr;

    static bool ApplyEventSoundSEH(void* sys, const char* name,
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
            outHashes[0] = h;
            outHashes[1] = h;
            outHashes[2] = h;
            outHashes[3] = h;
            return true;
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
            return false;
        }
    }

    static void __fastcall hkDefineFireSound(void* sys, unsigned int eqpType,
                                             unsigned __int64 r8, const char* root,
                                             char suffixSel, char buildSup,
                                             unsigned int* outHashes)
    {
        std::string text;
        int mSeg = -1;
        bool isEvent = false;
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
                        text = it->second.text;
                        mSeg = it->second.mSeg;
                        isEvent = it->second.isEvent;
                        found = true;
                    }
                }
            }
        }
        if (found && !text.empty())
        {
            if (isEvent)
            {
                if (ApplyEventSoundSEH(sys, text.c_str(), outHashes))
                    return;
            }
            else
            {
                const unsigned int et = (mSeg >= 0)
                    ? static_cast<unsigned int>(mSeg) : eqpType;
                g_OrigDefineFireSound(sys, et, r8, text.c_str(),
                                      suffixSel, buildSup, outHashes);
                return;
            }
        }
        g_OrigDefineFireSound(sys, eqpType, r8, root, suffixSel, buildSup, outHashes);
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
        Log("[EquipParam] fire-sound override Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[EquipParam] fire-sound override Install -> OK (target=%p)\n", target);
#endif
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
        Log("[EquipParam] GetAttackIdByEquipId guard Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[EquipParam] GetAttackIdByEquipId guard Install -> OK (target=%p)\n", target);
#endif
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

    static bool WeaponEquipIdIsPhantom(std::int32_t equipId)
    {
   
        if (equipId == 0)
            return false;
        const std::int32_t idx = EquipIdCompression::ComputeCompressed(equipId);
        if (!EquipIdCompression::IsCompressedInBounds(idx))
            return false;
        std::uint16_t* tw = static_cast<std::uint16_t*>(
            ResolveGameAddress(gAddr.EquipIdTable_TypeWords));
        if (!tw)
            return false;
        return tw[idx] == 0;
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
                if (!WeaponEquipIdIsPhantom(id))
                {
                    if (id > 0 && id < 0x289 && *outPinnedCount < 3)
                        outPinned[(*outPinnedCount)++] = id;
                    continue;
                }
                *srcW = 0;   // EQP_None
                if (workingReq)
                    *reinterpret_cast<std::int32_t*>(
                        workingReq + 0x10 + static_cast<std::size_t>(i) * 0x18) = 0;
                Log("[EquipParam] loadout: phantom weapon equipId=%d (its mod was uninstalled "
                    "while it was equipped) -> set to EQP_None (empty slot).\n", id);
            }
        }
        __except (SehAvOnly(GetExceptionCode()))
        {
        }
    }

    static void __fastcall hkUpdateLoadoutRequest(void* self, std::uint32_t slot)
    {
        std::int32_t pinned[3] = {};
        int pinnedCount = 0;
        BlankPhantomLoadoutWeaponsSEH(self, slot, pinned, &pinnedCount);
        for (int i = 0; i < pinnedCount; ++i)
            V_FrameWorkState::NotePinnedEquipId(pinned[i]);
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
                Log("[EquipParam] UpdateLoadoutRequest guarded a crash (a phantom loadout "
                    "weapon could not be cleared this frame) - request skipped.\n");
            }
        }
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
        Log("[EquipParam] UpdateLoadoutRequest guard Install -> FAIL (target=%p)\n", target);
#ifdef _DEBUG
    else
        Log("[EquipParam] UpdateLoadoutRequest guard Install -> OK (target=%p)\n", target);
#endif
    return ok;
}

void Uninstall_LoadoutRequestGuard()
{
    if (gAddr.CorePlugin_UpdateLoadoutRequest && g_OrigUpdateLoadoutRequest)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.CorePlugin_UpdateLoadoutRequest));
    g_OrigUpdateLoadoutRequest = nullptr;
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

    static std::mutex g_SuppLogMutex;
    static std::set<int> g_SuppLogSeen;

    static unsigned char __fastcall hkGetSuppressorAmount(void* self, std::uintptr_t developRow)
    {
        const unsigned char orig =
            g_OrigGetSuppressorAmount ? g_OrigGetSuppressorAmount(self, developRow) : 0;

        const unsigned int row = static_cast<unsigned int>(developRow & 0xFFFF);

        int equipId = -1;
        CallDevResolverSEH(self, row, &equipId);
        const int subId = (equipId > 0) ? TppEquip_GetSubIdForEquipId(equipId) : 0;

        int seg = -1;
        const std::uint8_t* gunBasic = ImplBufPtr(0x08);
        const std::uint8_t* muzzleOpt = ImplBufPtr(0x28);
        if (subId > 0 && gunBasic && muzzleOpt)
            ReadSuppressorRowSEH(gunBasic, muzzleOpt, subId, &seg);

        if (orig == 0)
        {
            std::lock_guard<std::mutex> lk(g_SuppLogMutex);
            if (g_SuppLogSeen.size() < 250 && g_SuppLogSeen.insert(static_cast<int>(row)).second)
            {
                Log("[SuppressorDiag] row=%u orig=%u equipId=%d subId=%d seg=%d\n",
                    row, orig, equipId, subId, seg);
            }
        }

        if (orig != 0)
            return orig;
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
        Log("[SuppressorGauge] GetSuppressorAmount address not set for this build - custom suppressor gauge skipped\n");
        return true;
    }

    const bool ok = CreateAndEnableHook(
        target, &hkGetSuppressorAmount,
        reinterpret_cast<void**>(&g_OrigGetSuppressorAmount));
    if (ok)
        Log("[SuppressorGauge] GetSuppressorAmount hook Install -> OK (target=%p)\n", target);
    else
        Log("[SuppressorGauge] GetSuppressorAmount hook Install -> FAIL (target=%p)\n", target);
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
        int lethalDamage, staminaDamage, impactForce, lethalDamageUI;
        int unk3, unk4, unk5, unk6, unk7, unk8;
        int flags;
        int damageSource, injureType, injurePart, unk11, unk12;
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
            *reinterpret_cast<std::uint16_t*>(p + 0x06) = static_cast<std::uint16_t>(r.lethalDamageUI & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x08) = static_cast<std::uint16_t>(r.unk3 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0a) = static_cast<std::uint16_t>(r.unk4 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0c) = static_cast<std::uint16_t>(r.unk5 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x0e) = static_cast<std::uint16_t>(r.unk6 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x10) = static_cast<std::uint16_t>(r.unk7 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x12) = static_cast<std::uint16_t>(r.unk8 & 0xFFFF);
            *reinterpret_cast<std::uint16_t*>(p + 0x14) = static_cast<std::uint16_t>(r.flags & 0xFFFF);
            p[0x16] = static_cast<std::uint8_t>(r.damageSource & 0xFF);
            p[0x17] = static_cast<std::uint8_t>((r.injureType & 0xF) | ((r.injurePart & 0xF) << 4));
            p[0x18] = static_cast<std::uint8_t>(r.unk11 & 0xFF);
            p[0x19] = static_cast<std::uint8_t>(r.unk12 & 0xFF);
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
        Log("[EquipParam] custom damage id space exhausted for '%s' (cap=%d custom slots)\n",
            name, kDamageCustomCap);
        return 0;
    }

    const int id = g_DamageCustomNextFree++;
    g_DamageNameToId[name] = id;

    CustomDamageRowPtrGrow(id);
    Log("[EquipParam] '%s' -> damageId %d (custom; served from DLL buffer via GetDamageParameter hook)\n",
        name, id);
    return id;
}

int __cdecl l_SetDamage(lua_State* L)
{
    if (!ResolveLuaApi())
        return 0;

    if (g_lua_type(L, 1) != LUA_TTABLE)
    {
        Log("[EquipParam] SetDamage: argument #1 must be a table\n");
        return 0;
    }

    int damageId = 0;
    if (!ReadNamedInt(L, 1, "damageId", damageId) || damageId <= 0)
    {
        Log("[EquipParam] SetDamage: missing/invalid damageId (declare one via V_TppEquip.DeclareDamages)\n");
        return 0;
    }
    if (damageId >= kDamageCustomBase + kDamageCustomCap)
    {
        Log("[EquipParam] SetDamage: damageId=%d out of range (1..%d = vanilla override, %d..%d = custom) - not written\n",
            damageId, kDamageCapacity - 1, kDamageCustomBase, kDamageCustomBase + kDamageCustomCap - 1);
        return 0;
    }

    DamageRow r{};
    r.damageId = damageId;
    ReadNamedInt(L, 1, "lethalDamage",   r.lethalDamage);
    ReadNamedInt(L, 1, "staminaDamage",  r.staminaDamage);
    ReadNamedInt(L, 1, "impactForce",    r.impactForce);
    ReadNamedInt(L, 1, "lethalDamageUI", r.lethalDamageUI);
    ReadNamedInt(L, 1, "unk3", r.unk3);
    ReadNamedInt(L, 1, "unk4", r.unk4);
    ReadNamedInt(L, 1, "unk5", r.unk5);
    ReadNamedInt(L, 1, "unk6", r.unk6);
    ReadNamedInt(L, 1, "unk7", r.unk7);
    ReadNamedInt(L, 1, "unk8", r.unk8);
    ReadNamedInt(L, 1, "damageSource", r.damageSource);
    ReadNamedInt(L, 1, "injureType",   r.injureType);
    ReadNamedInt(L, 1, "injurePart",   r.injurePart);
    ReadNamedInt(L, 1, "unk11", r.unk11);
    ReadNamedInt(L, 1, "unk12", r.unk12);

    auto readFlag = [&](const char* name, const char* legacy) -> int
    {
        int v = 0;
        if (ReadNamedInt(L, 1, name, v) && v)
            return 1;
        if (legacy && ReadNamedInt(L, 1, legacy, v) && v)
            return 1;
        return 0;
    };
    const int hitNPC       = readFlag("hitNPC", nullptr);
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
    r.flags = (hitNPC ? 0x1 : 0) | (isSniper ? 0x2 : 0) | (isShotgun ? 0x4 : 0) | (isTranq ? 0x8 : 0)
            | (isStun ? 0x10 : 0) | (isExplosive ? 0x20 : 0) | (isMelee ? 0x40 : 0) | (isBlade ? 0x80 : 0)
            | (isFire ? 0x100 : 0) | (isWater ? 0x200 : 0) | (isElectric ? 0x400 : 0) | (isParasite ? 0x800 : 0)
            | (isGas ? 0x1000 : 0) | (isVehicleHit ? 0x2000 : 0) | (unk25 ? 0x4000 : 0) | (isPenetrating ? 0x8000 : 0);

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);

    std::uint8_t* buf = DamageBufBase();
    if (!buf && damageId < kDamageCapacity)
    {
        Log("[EquipParam] SetDamage: native damage table not available for this build - vanilla override skipped\n");
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

#ifdef _DEBUG
    Log("[EquipParam] SetDamage damageId=%d lethal=%d stamina=%d flags=0x%04X src=%d -> %s\n",
        damageId, r.lethalDamage, r.staminaDamage, r.flags, r.damageSource,
        (damageId >= kDamageCustomBase) ? "custom DLL slot" : "vanilla override");
#endif
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
        if (r)
            Log("[Damage] ReloadDamageParameter reapply hook Install -> OK (target=%p)\n", reloadTarget);
        else
            Log("[Damage] ReloadDamageParameter reapply hook Install -> FAIL (target=%p)\n", reloadTarget);
        ok = ok && r;
    }
    else
    {
        Log("[Damage] ReloadDamageParameter address not set for this build - reapply guard skipped\n");
    }

    void* getterTarget = ResolveGameAddress(gAddr.DamageParameterTable_GetDamageParameter);
    if (getterTarget)
    {
        const bool r = CreateAndEnableHook(
            getterTarget, &hkGetDamageParameter,
            reinterpret_cast<void**>(&g_OrigGetDamageParameter));
        if (r)
            Log("[Damage] GetDamageParameter redirect hook Install -> OK (target=%p; custom attackIds %d..%d served from DLL buffer)\n",
                getterTarget, kDamageCustomBase, kDamageCustomBase + kDamageCustomCap - 1);
        else
            Log("[Damage] GetDamageParameter redirect hook Install -> FAIL (target=%p)\n", getterTarget);
        ok = ok && r;
    }
    else
    {
        Log("[Damage] GetDamageParameter address not set for this build - custom damage attackIds unavailable\n");
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
        Log("[FobGuard] vanilla %s id %d differs from its stock values after a module "
            "write - vanilla weapons built on it are no longer FOB-deployable\n",
            kVanillaSpaceNames[space], id);
    else if (transition < 0)
        Log("[FobGuard] vanilla %s id %d restored to stock values - FOB block lifted\n",
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
        Log("[FobGuard] vanilla %s id %d given %s by a module - vanilla weapons built "
            "on it are no longer FOB-deployable\n",
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
#ifdef _DEBUG
        Log("[FobGuard] walk equipId=%u CHIMERA slot=%d rc=%u ba=%u am=%u sk=%u "
            "mz=%u mo=%u st=%u/%u lt=%u/%u ub=%u\n",
            equipId, static_cast<int>(equipId) - 0x367, b[0], b[1], b[2], b[3],
            b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
#endif
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

#ifdef _DEBUG
        Log("[FobGuard] walk equipId=%u type=%d weaponId=%d rc=%u ba=%u am=%u sk=%u "
            "mz=%u mo=%u st=%u/%u lt=%u/%u ub=%u\n",
            equipId, eqpType, weaponId, b[0], b[1], b[2], b[3], b[4], b[5], b[6],
            b[7], b[8], b[9], b[10]);
#endif
    }

    std::lock_guard<std::recursive_mutex> lock(g_Mutex);
    return PartsBytesTainted(b);
}
