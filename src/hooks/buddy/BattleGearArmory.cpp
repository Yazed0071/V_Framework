#include <pch.h>

#include "BattleGearArmory.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include "AddressSet.h"
#include "BattleGearTexts.h"
#include "BattleGearWeapons.h"
#include "EquipPartParams.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using battleGearArmory::MainWeapon;
    using battleGearArmory::RoofGun;
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using RowOf_t               = std::uint64_t(__fastcall*)(void* develop, std::uint16_t developId);
    using RowDeployCost_t       = std::uint32_t(__fastcall*)(void* develop, std::uint16_t row);
    using GetLangText_t         = const char*(__fastcall*)(std::uint64_t langId);

    constexpr const char*   kValueSpace     = "BattleGearMainWeapon";
    constexpr const char*   kRoofValueSpace = "BattleGearRoofGun";
    constexpr const char*   kPageSpace      = "BattleGearPage";   // V Framework's saved picks: "Slot", "Slot<n>:MainWeapon" ...
    constexpr std::int32_t  kLastValue      = 0xFE;
    constexpr std::uint16_t kBodyRow        = 40000;   // the game's own Battle Gear rows: only their deploy costs are used
    constexpr std::uint16_t kRailGunRow     = 41000;
    constexpr std::uint16_t kHMissileRow    = 42000;
    constexpr std::uint16_t kStunGunRow     = 31004;   // develop ids of the D-Walker stun gun, missile pod and walker ZE'EV
    constexpr std::uint16_t kMissilePodRow  = 30004;
    constexpr std::uint16_t kZeevRow        = 29008;

    constexpr std::size_t   kTableApplication  = 0x98;                   // quark system table: the application's systems
    constexpr std::size_t   kApplicationUiData = 0x110;                  // the UI's shared data manager
    constexpr std::size_t   kUiDataDevelop     = 0xAC8;                  // its R&D table
    constexpr std::size_t   kRowOfSlot         = 0xE0 / sizeof(void*);   // R&D table vtable: the row of a develop id, 0x400 for none,
    constexpr std::size_t   kDeployCostSlot    = 0x3A8 / sizeof(void*);  // and a row's deploy cost
    constexpr std::uint16_t kNoRow             = 0x400;
    constexpr std::size_t   kRecords           = 0x08;                   // 0x68-byte records follow the table's vtable
    constexpr std::size_t   kRecordSize        = 0x68;
    constexpr std::size_t   kRecordName        = 0x20;                   // name lang id
    constexpr std::size_t   kRecordIcon        = 0x60;                   // icon texture path

    struct FireType
    {
        const char*   name;
        std::uint8_t  type;
        std::uint16_t developId;
    };

    constexpr FireType kFireTypes[] = { { "RAIL_GUN", battleGearArmory::kRailGun, kStunGunRow },
                                        { "H_MISSILE", battleGearArmory::kHMissile, kMissilePodRow } };

    std::mutex                      g_Mutex;
    std::vector<MainWeapon>         g_Custom;
    std::vector<RoofGun>            g_CustomRoof;
    std::unordered_set<std::string> g_MissingPackWarned;
    MainWeapon                      g_Latched;
    std::atomic<std::uint16_t>      g_FittedRegisteredRow{ 0 };   // develop id of the latched gun when a mod registered it
    std::atomic<std::uint16_t>      g_FittedRoofRow{ 0 };
    std::atomic<std::uint8_t>       g_PagePick{ 0 };
    std::atomic<std::uint8_t>       g_ScriptPick{ 0 };
    std::atomic<std::uint8_t>       g_RoofPagePick{ 0 };
    std::atomic<std::uint8_t>       g_RoofScriptPick{ 0 };
    std::atomic<bool>               g_RoofScriptPicked{ false };
    battleGearArmory::Loadout       g_Slots[battleGearArmory::kLoadoutSlots];
    std::atomic<int>                g_CurrentSlot{ 0 };

    // the latched guns' numbers, as the walker's frame reads them
    std::atomic<std::uint16_t>      g_RoofAttack{ battleGearArmory::kRoofMgAttackId };
    std::atomic<std::uint8_t>       g_RoofBullet{ battleGearArmory::kRoofMgBulletId };
    std::atomic<float>              g_RoofInterval{ battleGearArmory::kRoofMgFireInterval };
    std::atomic<std::uint16_t>      g_MainLevelAttack[3];   // per charge level, 0: the built-in gun's
    std::atomic<std::uint8_t>       g_MainBullet{ 0 };
    std::atomic<bool>               g_MainHasMuzzle{ false };
    std::atomic<bool>               g_MainHasMount{ false };
    std::atomic<float>              g_MainMuzzle[3];
    std::atomic<float>              g_MainMount[3];

    std::uint8_t* DevelopTableSEH()
    {
        static GetQuarkSystemTable_t getTable = nullptr;
        if (!getTable)
            getTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getTable)
            return nullptr;
        __try
        {
            const std::uint8_t* table = getTable();
            const std::uint8_t* application = table ? *reinterpret_cast<std::uint8_t* const*>(table + kTableApplication) : nullptr;
            const std::uint8_t* uiData = application ? *reinterpret_cast<std::uint8_t* const*>(application + kApplicationUiData) : nullptr;
            return uiData ? *reinterpret_cast<std::uint8_t* const*>(uiData + kUiDataDevelop) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint16_t RowOfSEH(std::uint8_t* develop, std::uint16_t developId)
    {
        __try
        {
            return static_cast<std::uint16_t>(reinterpret_cast<RowOf_t>((*reinterpret_cast<void***>(develop))[kRowOfSlot])(develop, developId));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoRow;
        }
    }

    std::uint32_t RowDeployCostSEH(std::uint8_t* develop, std::uint16_t row)
    {
        __try
        {
            return reinterpret_cast<RowDeployCost_t>((*reinterpret_cast<void***>(develop))[kDeployCostSlot])(develop, row);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint64_t RecordFieldSEH(const std::uint8_t* develop, std::uint16_t row, std::size_t field)
    {
        __try
        {
            return *reinterpret_cast<const std::uint64_t*>(develop + kRecords + static_cast<std::size_t>(row) * kRecordSize + field);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint64_t RecordField(std::uint16_t developId, std::size_t field)
    {
        std::uint8_t* develop = developId ? DevelopTableSEH() : nullptr;
        const std::uint16_t row = develop ? RowOfSEH(develop, developId) : kNoRow;
        return row != kNoRow ? RecordFieldSEH(develop, row, field) : 0;
    }

    bool LangTextShowsSEH(GetLangText_t getLangText, std::uint64_t langId)
    {
        __try
        {
            const char* text = getLangText(langId);
            return text && text[0];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    MainWeapon BuiltIn(const FireType& f)
    {
        MainWeapon w;
        w.key = f.name;
        w.value = f.type;
        w.fireType = f.type;
        w.developId = f.developId;
        return w;
    }

    RoofGun BuiltInRoofGun()
    {
        RoofGun g;
        g.developId = kZeevRow;
        return g;
    }

    const FireType* FireTypeOf(std::uint8_t type)
    {
        for (const FireType& f : kFireTypes)
            if (f.type == type)
                return &f;
        return nullptr;
    }

    void LatchRoofGun()
    {
        const RoofGun g = battleGearArmory::PickedRoofGun();
        g_FittedRoofRow.store(g.BuiltIn() ? 0 : g.developId, std::memory_order_relaxed);
        g_RoofAttack.store(g.attackId, std::memory_order_relaxed);
        g_RoofBullet.store(g.bulletId, std::memory_order_relaxed);
        g_RoofInterval.store(g.fireInterval, std::memory_order_relaxed);
    }

    // a weaker charge carries less of the full charge's impact force, as the built-in rail gun's levels do; without custom damage
    // ids every level deals the gun's own row
    void LatchMainNumbers(const MainWeapon& w)
    {
        constexpr float kLevelForce[3] = { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
        for (int level = 0; level < 3; ++level)
        {
            std::uint16_t id = w.attackId;
            if (id && level < 2)
            {
                const std::string name = w.key + "_Level" + std::to_string(level + 1);
                if (const int scaled = EquipParam_DeclareScaledDamage(name.c_str(), w.attackId, 1.0f, kLevelForce[level], 0))
                    id = static_cast<std::uint16_t>(scaled);
            }
            g_MainLevelAttack[level].store(id, std::memory_order_relaxed);
        }
        g_MainBullet.store(w.bulletId, std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
        {
            g_MainMuzzle[i].store(w.muzzle[i], std::memory_order_relaxed);
            g_MainMount[i].store(w.mount[i], std::memory_order_relaxed);
        }
        g_MainHasMuzzle.store(w.hasMuzzle, std::memory_order_relaxed);
        g_MainHasMount.store(w.hasMount, std::memory_order_relaxed);
    }

    std::string SlotKey(int slot, const char* what)
    {
        return "Slot" + std::to_string(slot) + ":" + what;
    }

    std::uint8_t SavedByte(const std::string& key)
    {
        const std::int32_t value = V_FrameWorkState::GetPersistedConstant(kPageSpace, key.c_str());
        return value > 0 && value <= 0xFF ? static_cast<std::uint8_t>(value) : 0;
    }

    // the page picks follow the current slot; a gun not registered yet keeps its value and is looked up when the block sets up
    void ApplyCurrentSlot()
    {
        const battleGearArmory::Loadout loadout = battleGearArmory::LoadoutSlot(battleGearArmory::CurrentLoadoutSlot());
        MainWeapon main;
        const bool known = loadout.mainWeapon && battleGearArmory::TryGetByValue(loadout.mainWeapon, &main);
        g_PagePick.store(loadout.mainWeapon, std::memory_order_relaxed);
        g_RoofPagePick.store(loadout.roofGun, std::memory_order_relaxed);
        battleGearWeapons::SetPagePicks(known ? main.fireType : battleGearArmory::kRailGun,
                                        loadout.head ? loadout.head : battleGearWeapons::kSupportHead);
    }

    struct GunFrame   // the rail gun out in the world: unit axes right, up and ahead along the barrel, and the built-in muzzle
    {
        float right[3];
        float up[3];
        float ahead[3];
        float muzzle[3];
    };

    float Dot(const float* a, const float* b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    void Cross(const float* a, const float* b, float* out)
    {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }

    // up is the hull's, squared to the barrel; right follows the hull's own handedness, whichever way the turret is turned
    bool MainGunFrame(GunFrame& frame)
    {
        alignas(16) float hull[16];
        float clearance = 0.0f;
        if (!battleGearWeapons::RailGunMuzzle(frame.muzzle) || !battleGearWeapons::BarrelFor(0, frame.ahead)
            || !battleGearWeapons::HullFrame(hull, &clearance))
            return false;
        const float* hullRight = hull;
        const float* hullUp = hull + 4;
        const float* hullAhead = hull + 8;
        const float lean = Dot(hullUp, frame.ahead);
        float length = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            frame.up[i] = hullUp[i] - frame.ahead[i] * lean;
            length += frame.up[i] * frame.up[i];
        }
        length = std::sqrt(length);
        if (length < 1e-3f)
            return false;
        for (float& u : frame.up)
            u /= length;
        float hullSide[3];
        Cross(hullUp, hullAhead, hullSide);
        const float hand = Dot(hullSide, hullRight) < 0.0f ? -1.0f : 1.0f;
        Cross(frame.up, frame.ahead, frame.right);
        for (float& r : frame.right)
            r *= hand;
        return true;
    }

    void MoveAlong(const GunFrame& frame, const std::atomic<float>* offset, const float* from, float* out)
    {
        const float x = offset[0].load(std::memory_order_relaxed);
        const float y = offset[1].load(std::memory_order_relaxed);
        const float z = offset[2].load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
            out[i] = from[i] + frame.right[i] * x + frame.up[i] * y + frame.ahead[i] * z;
    }
}

namespace battleGearArmory
{
    bool Register(MainWeapon& weapon)
    {
        if (weapon.key.empty() || weapon.developId == 0 || !FireTypeOf(weapon.fireType))
            return false;
        for (const FireType& f : kFireTypes)
        {
            if (weapon.key == f.name)
            {
                LogWarn("RegisterBuddyOutfit '%s': the name of a built-in Battle Gear gun, not registered", weapon.key.c_str());
                return false;
            }
        }
        std::int32_t value = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(kValueSpace, weapon.key.c_str(), kFirstCustomValue, value))
            return false;
        if (value < kFirstCustomValue || value > kLastValue)
        {
            LogError("RegisterBuddyOutfit '%s': no free Battle Gear main weapon value left (%d..%d), not registered",
                weapon.key.c_str(), kFirstCustomValue, kLastValue);
            return false;
        }
        weapon.value = static_cast<std::uint8_t>(value);

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto it = g_Custom.begin(); it != g_Custom.end(); ++it)
        {
            if (it->key == weapon.key || it->developId == weapon.developId)
            {
                g_Custom.erase(it);
                break;
            }
        }
        for (auto it = g_CustomRoof.begin(); it != g_CustomRoof.end(); ++it)   // a key names one gun
        {
            if (it->key == weapon.key)
            {
                g_CustomRoof.erase(it);
                break;
            }
        }
        g_Custom.push_back(weapon);
        return true;
    }

    bool TryGetByValue(std::uint8_t value, MainWeapon* out)
    {
        if (const FireType* f = FireTypeOf(value))
        {
            if (out) *out = BuiltIn(*f);
            return true;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const MainWeapon& w : g_Custom)
        {
            if (w.value == value)
            {
                if (out) *out = w;
                return true;
            }
        }
        return false;
    }

    bool TryGetByKey(const char* key, MainWeapon* out)
    {
        if (!key || !key[0])
            return false;
        for (const FireType& f : kFireTypes)
        {
            if (std::strcmp(key, f.name) == 0)
            {
                if (out) *out = BuiltIn(f);
                return true;
            }
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const MainWeapon& w : g_Custom)
        {
            if (w.key == key)
            {
                if (out) *out = w;
                return true;
            }
        }
        return false;
    }

    bool TryGetByDevelopId(std::uint16_t developId, MainWeapon* out)
    {
        if (developId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const MainWeapon& w : g_Custom)
        {
            if (w.developId == developId)
            {
                if (out) *out = w;
                return true;
            }
        }
        return false;
    }

    std::vector<MainWeapon> All()
    {
        std::vector<MainWeapon> all;
        for (const FireType& f : kFireTypes)
            all.push_back(BuiltIn(f));
        std::lock_guard<std::mutex> lock(g_Mutex);
        all.insert(all.end(), g_Custom.begin(), g_Custom.end());
        return all;
    }

    const char* FireTypeName(std::uint8_t fireType)
    {
        const FireType* f = FireTypeOf(fireType);
        return f ? f->name : kFireTypes[0].name;
    }

    bool FireTypeFromName(const char* name, std::uint8_t* fireType)
    {
        if (!name)
            return false;
        for (const FireType& f : kFireTypes)
        {
            if (std::strcmp(name, f.name) == 0)
            {
                if (fireType) *fireType = f.type;
                return true;
            }
        }
        return false;
    }

    void SetPagePick(std::uint8_t value)
    {
        g_PagePick.store(value, std::memory_order_relaxed);
    }

    std::uint8_t PagePick()
    {
        return g_PagePick.load(std::memory_order_relaxed);
    }

    bool SetScriptPick(const char* key)
    {
        MainWeapon w;
        if (!TryGetByKey(key, &w))
            return false;
        g_ScriptPick.store(w.value, std::memory_order_relaxed);
        return true;
    }

    void ClearScriptPick()
    {
        g_ScriptPick.store(0, std::memory_order_relaxed);
        g_RoofScriptPicked.store(false, std::memory_order_relaxed);
        g_RoofScriptPick.store(0, std::memory_order_relaxed);
    }

    MainWeapon PagePicked()
    {
        MainWeapon w;
        const std::uint8_t page = g_PagePick.load(std::memory_order_relaxed);
        return page && TryGetByValue(page, &w) ? w : BuiltIn(kFireTypes[0]);
    }

    MainWeapon Picked()
    {
        MainWeapon w;
        const std::uint8_t script = g_ScriptPick.load(std::memory_order_relaxed);
        return script && TryGetByValue(script, &w) ? w : PagePicked();
    }

    MainWeapon Latch()
    {
        MainWeapon w = Picked();
        if (w.fpkPath && !fox::detail::PathExistsByCode(w.fpkPath))
        {
            const FireType* f = FireTypeOf(w.fireType);
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_MissingPackWarned.insert(w.key).second)
                LogWarn("Battle Gear main weapon '%s': fpkPath not installed, the %s is fitted", w.key.c_str(), f->name);
            w = BuiltIn(*f);
        }
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_Latched = w;
        }
        g_FittedRegisteredRow.store(w.BuiltIn() ? 0 : w.developId, std::memory_order_relaxed);
        LatchMainNumbers(w);
        LatchRoofGun();
        battleGearWeapons::SetPagePicks(w.fireType, 0);
        return w;
    }

    MainWeapon Latched()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Latched;
    }

    void LoadLoadoutSlots()
    {
        Loadout slots[kLoadoutSlots];
        for (int i = 0; i < kLoadoutSlots; ++i)
            slots[i] = { SavedByte(SlotKey(i, "MainWeapon")), SavedByte(SlotKey(i, "RoofGun")), SavedByte(SlotKey(i, "Head")) };
        if (!slots[0].mainWeapon && !slots[0].roofGun && !slots[0].head)
            slots[0] = { SavedByte("MainWeapon"), SavedByte("RoofGun"), SavedByte("Head") };
        const std::uint8_t current = SavedByte("Slot");
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (int i = 0; i < kLoadoutSlots; ++i)
                g_Slots[i] = slots[i];
        }
        g_CurrentSlot.store(current < kLoadoutSlots ? current : 0, std::memory_order_relaxed);
        ApplyCurrentSlot();
    }

    Loadout LoadoutSlot(int slot)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return slot >= 0 && slot < kLoadoutSlots ? g_Slots[slot] : Loadout{};
    }

    void SetLoadoutSlot(int slot, const Loadout& loadout)
    {
        if (slot < 0 || slot >= kLoadoutSlots)
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_Slots[slot] = loadout;
        }
        V_FrameWorkState::SetPersistedConstant(kPageSpace, SlotKey(slot, "MainWeapon").c_str(), loadout.mainWeapon);
        V_FrameWorkState::SetPersistedConstant(kPageSpace, SlotKey(slot, "RoofGun").c_str(), loadout.roofGun);
        V_FrameWorkState::SetPersistedConstant(kPageSpace, SlotKey(slot, "Head").c_str(), loadout.head);
        if (slot == CurrentLoadoutSlot())
            ApplyCurrentSlot();
    }

    int CurrentLoadoutSlot()
    {
        return g_CurrentSlot.load(std::memory_order_relaxed);
    }

    void SetCurrentLoadoutSlot(int slot)
    {
        if (slot < 0 || slot >= kLoadoutSlots)
            return;
        if (g_CurrentSlot.exchange(slot, std::memory_order_relaxed) != slot)
            V_FrameWorkState::SetPersistedConstant(kPageSpace, "Slot", slot);
        ApplyCurrentSlot();
    }

    std::uint32_t LoadoutDeployCost(int slot)
    {
        return LoadoutDeployCost(LoadoutSlot(slot));
    }

    std::uint32_t LoadoutDeployCost(const Loadout& loadout)
    {
        MainWeapon main;
        if (!loadout.mainWeapon || !TryGetByValue(loadout.mainWeapon, &main))
            main = BuiltIn(kFireTypes[0]);
        RoofGun roof;
        if (!TryGetRoofGunByValue(loadout.roofGun, &roof))
            roof = BuiltInRoofGun();
        const std::uint16_t mainRow = !main.BuiltIn() ? main.developId : main.fireType == kHMissile ? kHMissileRow : kRailGunRow;
        return RowDeployCost(kBodyRow) + RowDeployCost(mainRow) + (roof.BuiltIn() ? 0 : RowDeployCost(roof.developId));
    }

    bool RegisterRoofGun(RoofGun& gun)
    {
        if (gun.key.empty() || gun.developId == 0)
            return false;
        for (const FireType& f : kFireTypes)
        {
            if (gun.key == f.name)
            {
                LogWarn("RegisterBuddyOutfit '%s': the name of a built-in Battle Gear gun, not registered", gun.key.c_str());
                return false;
            }
        }
        std::int32_t value = 0;
        if (!V_FrameWorkState::ResolveOrCreateConstantValue(kRoofValueSpace, gun.key.c_str(), 1, value))
            return false;
        if (value < 1 || value > kLastValue)
        {
            LogError("RegisterBuddyOutfit '%s': no free Battle Gear roof gun value left (1..%d), not registered", gun.key.c_str(), kLastValue);
            return false;
        }
        gun.value = static_cast<std::uint8_t>(value);

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto it = g_CustomRoof.begin(); it != g_CustomRoof.end(); ++it)
        {
            if (it->key == gun.key || it->developId == gun.developId)
            {
                g_CustomRoof.erase(it);
                break;
            }
        }
        for (auto it = g_Custom.begin(); it != g_Custom.end(); ++it)   // a key names one gun
        {
            if (it->key == gun.key)
            {
                g_Custom.erase(it);
                break;
            }
        }
        g_CustomRoof.push_back(gun);
        return true;
    }

    bool TryGetRoofGunByValue(std::uint8_t value, RoofGun* out)
    {
        if (value == 0)
        {
            if (out) *out = BuiltInRoofGun();
            return true;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const RoofGun& g : g_CustomRoof)
        {
            if (g.value == value)
            {
                if (out) *out = g;
                return true;
            }
        }
        return false;
    }

    bool TryGetRoofGunByKey(const char* key, RoofGun* out)
    {
        if (!key || !key[0])
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const RoofGun& g : g_CustomRoof)
        {
            if (g.key == key)
            {
                if (out) *out = g;
                return true;
            }
        }
        return false;
    }

    bool TryGetRoofGunByDevelopId(std::uint16_t developId, RoofGun* out)
    {
        if (developId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const RoofGun& g : g_CustomRoof)
        {
            if (g.developId == developId)
            {
                if (out) *out = g;
                return true;
            }
        }
        return false;
    }

    std::vector<RoofGun> AllRoofGuns()
    {
        std::vector<RoofGun> all;
        all.push_back(BuiltInRoofGun());
        std::lock_guard<std::mutex> lock(g_Mutex);
        all.insert(all.end(), g_CustomRoof.begin(), g_CustomRoof.end());
        return all;
    }

    void SetRoofGunPagePick(std::uint8_t value)
    {
        g_RoofPagePick.store(value, std::memory_order_relaxed);
    }

    std::uint8_t RoofGunPagePick()
    {
        return g_RoofPagePick.load(std::memory_order_relaxed);
    }

    bool SetRoofGunScriptPick(const char* key)
    {
        RoofGun g;
        if (key && key[0] && !TryGetRoofGunByKey(key, &g))
            return false;
        g_RoofScriptPick.store(g.value, std::memory_order_relaxed);   // no key: the built-in machine gun
        g_RoofScriptPicked.store(true, std::memory_order_relaxed);
        return true;
    }

    RoofGun PickedRoofGun()
    {
        RoofGun g;
        const std::uint8_t value = g_RoofScriptPicked.load(std::memory_order_relaxed) ? g_RoofScriptPick.load(std::memory_order_relaxed)
                                                                                      : g_RoofPagePick.load(std::memory_order_relaxed);
        return TryGetRoofGunByValue(value, &g) ? g : BuiltInRoofGun();
    }

    std::uint16_t RoofGunAttackId()
    {
        return g_RoofAttack.load(std::memory_order_relaxed);
    }

    std::uint8_t RoofGunBulletId()
    {
        return g_RoofBullet.load(std::memory_order_relaxed);
    }

    float RoofGunFireInterval()
    {
        return g_RoofInterval.load(std::memory_order_relaxed);
    }

    std::uint16_t MainAttackIdForLevel(int level)
    {
        return level >= 1 && level <= 3 ? g_MainLevelAttack[level - 1].load(std::memory_order_relaxed) : 0;
    }

    bool IsMainAttackId(std::uint16_t attackId)
    {
        if (attackId == 0)
            return false;
        for (const std::atomic<std::uint16_t>& level : g_MainLevelAttack)
            if (level.load(std::memory_order_relaxed) == attackId)
                return true;
        return false;
    }

    std::uint8_t MainBulletId()
    {
        return g_MainBullet.load(std::memory_order_relaxed);
    }

    void OffsetMainMuzzle(float* muzzle)
    {
        GunFrame frame;
        if (g_MainHasMuzzle.load(std::memory_order_relaxed) && MainGunFrame(frame))
            MoveAlong(frame, g_MainMuzzle, frame.muzzle, muzzle);
    }

    bool MainGunMountMatrix(float* matrix)
    {
        GunFrame frame;
        if (!g_MainHasMount.load(std::memory_order_relaxed) || !MainGunFrame(frame))
            return false;
        float at[3];
        MoveAlong(frame, g_MainMount, frame.muzzle, at);
        const float* rows[3] = { frame.right, frame.up, frame.ahead };
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                matrix[r * 4 + c] = rows[r][c];
            matrix[r * 4 + 3] = 0.0f;
            matrix[12 + r] = at[r];
        }
        matrix[15] = 1.0f;
        return true;
    }

    std::uint32_t RowDeployCost(std::uint16_t developId)
    {
        std::uint8_t* develop = DevelopTableSEH();
        const std::uint16_t row = develop ? RowOfSEH(develop, developId) : kNoRow;
        return row != kNoRow ? RowDeployCostSEH(develop, row) : 0;
    }

    std::uint32_t FittedDeployCost()
    {
        const MainWeapon main = Picked();
        const RoofGun roof = PickedRoofGun();
        const std::uint16_t mainRow = !main.BuiltIn() ? main.developId : main.fireType == kHMissile ? kHMissileRow : kRailGunRow;
        return RowDeployCost(kBodyRow) + RowDeployCost(mainRow) + (roof.BuiltIn() ? 0 : RowDeployCost(roof.developId));
    }

    std::uint64_t FittedNameLangId()
    {
        return RecordField(g_FittedRegisteredRow.load(std::memory_order_relaxed), kRecordName) & battleGearTexts::kLangMask;
    }

    std::uint64_t FittedIconPath()
    {
        const std::uint64_t registered = RecordField(g_FittedRegisteredRow.load(std::memory_order_relaxed), kRecordIcon)
                                       & battleGearTexts::kPathStemMask;
        if (registered)
            return registered | battleGearTexts::kFtexExtension;
        static const bool railGunIcon = IconInstalled(battleGearTexts::kRailGunIcon);
        static const bool hMissileIcon = IconInstalled(battleGearTexts::kHMissileIcon);
        if (battleGearWeapons::MainWeaponType() == kHMissile)
            return hMissileIcon ? battleGearTexts::kHMissileIcon | battleGearTexts::kFtexExtension : 0;
        return railGunIcon ? battleGearTexts::kRailGunIcon | battleGearTexts::kFtexExtension : 0;
    }

    std::uint64_t FittedRoofGunNameLangId()
    {
        if (const std::uint64_t registered = RecordField(g_FittedRoofRow.load(std::memory_order_relaxed), kRecordName) & battleGearTexts::kLangMask)
            return registered;
        static const bool ownName = TextInstalled(battleGearTexts::kRoofMgName);
        return ownName ? battleGearTexts::kRoofMgName : 0;
    }

    std::uint64_t FittedRoofGunIconPath()
    {
        const std::uint64_t registered = RecordField(g_FittedRoofRow.load(std::memory_order_relaxed), kRecordIcon)
                                       & battleGearTexts::kPathStemMask;
        if (registered)
            return registered | battleGearTexts::kFtexExtension;
        static const bool ownIcon = IconInstalled(battleGearTexts::kRoofMgIcon);
        return ownIcon ? battleGearTexts::kRoofMgIcon | battleGearTexts::kFtexExtension : 0;
    }

    bool TextInstalled(std::uint64_t langId)
    {
        const auto getLangText = reinterpret_cast<GetLangText_t>(gAddr.Ui_GetLangText ? ResolveGameAddress(gAddr.Ui_GetLangText) : nullptr);
        return getLangText && LangTextShowsSEH(getLangText, langId);
    }

    bool IconInstalled(std::uint64_t iconStem)
    {
        return fox::detail::PathExistsByCode(iconStem | battleGearTexts::kFtexExtension);
    }
}
