#define NOMINMAX
#include "pch.h"
#include "SetEquipParameters.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "log.h"

namespace
{
    EquipParams::Deps g_Deps{};

    constexpr int LUA_TNIL_CONST = 0;
    constexpr int LUA_TNUMBER_CONST = 3;
    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;

    struct ReceiverBaseRow
    {
        float v[8]{};

        bool operator==(const ReceiverBaseRow& o) const noexcept
        {
            for (int i = 0; i < 8; ++i)
            {
                if (v[i] != o.v[i])
                    return false;
            }
            return true;
        }
    };

    struct ReceiverWobblingRow
    {
        float v[7]{};

        bool operator==(const ReceiverWobblingRow& o) const noexcept
        {
            for (int i = 0; i < 7; ++i)
            {
                if (v[i] != o.v[i])
                    return false;
            }
            return true;
        }
    };

    struct ReceiverSystemRow
    {
        std::int32_t v[12]{};

        bool operator==(const ReceiverSystemRow& o) const noexcept
        {
            for (int i = 0; i < 12; ++i)
            {
                if (v[i] != o.v[i])
                    return false;
            }
            return true;
        }
    };

    struct ReceiverSoundRow
    {
        std::string sound;

        bool operator==(const ReceiverSoundRow& o) const noexcept
        {
            return sound == o.sound;
        }
    };

    struct BarrelBaseRow
    {
        float v[7]{};

        bool operator==(const BarrelBaseRow& o) const noexcept
        {
            for (int i = 0; i < 7; ++i)
            {
                if (v[i] != o.v[i])
                    return false;
            }
            return true;
        }
    };

    struct BulletBaseRow
    {
        float v[12]{};

        bool operator==(const BulletBaseRow& o) const noexcept
        {
            for (int i = 0; i < 12; ++i)
            {
                if (v[i] != o.v[i])
                    return false;
            }
            return true;
        }
    };

    struct BulletTrailRow
    {
        std::string path;

        bool operator==(const BulletTrailRow& o) const noexcept
        {
            return path == o.path;
        }
    };

    struct ReceiverDef
    {
        std::string ownerKey;
        std::int32_t receiverId = 0;
        std::int32_t attackId = 0;

        std::optional<ReceiverBaseRow> base;
        std::optional<ReceiverWobblingRow> wobbling;
        std::optional<ReceiverSystemRow> system;
        std::optional<ReceiverSoundRow> sound;
    };

    struct BarrelDef
    {
        std::string ownerKey;
        std::int32_t barrelId = 0;

        std::optional<BarrelBaseRow> base;

        std::int32_t barrelLength = 0;
        std::int32_t hasScopeMount = 0;
        std::int32_t unk2 = 0;
        std::int32_t hasSideMount = 0;
        std::int32_t hasUnderMount = 0;
    };

    struct MagazineDef
    {
        std::string ownerKey;
        std::int32_t ammoId = 0;
        std::int32_t eqpAmmoId = 0;
        std::int32_t rank = 0;
        std::int32_t magSize = 0;
        std::int32_t bulletId = 0;
    };

    struct MuzzleOptionDef
    {
        std::string ownerKey;
        std::int32_t muzzleOptionId = 0;
        float grouping = 0.0f;
        std::int32_t durability = 0;
        std::int32_t suppressor = 0;
    };

    struct OptionDef
    {
        std::string ownerKey;
        std::int32_t optionId = 0;
        std::int32_t isLaser = 0;
        std::int32_t isLight = 0;
    };

    struct SightDef
    {
        std::string ownerKey;
        std::int32_t sightId = 0;
        std::int32_t zoom1 = 0;
        std::int32_t zoom2 = 0;
        std::int32_t zoom3 = 0;
        std::int32_t scopeUiId = 0;
        std::int32_t booster = 0;
        std::int32_t nvg = 0;
        std::int32_t builtIn = 0;
        std::int32_t rangeFinder = 0;
        std::int32_t bdc = 0;
    };

    struct StockDef
    {
        std::string ownerKey;
        std::int32_t stockId = 0;
        float field2 = 0.0f;
        float field3 = 0.0f;
    };

    struct UnderBarrelDef
    {
        std::string ownerKey;
        std::int32_t underBarrelId = 0;
        std::int32_t field2 = 0;
        std::int32_t field3 = 0;
        std::int32_t field4 = 0;
    };

    struct BulletRawRow
    {
        float f2 = 0.0f;
        float f3 = 0.0f;
        float f4 = 0.0f;
        std::int32_t f5 = 0;
        std::int32_t f6 = 0;
        std::int32_t f7 = 0;
        std::int32_t f8 = 0;
        std::int32_t f9 = 0;
        std::int32_t f10 = 0;
        std::int32_t f11 = 0;
        std::int32_t flag1 = 0;
        std::int32_t flagBits = 0;
    };

    struct BulletDef
    {
        std::string ownerKey;
        std::int32_t bulletId = 0;
        std::optional<BulletBaseRow> base;
        std::optional<BulletTrailRow> trail;
        std::optional<BulletRawRow> bullet;
    };

    struct EquipParametersRequest
    {
        std::string key;

        std::optional<ReceiverDef> receiver;
        std::optional<BarrelDef> barrel;
        std::optional<MagazineDef> magazine;
        std::optional<MuzzleOptionDef> muzzleOption;
        std::optional<OptionDef> option;
        std::optional<SightDef> sight;
        std::optional<StockDef> stock;
        std::optional<UnderBarrelDef> underBarrel;
        std::optional<BulletDef> bullet;
    };

    template<typename T>
    struct SharedRowRegistry
    {
        std::unordered_map<std::string, std::int32_t> keyToIndex;
        std::vector<T> rows;
    };

    std::mutex g_Mutex;

    SharedRowRegistry<ReceiverBaseRow>     g_ReceiverBaseRegistry;
    SharedRowRegistry<ReceiverWobblingRow> g_ReceiverWobblingRegistry;
    SharedRowRegistry<ReceiverSystemRow>   g_ReceiverSystemRegistry;
    SharedRowRegistry<ReceiverSoundRow>    g_ReceiverSoundRegistry;
    SharedRowRegistry<BarrelBaseRow>       g_BarrelBaseRegistry;
    SharedRowRegistry<BulletBaseRow>       g_BulletBaseRegistry;
    SharedRowRegistry<BulletTrailRow>      g_BulletTrailRegistry;

