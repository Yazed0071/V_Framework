#include "pch.h"
#include "EquipPartParams.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaApi.h"

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

    static bool ReadNamedFloat(lua_State* L, int tableIdx, const char* name, double& out)
    {
        g_lua_getfield(L, tableIdx, const_cast<char*>(name));
        const bool ok = g_lua_isnumber(L, -1) != 0;
        if (ok)
            out = static_cast<double>(g_lua_tonumber(L, -1));
        g_lua_settop(L, -2);
        return ok;
    }

    static int ScaleByte(double v, double scale)
    {
        return Clamp(static_cast<int>(v * scale + 0.5), 0, 255);
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
    static const int kReceiverCapacity   = 233;for
    static const int kReceiverMaxId      = 255;
    static PartBuffer g_RcvIdBuf = { "receiverBuffer", 0x10, 6, 233, 255, {}, false, 0, {} };

    struct RcvPool
    {
        PartBuffer pb;
        int rowByteOffset;
        bool counted;
    };
    static RcvPool g_Base = { { "receiverParamSetsBase",     0x58, 12, 0, 255, {}, false, 0, {} }, 2, false };
    static RcvPool g_Wob  = { { "receiverParamSetsWobbling", 0x60, 14, 0, 255, {}, false, 0, {} }, 3, false };
    static RcvPool g_Sys  = { { "receiverParamSetsSystem",   0x68,  3, 0, 255, {}, false, 0, {} }, 4, false };
    static RcvPool g_Snd  = { { "receiverParamSetsSound",    0x70,  8, 0, 255, {}, false, 0, {} }, 5, false };

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

    static unsigned int __fastcall hkGetReceiverType(void* self, unsigned int receiverId)
    {
        if (!g_RcvTypeExtReady)
            EnsureRcvTypeExt();
        if (g_RcvTypeExtReady && receiverId < 256)
            return g_RcvTypeExt[receiverId];
        return g_OrigGetReceiverType ? g_OrigGetReceiverType(self, receiverId) : 0;
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
            g_lua_rawgeti(L, -1, 1);
            if (g_lua_type(L, -1) == LUA_TSTRING)
            {
                size_t len = 0;
                const char* s = g_lua_tolstring(L, -1, &len);
                if (s)
                    for (int c = 0; c < 7 && s[c]; ++c) label[c] = s[c];
            }
            g_lua_settop(L, -2);
            g_lua_settop(L, -2);

            const int idx = AllocateRcvPoolRow(rp);
            if (idx < 0) return -2;
            std::uint8_t* pool = PartCurrentBuf(rp.pb);
            if (!pool) return -2;
            WriteSndRowSEH(pool, idx, label);
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

    WriteMagazineSEH(buf, ammoId, eqpAmmoId, capacity, totalCarry, bulletId);

#ifdef _DEBUG
    Log("[EquipParam] SetMagazine ammoId=%d eqpAmmo=%d cap=%d total=%d bullet=%d -> native slot\n",
        ammoId, eqpAmmoId, capacity, totalCarry, bulletId);
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

    double stability = 1.0;
    double handling = 1.0;
    ReadNamedFloat(L, 1, "stability", stability);
    ReadNamedFloat(L, 1, "handling", handling);

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

    WriteStockSEH(buf, stockId, ScaleByte(stability, 100.0), ScaleByte(handling, 100.0));

#ifdef _DEBUG
    Log("[EquipParam] SetStock stockId=%d stability=%.2f handling=%.2f -> native slot\n",
        stockId, stability, handling);
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

    WriteMuzzleSEH(buf, muzzleOptionId, ScaleByte(grouping, 100.0), durability, suppressor);

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
    WriteSightSEH(buf, scopeId, Clamp(zoom1, 0, 255), Clamp(zoom2, 0, 255),
                  Clamp(zoom3, 0, 255), Clamp(scopeUiId, 0, 255), flags);

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

    int attackId = 0, rtype = 0, motionFrom = 0;
    const bool hasAttack     = ReadNamedInt(L, 1, "attackId", attackId);
    const bool hasType       = ReadNamedInt(L, 1, "receiverType", rtype);
    const bool hasMotionFrom = ReadNamedInt(L, 1, "motionFrom", motionFrom);

    const int base = ResolvePoolField(L, "base",     POOL_BASE);
    const int wob  = ResolvePoolField(L, "wobbling", POOL_WOB);
    const int sys  = ResolvePoolField(L, "system",   POOL_SYS);
    const int snd  = ResolvePoolField(L, "sound",    POOL_SND);

    if (base == -2 || wob == -2 || sys == -2 || snd == -2)
        return 0;
    if (!hasAttack || base < 0 || wob < 0 || sys < 0 || snd < 0)
    {
        Log("[EquipParam] SetReceiver receiverId=%d: requires attackId + base + wobbling + system + "
            "sound; each pool field is a game index (number) or a {custom values} table\n", receiverId);
        return 0;
    }

    WriteReceiverRowSEH(rbuf, receiverId, attackId, base, wob, sys, snd);

    int motionType = -1;
    if (hasType)
        motionType = rtype;
    else if (hasMotionFrom && motionFrom > 0 && motionFrom < 256)
    {
        EnsureRcvTypeExt();
        if (g_RcvTypeExtReady)
            motionType = g_RcvTypeExt[motionFrom];
    }

    if (motionType < 0)
    {
        motionType = 0;
        Log("[EquipParam] SetReceiver receiverId=%d: no receiverType/motionFrom - animation "
            "defaulted to type 0; set motionFrom=<vanilla RC> or receiverType=<index>\n", receiverId);
    }
    if (!WriteReceiverType(receiverId, motionType))
    {
        Log("[EquipParam] SetReceiver receiverId=%d: motion type write failed (table unavailable)\n",
            receiverId);
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
