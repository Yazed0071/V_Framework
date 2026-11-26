#pragma once

#include <cstdint>

struct MagazineParameters
{
    int id = 0;
    std::uint16_t equipId = 0;
    std::uint16_t capacity = 0;
    std::uint16_t carryMax = 0;
    std::uint16_t bulletId = 0;
};

bool EquipParam_GetWeaponMagazines(int equipId, MagazineParameters& primary,
                                   MagazineParameters& underbarrel);   // only when one of them is wide
bool EquipParam_GetWeaponMagazinesAnyWidth(int equipId, MagazineParameters& primary,
                                           MagazineParameters& underbarrel);
bool EquipParam_GetMagazineParameters(int ammoId, MagazineParameters& parameters);
bool EquipParam_WideMagazinesReady();
bool EquipParam_WideMagazinesPlanned();
int EquipParam_MagazineLaneForWeapon(int ammoId, int gunMagazine);   // the vanilla magazine lane a weapon borrows for a wide magazine; 0 until its row is set
void EquipParam_RefreshUnderBarrelMagazines(int ammoId);
unsigned int EquipParam_GetWeaponHandlingFamily(int weaponId);
bool EquipParam_GetEquipGunBasicRow(unsigned int equipId, unsigned char out12[12]);
int EquipParam_GetReceiverMotionFrom(int receiverId);
int EquipParam_GetSightScopeDonor(int sightId);
int EquipParam_LogicalMagazineForUnderBarrelRow(const std::uint8_t* underBarrelRow);
int EquipParam_LogicalUnderBarrelMagazineForWeapon(int weaponId, int narrowMagazine);
std::uint32_t EquipParam_GetEquipType(std::uint32_t equipId);
bool EquipParam_IsBulletIdTaken(int bulletId);
bool EquipParam_GetDamageKind(int attackId, std::uint16_t& lethalDamage, std::uint16_t& flags);
bool EquipParam_InitializeWidePartIds();

struct lua_State;

int __cdecl l_SetMagazine(lua_State* L);
int __cdecl l_SetStock(lua_State* L);
int __cdecl l_SetMuzzle(lua_State* L);
int __cdecl l_SetReceiver(lua_State* L);
int __cdecl l_SetSight(lua_State* L);
int __cdecl l_SetBarrel(lua_State* L);
int __cdecl l_SetUnderBarrel(lua_State* L);
int __cdecl l_SetOption(lua_State* L);
int __cdecl l_SetBullet(lua_State* L);
int __cdecl l_SetDamage(lua_State* L);
int __cdecl l_DeclareDamages(lua_State* L);

int EquipParam_AllocateMagazineSlotForName(const char* name);
int EquipParam_AllocateStockSlotForName(const char* name);
int EquipParam_AllocateMuzzleSlotForName(const char* name);
int EquipParam_AllocateReceiverSlotForName(const char* name);
int EquipParam_AllocateSightSlotForName(const char* name);
int EquipParam_AllocateBarrelSlotForName(const char* name);
int EquipParam_AllocateUnderBarrelSlotForName(const char* name);
int EquipParam_AllocateOptionSlotForName(const char* name);
int EquipParam_AllocateBulletSlotForName(const char* name);

bool Install_MotionLoader_ReceiverTypeHook();
void Uninstall_MotionLoader_ReceiverTypeHook();
bool Install_MotionLoader_UnderBarrelTypeHook();
void Uninstall_MotionLoader_UnderBarrelTypeHook();
bool Install_MotionLoader_BarrelTypeHook();
void Uninstall_MotionLoader_BarrelTypeHook();
bool Install_MotionLoader_MagazineTypeHook();
void Uninstall_MotionLoader_MagazineTypeHook();
bool Install_MotionLoader_StockTypeHook();
void Uninstall_MotionLoader_StockTypeHook();
bool Install_UiController_UiSightTypeHook();
void Uninstall_UiController_UiSightTypeHook();
int  EquipParam_InheritPartMotionTypes(int barrelDst, int barrelSrc,
                                       int magazineDst, int magazineSrc,
                                       int stockDst, int stockSrc,
                                       int* outEligible = nullptr);

bool Install_GetAttackIdGuard();
void Uninstall_GetAttackIdGuard();

bool Install_GunInfoGuard();
void Uninstall_GunInfoGuard();

bool Install_BulletEffectGuard();
void Uninstall_BulletEffectGuard();

bool Install_WeaponKeyLog();
void Uninstall_WeaponKeyLog();
void EquipParam_SetWeaponHandling(unsigned int fromKey, unsigned int toKey);
unsigned int EquipParam_GetWeaponHandlingDonor(unsigned int equipId);

