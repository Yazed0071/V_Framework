#include "pch.h"

#include "BuddyOutfitRegistry.h"

#include <Windows.h>
#include <intrin.h>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace buddyoutfit
{
    namespace
    {
        constexpr std::size_t kKindCount = static_cast<std::size_t>(Kind::Count);
        constexpr std::int32_t kMaxValue = 0xFE;

        struct KindInfo
        {
            const char*  name;
            const char*  spaceTag;
            std::uint8_t buddyType;
            bool         isSuit;
            std::uint8_t firstCustom;
        };

        constexpr KindInfo kKinds[kKindCount] =
        {
            { "quiet costume", "BuddyQuietCostume", kBuddyTypeQuiet, true,  6 },
            { "D-Horse tack",  "BuddyHorseTack",    kBuddyTypeHorse, false, 7 },
            { "D-Dog gear",    "BuddyDogGear",      kBuddyTypeDog,   false, 8 },
            { "quiet weapon",  "BuddyQuietWeapon",  kBuddyTypeQuiet, false, 5 },
        };

        alignas(64) std::uint32_t g_ValueBits[kKindCount][kValueBitWords] = {};
        alignas(64) std::uint32_t g_DevelopIdBits[kKindCount][kDevelopIdBitWords] = {};
        alignas(64) std::uint32_t g_BruiseBits[kValueBitWords] = {};

        constexpr std::uint16_t kTackDamageRate[kHorseVanillaTackCount] =
            { kHorseKeepDamageRate, kHorseKeepDamageRate, 179, 150, 100, kHorseKeepDamageRate, kHorseKeepDamageRate };
        constexpr float         kTackGallopRate[kHorseVanillaTackCount] = { 1.0f, 1.0f, 0.98f, 0.96f, 0.94f, 1.0f, 1.0f };
        constexpr std::uint16_t kTackBodyAttack[kHorseVanillaTackCount] = { 0x69, 0x69, 0x6A, 0x6B, 0x6C, 0x69, 0x69 }; // ATK_HorseBodyAttack, then Mil1..Mil3
        constexpr std::uint8_t  kTackSoundCode[kHorseVanillaTackCount]  = { 0, 7, 1, 2, 3, 5, 4 };

        struct HorseTackTables
        {
            alignas(64) std::uint16_t damage[256];
            alignas(64) float         gallop[256];
            alignas(64) std::uint16_t attack[256];
            alignas(64) std::uint8_t  sound[256];

            HorseTackTables()
            {
                for (int v = 0; v < 256; ++v)
                {
                    damage[v] = kTackDamageRate[0];
                    gallop[v] = kTackGallopRate[0];
                    attack[v] = kTackBodyAttack[0];
                    sound[v]  = kTackSoundCode[0];
                }
            }
        };

        HorseTackTables g_HorseTack;

        void PublishHorseEffects(const Entry& e, bool on)
        {
            const HorseTackEffects& fx = e.horse;
            const std::uint8_t sound = on && fx.soundType >= 0 && fx.soundType < kHorseVanillaTackCount
                ? static_cast<std::uint8_t>(fx.soundType) : 0;
            const std::uint8_t v = e.value;
            g_HorseTack.damage[v] = on && fx.damageRate >= 0 ? static_cast<std::uint16_t>(fx.damageRate) : kTackDamageRate[0];
            g_HorseTack.gallop[v] = on && fx.gallopSpeedRate > 0.0f ? fx.gallopSpeedRate : kTackGallopRate[0];
            g_HorseTack.attack[v] = on && fx.bodyAttackId >= 0 ? static_cast<std::uint16_t>(fx.bodyAttackId) : kTackBodyAttack[0];
            g_HorseTack.sound[v]  = kTackSoundCode[sound];
        }

        constexpr std::uint32_t kGearCommand[kDogVanillaGearCount] = { 0xB, 0xF, 0xD, 0xE, 0xC, 0xB, 0xB };
        constexpr float kNakedEscapeDistance     = 30.0f;
        constexpr float kNakedHitRetreatDistance = 20.0f;

        struct DogGearTables
        {
            alignas(64) std::uint32_t command[256];
            alignas(64) std::uint8_t  commandGear[256] = {};
            alignas(64) float         damage[256];
            alignas(64) float         escape[256];
            alignas(64) float         retreat[256];
            alignas(64) float         retarget[256];
            alignas(64) std::uint32_t holdsBite[kValueBitWords];
            alignas(64) std::uint32_t noFlinch[kValueBitWords] = {};
            alignas(64) std::uint32_t hitEffect[kValueBitWords] = {};
            alignas(64) std::uint32_t hitSound[kValueBitWords] = {};
            alignas(64) std::uint32_t threat[kValueBitWords] = {};

            DogGearTables()
            {
                for (int v = 0; v < 256; ++v)
                {
                    command[v]  = kGearCommand[0];
                    damage[v]   = 1.0f;
                    escape[v]   = kNakedEscapeDistance;
                    retreat[v]  = kNakedHitRetreatDistance;
                    retarget[v] = 0.0f;
                }
                for (std::uint32_t& w : holdsBite)
                    w = 0xFFFFFFFFu;
            }
        };

        DogGearTables g_DogGear;

        std::mutex                                g_Mutex;
        std::vector<Entry>                        g_Entries;
        std::unordered_map<std::uint16_t, size_t> g_ByDevelopId;

        const KindInfo& InfoOf(Kind kind)
        {
            return kKinds[static_cast<std::size_t>(kind)];
        }

        void SetBit(std::uint32_t* words, std::uint32_t bit, bool on)
        {
            volatile long* word = reinterpret_cast<volatile long*>(&words[bit >> 5]);
            const long mask = static_cast<long>(1u << (bit & 31));
            if (on)
                _InterlockedOr(word, mask);
            else
                _InterlockedAnd(word, ~mask);
        }

        void PublishDogEffects(const Entry& e, bool on)
        {
            const DogGearEffects& fx = e.dog;
            const std::uint8_t v = e.value;
            const std::int32_t commandGear = on && fx.commandType >= 0 && fx.commandType < kDogVanillaGearCount ? fx.commandType : 0;
            g_DogGear.command[v]  = kGearCommand[commandGear];
            g_DogGear.commandGear[v] = static_cast<std::uint8_t>(commandGear);
            g_DogGear.damage[v]   = on && fx.damageRate >= 0.0f ? fx.damageRate : 1.0f;
            g_DogGear.escape[v]   = on && fx.escapeDistance >= 0.0f ? fx.escapeDistance : kNakedEscapeDistance;
            g_DogGear.retreat[v]  = on && fx.hitRetreatDistance >= 0.0f ? fx.hitRetreatDistance : kNakedHitRetreatDistance;
            g_DogGear.retarget[v] = on && fx.retargetRange >= 0.0f ? fx.retargetRange : 0.0f;
            SetBit(g_DogGear.holdsBite, v, commandGear == 0);   // the soldier only stays pinned under NAKED's bite
            SetBit(g_DogGear.noFlinch, v, on && fx.bulletFlinch == 0);
            SetBit(g_DogGear.hitEffect, v, on && fx.armorHitEffect == 1);
            SetBit(g_DogGear.hitSound, v, on && fx.armorHitSound == 1);
            SetBit(g_DogGear.threat, v, on && fx.soldierThreat == 1);
        }

        void PublishBits(const Entry& e, bool on)
        {
            const std::size_t k = static_cast<std::size_t>(e.kind);
            SetBit(g_ValueBits[k], e.value, on);
            SetBit(g_DevelopIdBits[k], e.developId, on);
            if (e.kind == Kind::QuietCostume)
                SetBit(g_BruiseBits, e.value, on && (e.bruise || e.bruiseFv2 != 0));
            if (e.kind == Kind::HorseTack)
                PublishHorseEffects(e, on);
            if (e.kind == Kind::DogGear)
                PublishDogEffects(e, on);
        }

        void RebuildDevelopIndex_NoLock()
        {
            g_ByDevelopId.clear();
            for (std::size_t i = 0; i < g_Entries.size(); ++i)
                g_ByDevelopId[g_Entries[i].developId] = i;
        }
    }

    std::uint8_t BuddyTypeOf(Kind kind) { return InfoOf(kind).buddyType; }
    bool         IsSuitKind(Kind kind) { return InfoOf(kind).isSuit; }
    std::uint8_t FirstCustomValue(Kind kind) { return InfoOf(kind).firstCustom; }
    const char*  KindName(Kind kind) { return InfoOf(kind).name; }

    bool KindFromBuddyType(std::uint8_t buddyType, bool isSuit, Kind* out)
    {
        for (std::size_t k = 0; k < kKindCount; ++k)
        {
            if (kKinds[k].buddyType == buddyType && kKinds[k].isSuit == isSuit)
            {
                if (out) *out = static_cast<Kind>(k);
                return true;
            }
        }
        return false;
    }

    bool Register(Entry& entry)
    {
        if (entry.key.empty() || entry.developId == 0 || entry.kind >= Kind::Count)
            return false;

        const KindInfo& info = InfoOf(entry.kind);
        std::int32_t value = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                info.spaceTag, entry.key.c_str(), info.firstCustom, value))
            return false;
        if (value < info.firstCustom || value > kMaxValue)
        {
            LogError("RegisterBuddyOutfit '%s': no free %s value left (%d..%d), not registered",
                entry.key.c_str(), info.name, info.firstCustom, kMaxValue);
            return false;
        }
        entry.value = static_cast<std::uint8_t>(value);

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto it = g_Entries.begin(); it != g_Entries.end(); ++it)
        {
            if (it->key == entry.key || it->developId == entry.developId)
            {
                PublishBits(*it, false);
                g_Entries.erase(it);
                break;
            }
        }
        g_Entries.push_back(entry);
        RebuildDevelopIndex_NoLock();
        PublishBits(entry, true);
        return true;
    }

    void NoteDevelopRow(const std::string& key, std::uint16_t developId,
                        std::int32_t equipId, std::int32_t developType)
    {
        if (key.empty() || developId == 0)
            return;

        constexpr std::int32_t kQuietSr010 = 59;
        constexpr std::int32_t kQuietSr030 = 61;
        const bool quietWeapon = developType == kDevTypeQuiet && equipId > 0 && equipId <= 0xFFFF
                              && !(equipId >= kQuietSr010 && equipId <= kQuietSr030);
        if (!quietWeapon)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (auto it = g_Entries.begin(); it != g_Entries.end(); ++it)
            {
                if (it->key == key && it->kind == Kind::QuietWeapon)
                {
                    PublishBits(*it, false);
                    g_Entries.erase(it);
                    RebuildDevelopIndex_NoLock();
                    break;
                }
            }
            return;
        }

        Entry old;
        if (TryGetByKey(key.c_str(), &old) && old.kind == Kind::QuietWeapon
            && old.developId == developId && old.equipId == equipId)
            return;

        Entry e;
        e.key = key;
        e.kind = Kind::QuietWeapon;
        e.developId = developId;
        e.equipId = static_cast<std::uint16_t>(equipId);
        if (Register(e))
            LogInfo("Quiet weapon %s developId:%u equipId:%d value:%u", key.c_str(),
                static_cast<unsigned>(developId), equipId, static_cast<unsigned>(e.value));
    }

    bool TryGetByDevelopId(std::uint16_t developId, Entry* out)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_ByDevelopId.find(developId);
        if (it == g_ByDevelopId.end())
            return false;
        if (out) *out = g_Entries[it->second];
        return true;
    }

    bool TryGetByValue(Kind kind, std::uint8_t value, Entry* out)
    {
        const std::size_t k = static_cast<std::size_t>(kind);
        if (k >= kKindCount || !((g_ValueBits[k][value >> 5] >> (value & 31)) & 1u))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const Entry& e : g_Entries)
        {
            if (e.kind == kind && e.value == value)
            {
                if (out) *out = e;
                return true;
            }
        }
        return false;
    }

    bool TryGetByKey(const char* key, Entry* out)
    {
        if (!key || !key[0])
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const Entry& e : g_Entries)
        {
            if (e.key == key)
            {
                if (out) *out = e;
                return true;
            }
        }
        return false;
    }

    bool HasEntries()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return !g_Entries.empty();
    }

    std::uint32_t* ValueBits(Kind kind) { return g_ValueBits[static_cast<std::size_t>(kind)]; }
    std::uint32_t* DevelopIdBits(Kind kind) { return g_DevelopIdBits[static_cast<std::size_t>(kind)]; }
    std::uint32_t* BruiseValueBits() { return g_BruiseBits; }

    std::uint16_t* HorseDamageRates() { return g_HorseTack.damage; }
    float*         HorseGallopRates() { return g_HorseTack.gallop; }
    std::uint16_t* HorseBodyAttackIds() { return g_HorseTack.attack; }
    std::uint8_t*  HorseSoundCodes() { return g_HorseTack.sound; }

    std::uint32_t* DogCommands() { return g_DogGear.command; }
    std::uint8_t*  DogCommandGears() { return g_DogGear.commandGear; }
    float*         DogDamageRates() { return g_DogGear.damage; }
    float*         DogEscapeDistances() { return g_DogGear.escape; }
    float*         DogHitRetreatDistances() { return g_DogGear.retreat; }
    float*         DogRetargetRanges() { return g_DogGear.retarget; }
    std::uint32_t* DogHoldsBiteBits() { return g_DogGear.holdsBite; }
    std::uint32_t* DogNoBulletFlinchBits() { return g_DogGear.noFlinch; }
    std::uint32_t* DogArmorHitEffectBits() { return g_DogGear.hitEffect; }
    std::uint32_t* DogArmorHitSoundBits() { return g_DogGear.hitSound; }
    std::uint32_t* DogSoldierThreatBits() { return g_DogGear.threat; }

    std::uint8_t* ReadScriptVars()
    {
        using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
        static GetQuarkSystemTable_t s_GetTable = nullptr;
        if (!s_GetTable)
            s_GetTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!s_GetTable)
            return nullptr;
        __try
        {
            std::uint8_t* table = s_GetTable();
            if (!table)
                return nullptr;
            std::uint8_t* holder = *reinterpret_cast<std::uint8_t**>(table + 0x98); // table+0x98 -> +0x10 is the vars block
            return holder ? *reinterpret_cast<std::uint8_t**>(holder + 0x10) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
}