    std::vector<ReceiverDef>     g_CustomReceivers;
    std::vector<BarrelDef>       g_CustomBarrels;
    std::vector<MagazineDef>     g_CustomMagazines;
    std::vector<MuzzleOptionDef> g_CustomMuzzleOptions;
    std::vector<OptionDef>       g_CustomOptions;
    std::vector<SightDef>        g_CustomSights;
    std::vector<StockDef>        g_CustomStocks;
    std::vector<UnderBarrelDef>  g_CustomUnderBarrels;
    std::vector<BulletDef>       g_CustomBullets;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.GetLuaInt &&
            g_Deps.GetLuaNumber &&
            g_Deps.GetLuaString &&
            g_Deps.LuaObjLen &&
            g_Deps.LuaSetTop &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaGetField &&
            g_Deps.LuaRawGetI &&
            g_Deps.LuaGetTable &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushValue;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static bool LuaIsTable(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TTABLE_CONST;
    }

    static bool LuaIsNumber(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TNUMBER_CONST;
    }

    static bool LuaIsString(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static bool LuaIsNil(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TNIL_CONST;
    }

    static void PopOne(lua_State* L)
    {
        g_Deps.LuaSetTop(L, -2);
    }

    static std::string MakeBinaryKey(const void* data, std::size_t size)
    {
        return std::string(reinterpret_cast<const char*>(data), size);
    }

    template<typename T>
    static std::string MakeBinaryKey(const T& value)
    {
        return MakeBinaryKey(&value, sizeof(T));
    }

    static std::string MakeSoundKey(const ReceiverSoundRow& row)
    {
        return row.sound;
    }

    static std::string MakeTrailKey(const BulletTrailRow& row)
    {
        return row.path;
    }

    template<typename T>
    static std::int32_t InternRow(
        SharedRowRegistry<T>& registry,
        const T& row,
        const std::string& key)
    {
        auto it = registry.keyToIndex.find(key);
        if (it != registry.keyToIndex.end())
            return it->second;

        const std::int32_t index = static_cast<std::int32_t>(registry.rows.size());
        registry.rows.push_back(row);
        registry.keyToIndex.emplace(key, index);
        return index;
    }

    static std::int32_t ResolveReceiverBaseIndex(const ReceiverBaseRow& row)
    {
        return InternRow(g_ReceiverBaseRegistry, row, MakeBinaryKey(row));
    }

    static std::int32_t ResolveReceiverWobblingIndex(const ReceiverWobblingRow& row)
    {
        return InternRow(g_ReceiverWobblingRegistry, row, MakeBinaryKey(row));
    }

    static std::int32_t ResolveReceiverSystemIndex(const ReceiverSystemRow& row)
    {
        return InternRow(g_ReceiverSystemRegistry, row, MakeBinaryKey(row));
    }

    static std::int32_t ResolveReceiverSoundIndex(const ReceiverSoundRow& row)
    {
        return InternRow(g_ReceiverSoundRegistry, row, MakeSoundKey(row));
    }

    static std::int32_t ResolveBarrelBaseIndex(const BarrelBaseRow& row)
    {
        return InternRow(g_BarrelBaseRegistry, row, MakeBinaryKey(row));
    }

    static std::int32_t ResolveBulletBaseIndex(const BulletBaseRow& row)
    {
        return InternRow(g_BulletBaseRegistry, row, MakeBinaryKey(row));
    }

    static std::int32_t ResolveBulletTrailIndex(const BulletTrailRow& row)
    {
        return InternRow(g_BulletTrailRegistry, row, MakeTrailKey(row));
    }

    static bool ReadFieldInt(lua_State* L, int tableIndex, const char* name, std::int32_t& out)
    {
        g_Deps.LuaGetField(L, tableIndex, name);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            out = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));

