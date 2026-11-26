#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace battleGearArmory
{
    constexpr std::uint8_t kRailGun          = 3;   // the two built in carry their D-Walker main weapon type as their value
    constexpr std::uint8_t kHMissile         = 2;
    constexpr std::uint8_t kFirstCustomValue = 8;   // D-Walker main weapon types end at 5
    constexpr std::uint8_t kRoofGunType      = 5;   // the D-Walker sidearm type whose slot the roof gun fills (the walker ZE'EV)

    constexpr std::uint16_t kRoofMgAttackId     = 0x133;    // ATK_Tankgun_12_7mmHeavyMachineGun_West
    constexpr std::uint8_t  kRoofMgBulletId     = 0x3E;     // BL_Turret
    constexpr float         kRoofMgFireInterval = 0.125f;   // seconds between rounds, the tanks' cupola gun's

    struct MainWeapon
    {
        std::string   key;
        std::uint8_t  value     = 0;
        std::uint8_t  fireType  = 0;   // kRailGun or kHMissile: whose trigger, aim and rounds it uses
        std::uint16_t developId = 0;   // its R&D row; the built-in pair borrows the D-Walker stun gun and missile pod rows
        std::uint64_t fpkPath   = 0;   // 0: the Battle Gear's own weapon pack
        std::uint16_t attackId  = 0;   // TppDamage.ATK_* its rounds deal; 0: the built-in gun's. The rail gun type only
        std::uint8_t  bulletId  = 0;   // TppEquip.BL_* it fires; 0: the built-in gun's. The rail gun type only
        bool          hasMuzzle = false;
        bool          hasMount  = false;
        float         muzzle[3] = {};  // where its rounds leave, from the built-in muzzle: metres right, up and ahead along the barrel
        float         mount[3]  = {};  // where its model sits, the same way

        bool BuiltIn() const { return value < kFirstCustomValue; }
    };

    struct RoofGun
    {
        std::string   key;                     // empty: the built-in machine gun
        std::uint8_t  value        = 0;
        std::uint16_t developId    = 0;        // its R&D row; the built-in one borrows the walker ZE'EV's
        std::uint16_t attackId     = kRoofMgAttackId;       // TppDamage.ATK_* its rounds deal
        std::uint8_t  bulletId     = kRoofMgBulletId;       // TppEquip.BL_* it fires
        float         fireInterval = kRoofMgFireInterval;

        bool BuiltIn() const { return value == 0; }
    };

    bool Register(MainWeapon& weapon);
    bool TryGetByValue(std::uint8_t value, MainWeapon* out);
    bool TryGetByKey(const char* key, MainWeapon* out);
    bool TryGetByDevelopId(std::uint16_t developId, MainWeapon* out);
    std::vector<MainWeapon> All();

    const char* FireTypeName(std::uint8_t fireType);
    bool FireTypeFromName(const char* name, std::uint8_t* fireType);

    void SetPagePick(std::uint8_t value);
    std::uint8_t PagePick();
    bool SetScriptPick(const char* key);
    void ClearScriptPick();    // the roof gun's with it

    MainWeapon PagePicked();   // the Customize page's pick, the rail gun without one
    MainWeapon Picked();       // a script's pick for this mission comes first
    MainWeapon Latch();        // fixes both guns for the buddy block being set up; a pack that is not installed gives the built-in gun
    MainWeapon Latched();

    // three loadout slots, as the D-Walker has them: Customize edits the one picked on its slot screen, Sortie Prep's and the
    // iDroid's equipment lists pick which one deploys. The page picks above are always the current slot's.
    constexpr int kLoadoutSlots = 3;

    struct Loadout
    {
        std::uint8_t mainWeapon = 0;   // a main gun's value; 0: the rail gun
        std::uint8_t roofGun    = 0;   // a roof gun's value; 0: the machine gun
        std::uint8_t head       = 0;   // D-Walker head type; 0: the support head
    };

    void LoadLoadoutSlots();   // from V Framework's state; the single pick older builds saved becomes slot 0
    Loadout LoadoutSlot(int slot);
    void SetLoadoutSlot(int slot, const Loadout& loadout);
    int CurrentLoadoutSlot();
    void SetCurrentLoadoutSlot(int slot);
    std::uint32_t LoadoutDeployCost(int slot);   // GMP: the body's row and the slot's guns'
    std::uint32_t LoadoutDeployCost(const Loadout& loadout);

    bool RegisterRoofGun(RoofGun& gun);
    bool TryGetRoofGunByValue(std::uint8_t value, RoofGun* out);   // 0 is the built-in machine gun
    bool TryGetRoofGunByKey(const char* key, RoofGun* out);
    bool TryGetRoofGunByDevelopId(std::uint16_t developId, RoofGun* out);
    std::vector<RoofGun> AllRoofGuns();
    void SetRoofGunPagePick(std::uint8_t value);
    std::uint8_t RoofGunPagePick();
    bool SetRoofGunScriptPick(const char* key);
    RoofGun PickedRoofGun();

    // what the latched guns fire, read every walker frame
    std::uint16_t RoofGunAttackId();
    std::uint8_t  RoofGunBulletId();
    float         RoofGunFireInterval();
    std::uint16_t MainAttackIdForLevel(int level);   // charge level 1-3; 0: the built-in gun's
    bool IsMainAttackId(std::uint16_t attackId);     // one of the latched gun's own
    std::uint8_t  MainBulletId();                    // 0: the built-in gun's
    void OffsetMainMuzzle(float* muzzle);            // moves the built-in muzzle point to the latched gun's own, when it has one
    bool MainGunMountMatrix(float* matrix);          // 16 floats as the game stores a matrix; false: the model stays where the walker hangs it

    std::uint32_t RowDeployCost(std::uint16_t developId);   // GMP, from the game's R&D table
    std::uint32_t FittedDeployCost();                       // the picked guns' rows on top of the body's
    std::uint64_t FittedNameLangId();                       // the fitted registered main gun's name as its R&D row gives it, 0 for a built-in gun
    std::uint64_t FittedIconPath();                         // its icon; a built-in gun's own once the texture is installed, else 0
    std::uint64_t FittedRoofGunNameLangId();                // the same for the roof gun
    std::uint64_t FittedRoofGunIconPath();

    bool TextInstalled(std::uint64_t langId);    // the lang packs carry this Battle Gear text
    bool IconInstalled(std::uint64_t iconStem);
}