bool Install_FireSoundOverride_Hook();
void Uninstall_FireSoundOverride_Hook();

bool Install_LoadoutRequestGuard();
void Uninstall_LoadoutRequestGuard();

bool Install_LoadoutGunInfoGetOrBuild();
void Uninstall_LoadoutGunInfoGetOrBuild();

bool Install_SuppressorGauge_Hook();
void Uninstall_SuppressorGauge_Hook();

int EquipParam_AllocateDamageSlotForName(const char* name);
// a custom damage id under `name` holding the loaded row baseAttackId: lethalScale scales its damage to soldiers and the
// player, forceScale its impact force (the share armor lets through), its damage to vehicles, its blow and its shield damage,
// addFlags sets damage type flags on top of the row's own (0x1 isBullet ... 0x8000 isPenetrating, as SetDamage packs them);
// 0 while the table is not loaded or custom damage ids are not served (GetDamageParameter hook off)
int EquipParam_DeclareScaledDamage(const char* name, int baseAttackId, float lethalScale, float forceScale, int addFlags);
bool Install_DamageParameter_Hook();
void Uninstall_DamageParameter_Hook();

enum EquipVanillaSpace
{
    kVanillaSpace_Receiver = 0,
    kVanillaSpace_Barrel,
    kVanillaSpace_Magazine,
    kVanillaSpace_Bullet,
    kVanillaSpace_Stock,
    kVanillaSpace_MuzzleOption,
    kVanillaSpace_Sight,
    kVanillaSpace_UnderBarrel,
    kVanillaSpace_Option,
    kVanillaSpace_Weapon,
    kVanillaSpace_Count
};

int  EquipParam_GetWideAlias(int space, int wideId);
int  EquipParam_VanillaPartCount(int space);
int  EquipParam_WidePartRowState(int space, int wideId);
int  EquipParam_GetWideReceiverDonor(int wideId);
bool EquipParam_SetReceiverMotionType(int receiverId, int receiverType);
void EquipParam_RefillMotionEntries();
unsigned int EquipParam_ViewGunAsItsKind(unsigned int equipId);   // part type lookups on this thread answer for that gun; returns the gun viewed before, pass it back when done
int  EquipParam_GetReceiverForEquipId(int equipId);
int  EquipParam_GetBarrelLengthForEquipId(int equipId); // TppEquip.BARREL_LENGTH_* of the gun's barrel: 0 short, 1 middle, 2 long
void EquipParam_BindAliasMotion(int aliasId, int wideId);
int  EquipParam_GetDeclaredWeaponAttackId(int equipId);
void EquipParam_EnableWidePartIds(int newMaxId);
int  EquipParam_ResolvePartByte(int space, int id);
int  EquipParam_GetMagazineEquipAmmoId(int ammoId);
int  EquipParam_GetMagazineBulletId(int ammoId);
bool EquipParam_IsSuppressorMuzzleOption(int muzzleOptionId);
int  EquipParam_GetMuzzleOptionDurability(int muzzleOptionId);
int  EquipParam_GetBulletType(int bulletId);
int  EquipParam_GetLaneBulletType(int lane);
int  EquipParam_LogicalBulletForLane(int lane);   // the declared bullet an engine bullet byte stands for
int  EquipParam_BulletLaneForUse(int bulletId);   // the engine byte a declared bullet fires through now; gives it a lane if it has none
void EquipParam_BindBulletsForEquip(int equipId);
void EquipParam_BulletLanesNewMission();
void EquipParam_SaveBulletLanes();
int  EquipParam_FindVanillaMagazineByBullet(int bulletId, int excludeAmmoId);
void EquipParam_VanillaPreWrite(int space, int id, const unsigned char* row, int stride);
void EquipParam_VanillaPostWrite(int space, int id, const unsigned char* row, int stride);
void EquipParam_VanillaForceTaint(int space, int id, const char* why);
bool EquipParam_FindModifiedVanillaRow(const char** what, int* id);   // first vanilla row a mod changed that FOB cannot put back; FOB sorties are refused while one exists
void EquipParam_SyncFobPartRows();     // FOB: changed vanilla part and gunBasic rows hold the game's own bytes; outside FOB the mod's
void EquipParam_EnforceFobVanilla();   // the above plus equip id rows and weapon handling swaps of vanilla weapons
bool EquipParam_FobVanillaPartsOn();
bool EquipParam_FobVanillaBullet(int bulletId);   // FOB is on and the bullet is a vanilla one: its mod behavior stays off
bool EquipParam_IsEquipIdFobTainted(unsigned int equipId, int isWeaponSlot);
int  EquipParam_BulletFalloffSwapBegin(int lane);
void EquipParam_BulletFalloffSwapEnd();