        PopOne(L);
        return ok;
    }

    static void ReadFieldFloatOptional(lua_State* L, int tableIndex, const char* name, float defaultValue, float& out)
    {
        out = defaultValue;

        g_Deps.LuaGetField(L, tableIndex, name);
        if (LuaIsNumber(L, -1))
            out = static_cast<float>(g_Deps.GetLuaNumber(L, -1));

        PopOne(L);
    }

    static bool ReadFieldFloat(lua_State* L, int tableIndex, const char* name, float& out)
    {
        g_Deps.LuaGetField(L, tableIndex, name);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            out = static_cast<float>(g_Deps.GetLuaNumber(L, -1));

        PopOne(L);
        return ok;
    }
    static bool ReadFieldFloatOptional(lua_State* L, int tableIndex, const char* name, float& outValue)
    {
        g_Deps.LuaGetField(L, tableIndex, name);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = static_cast<float>(g_Deps.GetLuaNumber(L, -1));

        PopOne(L);
        return ok;
    }

    static bool ReadFieldIntOptional(lua_State* L, int tableIndex, const char* name, std::int32_t& outValue)
    {
        g_Deps.LuaGetField(L, tableIndex, name);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));

        PopOne(L);
        return ok;
    }

    static bool ReadArrayFloatAt(lua_State* L, int tableIndex, int index1Based, float& outValue)
    {
        g_Deps.LuaRawGetI(L, tableIndex, index1Based);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = static_cast<float>(g_Deps.GetLuaNumber(L, -1));

        PopOne(L);
        return ok;
    }

    static bool ReadArrayIntAt(lua_State* L, int tableIndex, int index1Based, std::int32_t& outValue)
    {
        g_Deps.LuaRawGetI(L, tableIndex, index1Based);

        const bool ok = LuaIsNumber(L, -1);
        if (ok)
            outValue = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));

        PopOne(L);
        return ok;
    }
    static bool HasAnyNamedField(lua_State* L, int tableIndex, const char* const* names, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            g_Deps.LuaGetField(L, tableIndex, names[i]);
            const bool exists = LuaIsNumber(L, -1) || LuaIsString(L, -1) || LuaIsTable(L, -1);
            PopOne(L);

            if (exists)
                return true;
        }

        return false;
    }
    static bool ReadNamedOrArrayFloatRow(lua_State* L, int tableIndex, float* outValues, int count, const char* const* names)
    {
        const bool hasNamed = HasAnyNamedField(L, tableIndex, names, count);

        if (hasNamed)
        {
            for (int i = 0; i < count; ++i)
            {
                if (!ReadFieldFloatOptional(L, tableIndex, names[i], outValues[i]))
                    return false;
            }
            return true;
        }

        for (int i = 0; i < count; ++i)
        {
            if (!ReadArrayFloatAt(L, tableIndex, i + 1, outValues[i]))
                return false;
        }

        return true;
    }

    static bool ReadNamedOrArrayIntRow(lua_State* L, int tableIndex, std::int32_t* outValues, int count, const char* const* names)
    {
        const bool hasNamed = HasAnyNamedField(L, tableIndex, names, count);

        if (hasNamed)
        {
            for (int i = 0; i < count; ++i)
            {
                if (!ReadFieldIntOptional(L, tableIndex, names[i], outValues[i]))
                    return false;
            }
            return true;
        }

        for (int i = 0; i < count; ++i)
        {
            if (!ReadArrayIntAt(L, tableIndex, i + 1, outValues[i]))
                return false;
        }

        return true;
    }

    static bool ReadFieldString(lua_State* L, int tableIndex, const char* name, std::string& out)
    {
        g_Deps.LuaGetField(L, tableIndex, name);

        const bool ok = LuaIsString(L, -1);
        if (ok)
        {
            const char* s = g_Deps.GetLuaString(L, -1);
            out = s ? s : "";
        }

        PopOne(L);
        return ok;
    }

    static bool PushFieldTable(lua_State* L, int tableIndex, const char* name)
    {
        g_Deps.LuaGetField(L, tableIndex, name);
        if (!LuaIsTable(L, -1))
        {
            PopOne(L);
            return false;
        }
        return true;
    }

    static bool ReadFloatArray(lua_State* L, int tableIndex, float* out, int count)
    {
        if (!LuaIsTable(L, tableIndex))
            return false;

        for (int i = 0; i < count; ++i)
        {
            g_Deps.LuaRawGetI(L, tableIndex, i + 1);

            if (!LuaIsNumber(L, -1))
            {
                PopOne(L);
                return false;
            }

            out[i] = static_cast<float>(g_Deps.GetLuaNumber(L, -1));
            PopOne(L);
        }

        return true;
    }

    static bool ReadIntArray(lua_State* L, int tableIndex, std::int32_t* out, int count)
    {
        if (!LuaIsTable(L, tableIndex))
            return false;

        for (int i = 0; i < count; ++i)
        {
            g_Deps.LuaRawGetI(L, tableIndex, i + 1);

            if (!LuaIsNumber(L, -1))
            {
                PopOne(L);
                return false;
            }

            out[i] = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));
            PopOne(L);
        }

        return true;
    }

    static bool ParseReceiver(lua_State* L, int idx, ReceiverDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "receiverId", out.receiverId))
            return false;

        ReadFieldInt(L, idx, "attackId", out.attackId);

        if (PushFieldTable(L, idx, "receiverParamSetsBase"))
        {
            static const char* kNames[] =
            {
                "fireRate",
                "aimAssistDist",
                "drawSpeed",
                "unk4",
                "unk5",
                "ironSight1",
                "ironSight2",
                "reloadSpeed"
            };

            ReceiverBaseRow row{};
            if (!ReadNamedOrArrayFloatRow(L, -1, row.v, 8, kNames))
            {
                PopOne(L);
                return false;
            }

            out.base = row;
            PopOne(L);
        }

        if (PushFieldTable(L, idx, "receiverParamSetsWobbling"))
        {
            static const char* kNames[] =
            {
                "unk1", "unk2", "unk3", "unk4", "unk5", "unk6", "unk7"
            };

            ReceiverWobblingRow row{};
            if (!ReadNamedOrArrayFloatRow(L, -1, row.v, 7, kNames))
            {
                PopOne(L);
                return false;
            }

            out.wobbling = row;
            PopOne(L);
        }

        if (PushFieldTable(L, idx, "receiverParamSetsSystem"))
        {
            static const char* kNames[] =
            {
                "unk1", "unk2", "unk3", "unk4", "unk5", "unk6",
                "unk7", "unk8", "unk9", "unk10", "unk11", "unk12"
            };

            ReceiverSystemRow row{};
            if (!ReadNamedOrArrayIntRow(L, -1, row.v, 12, kNames))
            {
                PopOne(L);
                return false;
            }

            out.system = row;
            PopOne(L);
        }

        if (PushFieldTable(L, idx, "receiverParamSetsSound"))
        {
            g_Deps.LuaRawGetI(L, -1, 1);

            if (!LuaIsString(L, -1))
            {
                PopOne(L);
                PopOne(L);
                return false;
            }

            ReceiverSoundRow row{};
            {
                const char* s = g_Deps.GetLuaString(L, -1);
                row.sound = s ? s : "";
            }

            PopOne(L);

            if (row.sound.empty())
            {
                PopOne(L);
                return false;
            }

            out.sound = row;
            PopOne(L);
        }

        return true;
    }

    static bool ParseBarrel(lua_State* L, int idx, BarrelDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "barrelId", out.barrelId))
            return false;

        if (PushFieldTable(L, idx, "barrelParamSetsBase"))
        {
            static const char* kNames[] =
            {
                "unk1", "unk2", "unk3", "unk4", "unk5", "unk6", "unk7"
            };

            BarrelBaseRow row{};
            if (!ReadNamedOrArrayFloatRow(L, -1, row.v, 7, kNames))
            {
                PopOne(L);
                return false;
            }

            out.base = row;
            PopOne(L);
        }

        ReadFieldInt(L, idx, "barrelLength", out.barrelLength);
        ReadFieldInt(L, idx, "hasScopeMount", out.hasScopeMount);
        ReadFieldInt(L, idx, "unk2", out.unk2);
        ReadFieldInt(L, idx, "hasSideMount", out.hasSideMount);
        ReadFieldInt(L, idx, "hasUnderMount", out.hasUnderMount);

        return true;
    }

    static bool ParseMagazine(lua_State* L, int idx, MagazineDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "ammoId", out.ammoId))
            return false;

        ReadFieldInt(L, idx, "eqpAmmoId", out.eqpAmmoId);
        ReadFieldInt(L, idx, "magCapacity", out.rank);
        ReadFieldInt(L, idx, "totalCarryCapacity", out.magSize);
        ReadFieldInt(L, idx, "bulletId", out.bulletId);

        return true;
    }

    static bool ParseMuzzleOption(lua_State* L, int idx, MuzzleOptionDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "muzzleOptionId", out.muzzleOptionId))
            return false;

        ReadFieldFloatOptional(L, idx, "grouping", 0.0f, out.grouping);
        ReadFieldInt(L, idx, "durability", out.durability);
        ReadFieldInt(L, idx, "suppressor", out.suppressor);

        return true;
    }

    static bool ParseOption(lua_State* L, int idx, OptionDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "optionId", out.optionId))
            return false;

        ReadFieldInt(L, idx, "isLaser", out.isLaser);
        ReadFieldInt(L, idx, "isLight", out.isLight);

        return true;
    }

    static bool ParseSight(lua_State* L, int idx, SightDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "sightId", out.sightId))
            return false;

        ReadFieldInt(L, idx, "zoom1", out.zoom1);
        ReadFieldInt(L, idx, "zoom2", out.zoom2);
        ReadFieldInt(L, idx, "zoom3", out.zoom3);
        ReadFieldInt(L, idx, "scopeUiId", out.scopeUiId);
        ReadFieldInt(L, idx, "booster", out.booster);
        ReadFieldInt(L, idx, "nvg", out.nvg);
        ReadFieldInt(L, idx, "builtIn", out.builtIn);
        ReadFieldInt(L, idx, "rangeFinder", out.rangeFinder);
        ReadFieldInt(L, idx, "bdc", out.bdc);

        return true;
    }

    static bool ParseStock(lua_State* L, int idx, StockDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "stockId", out.stockId))
            return false;

        ReadFieldFloatOptional(L, idx, "field2", 0.0f, out.field2);
        ReadFieldFloatOptional(L, idx, "field3", 0.0f, out.field3);

        return true;
    }

    static bool ParseUnderBarrel(lua_State* L, int idx, UnderBarrelDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "underBarrelId", out.underBarrelId))
            return false;

        ReadFieldInt(L, idx, "field2", out.field2);
        ReadFieldInt(L, idx, "field3", out.field3);
        ReadFieldInt(L, idx, "field4", out.field4);

        return true;
    }

    static bool ParseBullet(lua_State* L, int idx, BulletDef& out, const std::string& ownerKey)
    {
        out.ownerKey = ownerKey;

        if (!ReadFieldInt(L, idx, "bulletId", out.bulletId))
            return false;

        if (PushFieldTable(L, idx, "bulletParamSetsBase"))
        {
            static const char* kNames[] =
            {
                "unk1", "unk2", "unk3", "unk4", "unk5", "unk6",
                "unk7", "unk8", "unk9", "unk10", "unk11", "unk12"
            };

            BulletBaseRow row{};
            if (!ReadNamedOrArrayFloatRow(L, -1, row.v, 12, kNames))
            {
                PopOne(L);
                return false;
            }

            out.base = row;
            PopOne(L);
        }

        if (PushFieldTable(L, idx, "bulletTrailEffectList"))
        {
            g_Deps.LuaRawGetI(L, -1, 1);

            if (!LuaIsString(L, -1))
            {
                PopOne(L);
                PopOne(L);
                return false;
            }

            BulletTrailRow row{};
            {
                const char* s = g_Deps.GetLuaString(L, -1);
                row.path = s ? s : "";
            }

            PopOne(L);

            if (row.path.empty())
            {
                PopOne(L);
                return false;
            }

            out.trail = row;
            PopOne(L);
        }

        if (PushFieldTable(L, idx, "bullet"))
        {
            BulletRawRow row{};

            static const char* kBulletNames[] =
            {
                "unk1", "unk2", "unk3", "unk4", "unk5", "unk6",
                "unk7", "unk8", "unk9", "unk10", "flag1", "flagBits"
            };

            const bool hasNamed = HasAnyNamedField(L, -1, kBulletNames, 12);

            if (hasNamed)
            {
                if (!ReadFieldFloatOptional(L, -1, "unk1", row.f2)) { PopOne(L); return false; }
                if (!ReadFieldFloatOptional(L, -1, "unk2", row.f3)) { PopOne(L); return false; }
                if (!ReadFieldFloatOptional(L, -1, "unk3", row.f4)) { PopOne(L); return false; }

                if (!ReadFieldInt(L, -1, "unk4", row.f5)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk5", row.f6)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk6", row.f7)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk7", row.f8)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk8", row.f9)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk9", row.f10)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "unk10", row.f11)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "flag1", row.flag1)) { PopOne(L); return false; }
                if (!ReadFieldInt(L, -1, "flagBits", row.flagBits)) { PopOne(L); return false; }
            }
            else
            {
                if (!ReadArrayFloatAt(L, -1, 1, row.f2)) { PopOne(L); return false; }
                if (!ReadArrayFloatAt(L, -1, 2, row.f3)) { PopOne(L); return false; }
                if (!ReadArrayFloatAt(L, -1, 3, row.f4)) { PopOne(L); return false; }

                if (!ReadArrayIntAt(L, -1, 4, row.f5)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 5, row.f6)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 6, row.f7)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 7, row.f8)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 8, row.f9)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 9, row.f10)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 10, row.f11)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 11, row.flag1)) { PopOne(L); return false; }
                if (!ReadArrayIntAt(L, -1, 12, row.flagBits)) { PopOne(L); return false; }
            }

            out.bullet = row;
            PopOne(L);
        }

        return true;
    }

    static bool ParseRequest(lua_State* L, int idx, EquipParametersRequest& out)
    {
        ReadFieldString(L, idx, "key", out.key);

        if (PushFieldTable(L, idx, "receiver"))
        {
            ReceiverDef def{};
            const bool ok = ParseReceiver(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.receiver = def;
        }

        if (PushFieldTable(L, idx, "barrel"))
        {
            BarrelDef def{};
            const bool ok = ParseBarrel(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.barrel = def;
        }

        if (PushFieldTable(L, idx, "magazine"))
        {
            MagazineDef def{};
            const bool ok = ParseMagazine(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.magazine = def;
        }

        if (PushFieldTable(L, idx, "muzzleOption"))
        {
            MuzzleOptionDef def{};
            const bool ok = ParseMuzzleOption(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.muzzleOption = def;
        }

        if (PushFieldTable(L, idx, "option"))
        {
            OptionDef def{};
            const bool ok = ParseOption(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.option = def;
        }

        if (PushFieldTable(L, idx, "sight"))
        {
            SightDef def{};
            const bool ok = ParseSight(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.sight = def;
        }

        if (PushFieldTable(L, idx, "stock"))
        {
            StockDef def{};
            const bool ok = ParseStock(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.stock = def;
        }

        if (PushFieldTable(L, idx, "underBarrel"))
        {
            UnderBarrelDef def{};
            const bool ok = ParseUnderBarrel(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.underBarrel = def;
        }

        if (PushFieldTable(L, idx, "bullet"))
        {
            BulletDef def{};
            const bool ok = ParseBullet(L, -1, def, out.key);
            PopOne(L);
            if (!ok)
                return false;
            out.bullet = def;
        }

        return true;
    }

    template<typename T, typename Pred>
    static void UpsertBy(std::vector<T>& vec, const T& value, Pred pred)
    {
        auto it = std::find_if(vec.begin(), vec.end(), pred);
        if (it != vec.end())
        {
            *it = value;
            return;
        }
        vec.push_back(value);
    }

    static void QueueReceiver(const ReceiverDef& def)
    {
        UpsertBy(g_CustomReceivers, def, [&](const ReceiverDef& x) {
            return x.receiverId == def.receiverId;
            });

        Log("[EquipParams] queued receiver receiverId=0x%X owner='%s'\n",
            def.receiverId, def.ownerKey.c_str());
    }

    static void QueueBarrel(const BarrelDef& def)
    {
        UpsertBy(g_CustomBarrels, def, [&](const BarrelDef& x) {
            return x.barrelId == def.barrelId;
            });

        Log("[EquipParams] queued barrel barrelId=0x%X owner='%s'\n",
            def.barrelId, def.ownerKey.c_str());
    }

    static void QueueMagazine(const MagazineDef& def)
    {
        UpsertBy(g_CustomMagazines, def, [&](const MagazineDef& x) {
            return x.ammoId == def.ammoId;
            });

        Log("[EquipParams] queued magazine ammoId=0x%X owner='%s'\n",
            def.ammoId, def.ownerKey.c_str());
    }

    static void QueueMuzzleOption(const MuzzleOptionDef& def)
    {
        UpsertBy(g_CustomMuzzleOptions, def, [&](const MuzzleOptionDef& x) {
            return x.muzzleOptionId == def.muzzleOptionId;
            });

        Log("[EquipParams] queued muzzleOption muzzleOptionId=0x%X owner='%s'\n",
            def.muzzleOptionId, def.ownerKey.c_str());
    }

    static void QueueOption(const OptionDef& def)
    {
        UpsertBy(g_CustomOptions, def, [&](const OptionDef& x) {
            return x.optionId == def.optionId;
            });

        Log("[EquipParams] queued option optionId=0x%X owner='%s'\n",
            def.optionId, def.ownerKey.c_str());
    }

    static void QueueSight(const SightDef& def)
    {
        UpsertBy(g_CustomSights, def, [&](const SightDef& x) {
            return x.sightId == def.sightId;
            });

        Log("[EquipParams] queued sight sightId=0x%X owner='%s'\n",
            def.sightId, def.ownerKey.c_str());
    }

    static void QueueStock(const StockDef& def)
    {
        UpsertBy(g_CustomStocks, def, [&](const StockDef& x) {
            return x.stockId == def.stockId;
            });

        Log("[EquipParams] queued stock stockId=0x%X owner='%s'\n",
            def.stockId, def.ownerKey.c_str());
    }

    static void QueueUnderBarrel(const UnderBarrelDef& def)
    {
        UpsertBy(g_CustomUnderBarrels, def, [&](const UnderBarrelDef& x) {
            return x.underBarrelId == def.underBarrelId;
            });

        Log("[EquipParams] queued underBarrel underBarrelId=0x%X owner='%s'\n",
            def.underBarrelId, def.ownerKey.c_str());
    }

    static void QueueBullet(const BulletDef& def)
    {
        UpsertBy(g_CustomBullets, def, [&](const BulletDef& x) {
            return x.bulletId == def.bulletId;
            });

        Log("[EquipParams] queued bullet bulletId=0x%X owner='%s'\n",
            def.bulletId, def.ownerKey.c_str());
    }

    static bool ApplyRequest(const EquipParametersRequest& req)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);

        if (req.receiver)     QueueReceiver(*req.receiver);
        if (req.barrel)       QueueBarrel(*req.barrel);
        if (req.magazine)     QueueMagazine(*req.magazine);
        if (req.muzzleOption) QueueMuzzleOption(*req.muzzleOption);
        if (req.option)       QueueOption(*req.option);
        if (req.sight)        QueueSight(*req.sight);
        if (req.stock)        QueueStock(*req.stock);
        if (req.underBarrel)  QueueUnderBarrel(*req.underBarrel);
        if (req.bullet)       QueueBullet(*req.bullet);

        return true;
    }

    static void RebuildSharedRegistries()
    {
        g_ReceiverBaseRegistry.keyToIndex.clear();
        g_ReceiverBaseRegistry.rows.clear();

        g_ReceiverWobblingRegistry.keyToIndex.clear();
        g_ReceiverWobblingRegistry.rows.clear();

        g_ReceiverSystemRegistry.keyToIndex.clear();
        g_ReceiverSystemRegistry.rows.clear();

        g_ReceiverSoundRegistry.keyToIndex.clear();
        g_ReceiverSoundRegistry.rows.clear();

        g_BarrelBaseRegistry.keyToIndex.clear();
        g_BarrelBaseRegistry.rows.clear();

        g_BulletBaseRegistry.keyToIndex.clear();
        g_BulletBaseRegistry.rows.clear();

        g_BulletTrailRegistry.keyToIndex.clear();
        g_BulletTrailRegistry.rows.clear();

        for (const auto& def : g_CustomReceivers)
        {
            if (def.base)      (void)ResolveReceiverBaseIndex(*def.base);
            if (def.wobbling)  (void)ResolveReceiverWobblingIndex(*def.wobbling);
            if (def.system)    (void)ResolveReceiverSystemIndex(*def.system);
            if (def.sound)     (void)ResolveReceiverSoundIndex(*def.sound);
        }

        for (const auto& def : g_CustomBarrels)
        {
            if (def.base)      (void)ResolveBarrelBaseIndex(*def.base);
        }

        for (const auto& def : g_CustomBullets)
        {
            if (def.base)      (void)ResolveBulletBaseIndex(*def.base);
            if (def.trail)     (void)ResolveBulletTrailIndex(*def.trail);
        }
    }
}

namespace EquipParams
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_SetEquipParameters(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !LuaIsTable(L, 1))
            return 0;

        EquipParametersRequest req{};
        if (!ParseRequest(L, 1, req))
        {
            Log("[EquipParams] invalid SetEquipParameters payload\n");
            return 0;
        }

        ApplyRequest(req);
        return 0;
    }

    void ApplyQueuedEquipParameters_LuaTables(lua_State* L)
    {
        if (!L || !EnsureLuaReady() || !LuaIsTable(L, -1))
            return;

        std::lock_guard<std::mutex> lock(g_Mutex);
        RebuildSharedRegistries();

        Log("[EquipParams] Rebuilt shared registries: receiverBase=%zu receiverWobbling=%zu receiverSystem=%zu receiverSound=%zu barrelBase=%zu bulletBase=%zu bulletTrail=%zu\n",
            g_ReceiverBaseRegistry.rows.size(),
            g_ReceiverWobblingRegistry.rows.size(),
            g_ReceiverSystemRegistry.rows.size(),
            g_ReceiverSoundRegistry.rows.size(),
            g_BarrelBaseRegistry.rows.size(),
            g_BulletBaseRegistry.rows.size(),
            g_BulletTrailRegistry.rows.size());

        const int entryTop = g_Deps.GetLuaTop(L);

        auto AbsIndex = [&](int idx) -> int
            {
                if (idx > 0)
                    return idx;
                return g_Deps.GetLuaTop(L) + idx + 1;
            };

        auto PushRootFieldTable = [&](const char* name, int& outTableIndex) -> bool
            {
                outTableIndex = 0;

                const int rootIndex = AbsIndex(-1);
                g_Deps.LuaGetField(L, rootIndex, name);
                if (!LuaIsTable(L, -1))
                {
                    PopOne(L);
                    return false;
                }

                outTableIndex = g_Deps.GetLuaTop(L);
                return true;
            };

        auto ReadRowIntField = [&](int rowIndex, int fieldIndex1Based, std::int32_t defaultValue = 0) -> std::int32_t
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.LuaRawGetI(L, rowAbs, fieldIndex1Based);

                std::int32_t value = defaultValue;
                if (LuaIsNumber(L, -1))
                    value = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, -1));

                PopOne(L);
                return value;
            };

        auto ReadRowFloatField = [&](int rowIndex, int fieldIndex1Based, float defaultValue = 0.0f) -> float
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.LuaRawGetI(L, rowAbs, fieldIndex1Based);

                float value = defaultValue;
                if (LuaIsNumber(L, -1))
                    value = static_cast<float>(g_Deps.GetLuaNumber(L, -1));

                PopOne(L);
                return value;
            };

        auto ReadRowStringField = [&](int rowIndex, int fieldIndex1Based) -> std::string
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.LuaRawGetI(L, rowAbs, fieldIndex1Based);

                std::string value;
                if (LuaIsString(L, -1))
                {
                    const char* s = g_Deps.GetLuaString(L, -1);
                    value = s ? s : "";
                }

                PopOne(L);
                return value;
            };

        auto WriteRowIntField = [&](int rowIndex, int fieldIndex1Based, std::int32_t value)
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
                g_Deps.PushLuaNumber(L, static_cast<float>(value));
                g_Deps.LuaSetTable(L, rowAbs);
            };

        auto WriteRowFloatField = [&](int rowIndex, int fieldIndex1Based, float value)
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
                g_Deps.PushLuaNumber(L, value);
                g_Deps.LuaSetTable(L, rowAbs);
            };

        auto WriteRowStringField = [&](int rowIndex, int fieldIndex1Based, const std::string& value)
            {
                const int rowAbs = AbsIndex(rowIndex);

                g_Deps.PushLuaNumber(L, static_cast<float>(fieldIndex1Based));
                g_Deps.LuaPushString(L, value.c_str());
                g_Deps.LuaSetTable(L, rowAbs);
            };

        auto RowEqualsFloatArray = [&](int rowIndex, const float* values, int count) -> bool
            {
                if (!LuaIsTable(L, rowIndex))
                    return false;

                for (int i = 0; i < count; ++i)
                {
                    const float existing = ReadRowFloatField(rowIndex, i + 1, 0.0f);
                    if (existing != values[i])
                        return false;
                }

                return true;
            };

        auto RowEqualsIntArray = [&](int rowIndex, const std::int32_t* values, int count) -> bool
            {
                if (!LuaIsTable(L, rowIndex))
                    return false;

                for (int i = 0; i < count; ++i)
                {
                    const std::int32_t existing = ReadRowIntField(rowIndex, i + 1, 0);
                    if (existing != values[i])
                        return false;
                }

                return true;
            };

        auto RowEqualsSingleStringRow = [&](int rowIndex, const std::string& value) -> bool
            {
                if (!LuaIsTable(L, rowIndex))
                    return false;

                return ReadRowStringField(rowIndex, 1) == value;
            };

        auto AppendEmptyRow = [&](int tableIndex, int fieldCount) -> int
            {
                const int tableAbs = AbsIndex(tableIndex);
                int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbs));

                g_Deps.LuaCreateTable(L, fieldCount, 0);
                const int rowIndex = g_Deps.GetLuaTop(L);

                ++rowCount;
                g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
                g_Deps.LuaPushValue(L, rowIndex);
                g_Deps.LuaSetTable(L, tableAbs);

                PopOne(L);
                return rowCount;
            };

        auto EnsureIdRow = [&](int tableIndex, std::int32_t idValue, int fieldCount) -> int
            {
                const int tableAbs = AbsIndex(tableIndex);
                const int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbs));

                for (int i = 1; i <= rowCount; ++i)
                {
                    g_Deps.LuaRawGetI(L, tableAbs, i);

                    bool found = false;
                    if (LuaIsTable(L, -1))
                    {
                        const std::int32_t currentId = ReadRowIntField(-1, 1, 0);
                        found = (currentId == idValue);
                    }

                    PopOne(L);

                    if (found)
                        return i;
                }

                const int newRow = AppendEmptyRow(tableAbs, fieldCount);

                g_Deps.LuaRawGetI(L, tableAbs, newRow);
                WriteRowIntField(-1, 1, idValue);
                PopOne(L);

                return newRow;
            };

        auto UpsertAnonymousFloatRow = [&](int tableIndex, const float* values, int count) -> int
            {
                const int tableAbs = AbsIndex(tableIndex);
                int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbs));

                for (int i = 1; i <= rowCount; ++i)
                {
                    g_Deps.LuaRawGetI(L, tableAbs, i);

                    bool match = false;
                    if (LuaIsTable(L, -1))
                        match = RowEqualsFloatArray(-1, values, count);

                    PopOne(L);

                    if (match)
                        return i - 1;
                }

                g_Deps.LuaCreateTable(L, count, 0);
                const int rowIndex = g_Deps.GetLuaTop(L);

                for (int i = 0; i < count; ++i)
                    WriteRowFloatField(rowIndex, i + 1, values[i]);

                ++rowCount;
                g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
                g_Deps.LuaPushValue(L, rowIndex);
                g_Deps.LuaSetTable(L, tableAbs);

                PopOne(L);
                return rowCount - 1;
            };

        auto UpsertAnonymousIntRow = [&](int tableIndex, const std::int32_t* values, int count) -> int
            {
                const int tableAbs = AbsIndex(tableIndex);
                int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbs));

                for (int i = 1; i <= rowCount; ++i)
                {
                    g_Deps.LuaRawGetI(L, tableAbs, i);

                    bool match = false;
                    if (LuaIsTable(L, -1))
                        match = RowEqualsIntArray(-1, values, count);

                    PopOne(L);

                    if (match)
                        return i - 1;
                }

                g_Deps.LuaCreateTable(L, count, 0);
                const int rowIndex = g_Deps.GetLuaTop(L);

                for (int i = 0; i < count; ++i)
                    WriteRowIntField(rowIndex, i + 1, values[i]);

                ++rowCount;
                g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
                g_Deps.LuaPushValue(L, rowIndex);
                g_Deps.LuaSetTable(L, tableAbs);

                PopOne(L);
                return rowCount - 1;
            };

        auto UpsertAnonymousSingleStringRow = [&](int tableIndex, const std::string& value) -> int
            {
                const int tableAbs = AbsIndex(tableIndex);
                int rowCount = static_cast<int>(g_Deps.LuaObjLen(L, tableAbs));

                for (int i = 1; i <= rowCount; ++i)
                {
                    g_Deps.LuaRawGetI(L, tableAbs, i);

                    bool match = false;
                    if (LuaIsTable(L, -1))
                        match = RowEqualsSingleStringRow(-1, value);

                    PopOne(L);

                    if (match)
                        return i - 1;
                }

                g_Deps.LuaCreateTable(L, 1, 0);
                const int rowIndex = g_Deps.GetLuaTop(L);
                WriteRowStringField(rowIndex, 1, value);

                ++rowCount;
                g_Deps.PushLuaNumber(L, static_cast<float>(rowCount));
                g_Deps.LuaPushValue(L, rowIndex);
                g_Deps.LuaSetTable(L, tableAbs);

                PopOne(L);
                return rowCount - 1;
            };

        std::vector<std::int32_t> receiverBaseActual(g_ReceiverBaseRegistry.rows.size(), -1);
        std::vector<std::int32_t> receiverWobblingActual(g_ReceiverWobblingRegistry.rows.size(), -1);
        std::vector<std::int32_t> receiverSystemActual(g_ReceiverSystemRegistry.rows.size(), -1);
        std::vector<std::int32_t> receiverSoundActual(g_ReceiverSoundRegistry.rows.size(), -1);
        std::vector<std::int32_t> barrelBaseActual(g_BarrelBaseRegistry.rows.size(), -1);
        std::vector<std::int32_t> bulletBaseActual(g_BulletBaseRegistry.rows.size(), -1);
        std::vector<std::int32_t> bulletTrailActual(g_BulletTrailRegistry.rows.size(), -1);

        int tableIndex = 0;

        if (PushRootFieldTable("receiverParamSetsBase", tableIndex))
        {
            for (std::size_t i = 0; i < g_ReceiverBaseRegistry.rows.size(); ++i)
                receiverBaseActual[i] = UpsertAnonymousFloatRow(tableIndex, g_ReceiverBaseRegistry.rows[i].v, 8);

            PopOne(L);
        }

        if (PushRootFieldTable("receiverParamSetsWobbling", tableIndex))
        {
            for (std::size_t i = 0; i < g_ReceiverWobblingRegistry.rows.size(); ++i)
                receiverWobblingActual[i] = UpsertAnonymousFloatRow(tableIndex, g_ReceiverWobblingRegistry.rows[i].v, 7);

            PopOne(L);
        }

        if (PushRootFieldTable("receiverParamSetsSystem", tableIndex))
        {
            for (std::size_t i = 0; i < g_ReceiverSystemRegistry.rows.size(); ++i)
                receiverSystemActual[i] = UpsertAnonymousIntRow(tableIndex, g_ReceiverSystemRegistry.rows[i].v, 12);

            PopOne(L);
        }

        if (PushRootFieldTable("receiverParamSetsSound", tableIndex))
        {
            for (std::size_t i = 0; i < g_ReceiverSoundRegistry.rows.size(); ++i)
                receiverSoundActual[i] = UpsertAnonymousSingleStringRow(tableIndex, g_ReceiverSoundRegistry.rows[i].sound);

            PopOne(L);
        }

        if (PushRootFieldTable("barrelParamSetsBase", tableIndex))
        {
            for (std::size_t i = 0; i < g_BarrelBaseRegistry.rows.size(); ++i)
                barrelBaseActual[i] = UpsertAnonymousFloatRow(tableIndex, g_BarrelBaseRegistry.rows[i].v, 7);

            PopOne(L);
        }

        if (PushRootFieldTable("bulletParamSetsBase", tableIndex))
        {
            for (std::size_t i = 0; i < g_BulletBaseRegistry.rows.size(); ++i)
                bulletBaseActual[i] = UpsertAnonymousFloatRow(tableIndex, g_BulletBaseRegistry.rows[i].v, 12);

            PopOne(L);
        }

        if (PushRootFieldTable("bulletTrailEffectList", tableIndex))
        {
            for (std::size_t i = 0; i < g_BulletTrailRegistry.rows.size(); ++i)
                bulletTrailActual[i] = UpsertAnonymousSingleStringRow(tableIndex, g_BulletTrailRegistry.rows[i].path);

            PopOne(L);
        }

        if (PushRootFieldTable("receiver", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomReceivers)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.receiverId, 6);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.receiverId);
                WriteRowIntField(rowIndex, 2, def.attackId);

                if (def.base)
                {
                    const int regIdx = ResolveReceiverBaseIndex(*def.base);
                    if (regIdx >= 0 && regIdx < static_cast<int>(receiverBaseActual.size()))
                        WriteRowIntField(rowIndex, 3, receiverBaseActual[regIdx]);
                }

                if (def.wobbling)
                {
                    const int regIdx = ResolveReceiverWobblingIndex(*def.wobbling);
                    if (regIdx >= 0 && regIdx < static_cast<int>(receiverWobblingActual.size()))
                        WriteRowIntField(rowIndex, 4, receiverWobblingActual[regIdx]);
                }

                if (def.system)
                {
                    const int regIdx = ResolveReceiverSystemIndex(*def.system);
                    if (regIdx >= 0 && regIdx < static_cast<int>(receiverSystemActual.size()))
                        WriteRowIntField(rowIndex, 5, receiverSystemActual[regIdx]);
                }

                if (def.sound)
                {
                    const int regIdx = ResolveReceiverSoundIndex(*def.sound);
                    if (regIdx >= 0 && regIdx < static_cast<int>(receiverSoundActual.size()))
                        WriteRowIntField(rowIndex, 6, receiverSoundActual[regIdx]);
                }

                Log("[EquipParams] Applied receiver receiverId=0x%X\n", def.receiverId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("barrel", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomBarrels)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.barrelId, 7);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.barrelId);

                if (def.base)
                {
                    const int regIdx = ResolveBarrelBaseIndex(*def.base);
                    if (regIdx >= 0 && regIdx < static_cast<int>(barrelBaseActual.size()))
                        WriteRowIntField(rowIndex, 2, barrelBaseActual[regIdx]);
                }

                WriteRowIntField(rowIndex, 3, def.barrelLength);
                WriteRowIntField(rowIndex, 4, def.hasScopeMount);
                WriteRowIntField(rowIndex, 5, def.unk2);
                WriteRowIntField(rowIndex, 6, def.hasSideMount);
                WriteRowIntField(rowIndex, 7, def.hasUnderMount);

                Log("[EquipParams] Applied barrel barrelId=0x%X\n", def.barrelId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("magazine", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomMagazines)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.ammoId, 5);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.ammoId);
                WriteRowIntField(rowIndex, 2, def.eqpAmmoId);
                WriteRowIntField(rowIndex, 3, def.rank);
                WriteRowIntField(rowIndex, 4, def.magSize);
                WriteRowIntField(rowIndex, 5, def.bulletId);

                Log("[EquipParams] Applied magazine ammoId=0x%X magSize=%d\n", def.ammoId, def.magSize);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("muzzleOption", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomMuzzleOptions)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.muzzleOptionId, 4);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.muzzleOptionId);
                WriteRowFloatField(rowIndex, 2, def.grouping);
                WriteRowIntField(rowIndex, 3, def.durability);
                WriteRowIntField(rowIndex, 4, def.suppressor);

                Log("[EquipParams] Applied muzzleOption muzzleOptionId=0x%X\n", def.muzzleOptionId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("option", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomOptions)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.optionId, 3);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.optionId);
                WriteRowIntField(rowIndex, 2, def.isLaser);
                WriteRowIntField(rowIndex, 3, def.isLight);

                Log("[EquipParams] Applied option optionId=0x%X\n", def.optionId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("sight", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomSights)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.sightId, 10);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.sightId);
                WriteRowIntField(rowIndex, 2, def.zoom1);
                WriteRowIntField(rowIndex, 3, def.zoom2);
                WriteRowIntField(rowIndex, 4, def.zoom3);
                WriteRowIntField(rowIndex, 5, def.scopeUiId);
                WriteRowIntField(rowIndex, 6, def.booster);
                WriteRowIntField(rowIndex, 7, def.nvg);
                WriteRowIntField(rowIndex, 8, def.builtIn);
                WriteRowIntField(rowIndex, 9, def.rangeFinder);
                WriteRowIntField(rowIndex, 10, def.bdc);

                Log("[EquipParams] Applied sight sightId=0x%X\n", def.sightId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("stock", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomStocks)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.stockId, 3);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.stockId);
                WriteRowFloatField(rowIndex, 2, def.field2);
                WriteRowFloatField(rowIndex, 3, def.field3);

                Log("[EquipParams] Applied stock stockId=0x%X\n", def.stockId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("underBarrel", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomUnderBarrels)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.underBarrelId, 4);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.underBarrelId);
                WriteRowIntField(rowIndex, 2, def.field2);
                WriteRowIntField(rowIndex, 3, def.field3);
                WriteRowIntField(rowIndex, 4, def.field4);

                Log("[EquipParams] Applied underBarrel underBarrelId=0x%X\n", def.underBarrelId);
                PopOne(L);
            }

            PopOne(L);
        }

        if (PushRootFieldTable("bullet", tableIndex))
        {
            const int tableAbs = AbsIndex(tableIndex);

            for (const auto& def : g_CustomBullets)
            {
                const int rowNumber = EnsureIdRow(tableAbs, def.bulletId, 13);
                g_Deps.LuaRawGetI(L, tableAbs, rowNumber);
                const int rowIndex = g_Deps.GetLuaTop(L);

                WriteRowIntField(rowIndex, 1, def.bulletId);

                if (def.base)
                {
                    const int regIdx = ResolveBulletBaseIndex(*def.base);
                    if (regIdx >= 0 && regIdx < static_cast<int>(bulletBaseActual.size()))
                        WriteRowIntField(rowIndex, 2, bulletBaseActual[regIdx]);
                }

                if (def.trail)
                {
                    const int regIdx = ResolveBulletTrailIndex(*def.trail);
                    if (regIdx >= 0 && regIdx < static_cast<int>(bulletTrailActual.size()))
                        WriteRowIntField(rowIndex, 3, bulletTrailActual[regIdx]);
                }

                if (def.bullet)
                {
                    WriteRowFloatField(rowIndex, 4, def.bullet->f4);
                    WriteRowIntField(rowIndex, 5, def.bullet->f5);
                    WriteRowIntField(rowIndex, 6, def.bullet->f6);
                    WriteRowIntField(rowIndex, 7, def.bullet->f7);
                    WriteRowIntField(rowIndex, 8, def.bullet->f8);
                    WriteRowIntField(rowIndex, 9, def.bullet->f9);
                    WriteRowIntField(rowIndex, 10, def.bullet->f10);
                    WriteRowIntField(rowIndex, 11, def.bullet->f11);
                    WriteRowIntField(rowIndex, 12, def.bullet->flag1);
                    WriteRowIntField(rowIndex, 13, def.bullet->flagBits);
                }

                Log("[EquipParams] Applied bullet bulletId=0x%X\n", def.bulletId);
                PopOne(L);
            }

            PopOne(L);
        }

        g_Deps.LuaSetTop(L, entryTop);
    }

    bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook()
    {
        Log("[EquipParams] no-op install; hook is owned by GunBasic module\n");
        return true;
    }

    bool Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl2_Hook()
    {
        Log("[EquipParams] no-op uninstall; hook is owned by GunBasic module\n");
        return true;
    }
}