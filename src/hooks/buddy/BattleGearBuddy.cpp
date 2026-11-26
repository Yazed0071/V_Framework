#include <pch.h>

#include "BattleGearBuddy.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "AnimControl_PoseToBoneMatrices.h"
#include "BalloonSystemImpl_CreateImpl.h"
#include "BattleGearAnnounce.h"
#include "BattleGearCarry.h"
#include "BattleGearCustomizePage.h"
#include "BattleGearEffects.h"
#include "BattleGearLegs.h"
#include "BattleGearLoadoutSlots.h"
#include "BattleGearRoofGunSight.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "Buddy2BlockControllerImpl_CallBuddyInit.h"
#include "Buddy2BlockControllerImpl_GetBuddyGameObjectType.h"
#include "Buddy2BlockControllerImpl_GetPathAtBuddyType.h"
#include "Buddy2BlockControllerImpl_GetPathAtSub.h"
#include "Buddy2BlockControllerImpl_SubBlockSetting.h"
#include "BuddyOutfitRegistry.h"
#include "BuddyCommand_GetBuddyCommandName.h"
#include "BuddyServiceImpl_BuddyEquipSettings.h"
#include "BuddyServiceImpl_BuddyGearHead.h"
#include "BuddyServiceImpl_CanSortieBuddyType.h"
#include "BuddyServiceImpl_GetBuddyGear.h"
#include "BuddyServiceImpl_SetBuddyGearMainWP.h"
#include "BuddyServiceImpl_InitBuddyCommand.h"
#include "BulletSystem3Impl_CreateBullet.h"
#include "FoxHashes.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "RealizedWalkerGear2Impl_Execute1.h"
#include "RideWalkerGearActionImpl_ChangeWeapon.h"
#include "RideWalkerGearActionImpl_StateDrive.h"
#include "WalkerGear2Impl_SetUpMainWeapon.h"
#include "WalkerGearAttackActionImpl_GetMuzzleMatrix.h"
#include "WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget.h"
#include "WalkerGearAttackActionImpl_State_FireStunGun.h"
#include "WalkerGearAttackActionImpl_State_HoldOnArmWeapon.h"
#include "RideWalkerGearActionImpl_SubjectiveCamera.h"
#include "WalkerGearBasicActionImpl_CheckLowStepOnMove.h"
#include "WalkerGearBasicActionImpl_CorrectVelocity.h"
#include "WalkerGearBasicActionImpl_Execute.h"
#include "WalkerGearBasicActionImpl_FallCheck.h"
#include "WalkerGearDamageActionImpl_CorrectVelocity.h"
#include "log.h"
#include "../ui/CharacterSelectorCallbackImpl_SetupBuddyList.h"
#include "../ui/CustomizeUtilityImpl_GetBuddyUsageCost.h"
#include "../ui/MbDvcManageEv_UpdateCallBuddyPhaseOpen.h"
#include "../ui/MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility.h"
#include "../ui/MissionPreparationSystemImpl_GetParamBuddyPartsFromVars.h"
#include "../ui/TppModelViewerBuddyCommand_Execute.h"
#include "../../lua/LuaApi.h"

namespace
{
    using IsFobMode_t           = bool(__fastcall*)();
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using ServiceCheck_t        = bool(__fastcall*)(void* service);
    using DropPoint_t           = const float*(__fastcall*)(void* service, float* point);
    using SetForceChange_t      = void(__fastcall*)(void* blockController, bool force);
    using CallBuddyWithHalo_t   = bool(__fastcall*)(void* blockController, std::uint8_t buddyType, const float* point);

    constexpr const char* kMainPack                    = "/Assets/tpp/pack/buddy/battlegear/buddy_btg_00.fpk";
    constexpr const char* kOriginalSetSortieBuddyField = "_V_BattleGear_SetSortieBuddy";

    constexpr std::size_t  kVarBuddyType     = 0x2D68;   // vars.buddyType
    constexpr std::size_t  kVarObtainedBuddy = 0x2D69;   // one bit per buddy type
    constexpr std::size_t  kVarSortieBuddy   = 0x2D6A;   // one bit per buddy type
    constexpr std::uint8_t kBattleGearBit    = 1u << battleGear::kBuddyType;

    constexpr ULONGLONG kPollIntervalMs    = 1000;
    constexpr int       kLuaSlotsNeeded    = 8;
    constexpr int       kLoggedReasserts   = 10;
    constexpr int       kLoggedPackLookups = 8;

    constexpr std::size_t kTableApplication = 0x98;                    // quark system table: the application's systems
    constexpr std::size_t kBuddyService     = 0xE8;
    constexpr std::size_t kServiceRuns      = 0x08 / sizeof(void*);    // buddy service vtable: the two checks the game's own
    constexpr std::size_t kServiceCanChange = 0x20 / sizeof(void*);    // equipment change asks first,
    constexpr std::size_t kServiceDropPoint = 0x218 / sizeof(void*);   // and where a called buddy comes down

    constexpr std::size_t kBlockMainGroup  = 0x68;    // buddy block controller: the block group of the buddy's main pack
    constexpr std::size_t kBlockLoadedType = 0x175;   // the buddy type its blocks hold, the requested one sits a byte before
    constexpr std::size_t kGroupBlockCount = 0x1C;
    constexpr std::size_t kGroupBlocks     = 0x48;
    constexpr std::size_t kBlockStatus     = 0x80;
    constexpr int         kBlockLoaded     = 3;

    std::atomic<const std::uint8_t*> g_BlockController{ nullptr };
    thread_local int                 t_BlockSetUps = 0;

    std::atomic<bool>          g_Installed{ false };
    std::atomic<std::uint8_t>  g_HostActive{ 0 };
    std::atomic<bool>          g_HangarComplete{ false };
    std::atomic<ULONGLONG>     g_NextWrapCheck{ 0 };
    std::atomic<ULONGLONG>     g_NextPump{ 0 };
    std::atomic<bool>          g_WrapBusy{ false };
    std::atomic<int>           g_ReassertLogged{ 0 };
    std::atomic<int>           g_PackLookupLogged{ 0 };
    std::atomic<std::uint64_t> g_MainPackCode{ 0 };
    IsFobMode_t                g_IsFobMode = nullptr;

    bool InFobSEH()
    {
        __try
        {
            return g_IsFobMode();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool ComputeHostActive()
    {
        return g_Installed.load(std::memory_order_acquire) && g_IsFobMode
            && !MissionCodeGuard::ShouldBypassHooks() && !InFobSEH();
    }

    void RefreshHostActive()
    {
        g_HostActive.store(ComputeHostActive() ? 1 : 0, std::memory_order_release);
    }

    std::uint64_t InstalledPack(std::atomic<std::uint64_t>& cache, const char* path)
    {
        std::uint64_t code = cache.load(std::memory_order_relaxed);
        if (!code)
        {
            code = FoxHashes::PathCode64Ext(path);
            cache.store(code, std::memory_order_relaxed);
        }
        return code && fox::detail::PathExistsByCode(code) ? code : 0;
    }

    std::uint8_t ReadVarSEH(const std::uint8_t* vars, std::size_t offset)
    {
        __try
        {
            return vars[offset];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t LoadedBuddyTypeSEH(const std::uint8_t* controller)
    {
        __try
        {
            const auto* group = *reinterpret_cast<const std::uint8_t* const*>(controller + kBlockMainGroup);
            if (!group || *reinterpret_cast<const std::uint32_t*>(group + kGroupBlockCount) == 0)
                return 0;
            const auto* block = *reinterpret_cast<const std::uint8_t* const*>(group + kGroupBlocks);
            if (!block || *reinterpret_cast<const int*>(block + kBlockStatus) != kBlockLoaded)
                return 0;
            return controller[kBlockLoadedType];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool RedeploySEH(GetQuarkSystemTable_t getTable, void* blockController, SetForceChange_t setForceChange,
                     CallBuddyWithHalo_t callBuddyWithHalo)
    {
        __try
        {
            const std::uint8_t* table = getTable();
            const std::uint8_t* application = table ? *reinterpret_cast<std::uint8_t* const*>(table + kTableApplication) : nullptr;
            void* service = application ? *reinterpret_cast<void* const*>(application + kBuddyService) : nullptr;
            if (!service)
                return false;
            void** vtable = *static_cast<void***>(service);
            if (!reinterpret_cast<ServiceCheck_t>(vtable[kServiceRuns])(service)
                || !reinterpret_cast<ServiceCheck_t>(vtable[kServiceCanChange])(service))
                return false;
            alignas(16) float point[4] = {};
            const float* at = reinterpret_cast<DropPoint_t>(vtable[kServiceDropPoint])(service, point);
            setForceChange(blockController, true);
            return callBuddyWithHalo(blockController, battleGear::kBuddyType, at ? at : point);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool KeepUnlockedSEH(std::uint8_t* vars)
    {
        __try
        {
            if ((vars[kVarSortieBuddy] & kBattleGearBit) != 0)
                return false;
            vars[kVarObtainedBuddy] |= kBattleGearBit;
            vars[kVarSortieBuddy] |= kBattleGearBit;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadGlobalBool(lua_State* L, const char* table, const char* field)
    {
        const int top = g_lua_gettop(L);
        bool value = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        if (g_lua_type(L, -1) == LUA_TTABLE || g_lua_type(L, -1) == LUA_TUSERDATA)
        {
            g_lua_getfield(L, -1, const_cast<char*>(field));
            value = g_lua_toboolean(L, -1) != 0;
        }
        g_lua_settop(L, top);
        return value;
    }

    bool CallBuddyService(lua_State* L, const char* function, bool withType, bool* result)
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppBuddyService"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>(function));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                if (withType)
                    g_lua_pushnumber(L, static_cast<double>(battleGear::kBuddyType));
                ok = g_lua_pcall(L, withType ? 1 : 0, 1, 0) == 0;
                if (ok && result)
                    *result = g_lua_toboolean(L, -1) != 0;
                if (!ok)
                {
                    const char* err = GetLuaString(L, -1);
                    LogWarn("BattleGear: TppBuddyService.%s failed, %s", function, err ? err : "unknown error");
                }
            }
            else
            {
                LogWarn("BattleGear: TppBuddyService.%s not found, Battle Gear stays locked", function);
            }
        }
        g_lua_settop(L, top);
        return ok;
    }

    void ApplyUnlock(lua_State* L)
    {
        const bool hangarComplete = ReadGlobalBool(L, "gvars", "trm_isPushRewardBattleGearDevelopComplete");
        g_HangarComplete.store(hangarComplete, std::memory_order_release);
        if (!hangarComplete)
            return;
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        const bool wasSortie = vars && (ReadVarSEH(vars, kVarSortieBuddy) & kBattleGearBit) != 0;
        CallBuddyService(L, "SetObtainedBuddyType", true, nullptr);
        CallBuddyService(L, "SetSortieBuddyType", true, nullptr);
        if (!wasSortie)
            CallBuddyService(L, "SetVarsMissionStart", false, nullptr);
        const bool nowSortie = vars && (ReadVarSEH(vars, kVarSortieBuddy) & kBattleGearBit) != 0;
        LogDebug("BattleGear: unlock at mission %u start, save bit %d -> %d",
            static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), wasSortie ? 1 : 0, nowSortie ? 1 : 0);
    }

    bool CallOriginal(lua_State* L, int argumentCount, const char* name)
    {
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= argumentCount; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, argumentCount, LUA_MULTRET, 0) == 0)
            return true;
        const char* err = GetLuaString(L, -1);
        LogError("%s: %s", name, err ? err : "unknown error");
        g_lua_settop(L, argumentCount);
        return false;
    }

    int __cdecl l_SetSortieBuddy(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        if (!CallOriginal(L, top, "TppMission.SetSortieBuddy"))
            return 0;
        const int results = g_lua_gettop(L);
        if (ComputeHostActive() && V_FrameWork::LuaStackHasRoom(L, kLuaSlotsNeeded))
            ApplyUnlock(L);
        g_lua_settop(L, results);
        return results - top;
    }

    void WrapLuaFunction(lua_State* L, const char* library, const char* function,
                         const char* originalField, lua_CFunction wrapper)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(library));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int libraryTable = g_lua_gettop(L);
        g_lua_getfield(L, libraryTable, const_cast<char*>(originalField));
        const bool wrapped = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, libraryTable);
        if (wrapped)
        {
            g_lua_settop(L, top);
            return;
        }

        g_lua_getfield(L, libraryTable, const_cast<char*>(function));
        if (g_lua_type(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            return;
        }
        const int original = g_lua_gettop(L);

        g_lua_pushstring(L, const_cast<char*>(originalField));
        g_lua_pushvalue(L, original);
        g_lua_rawset(L, libraryTable);

        g_lua_pushstring(L, const_cast<char*>(function));
        g_lua_pushvalue(L, original);
        g_lua_pushcclosure(L, wrapper, 1);
        g_lua_rawset(L, libraryTable);

        g_lua_settop(L, top);
    }

    void UninstallParts()
    {
        Uninstall_BattleGearCustomizePage_Patches();
        Uninstall_BattleGearCarry_Patches();
        Uninstall_BuddyCommand_GetBuddyCommandName_Hooks();
        Uninstall_BattleGearAnnounce_Patches();
        Uninstall_MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility_Hook();
        Uninstall_RideWalkerGearActionImpl_SubjectiveCamera_Hooks();
        battleGearRoofGunSight::Uninstall();
        Uninstall_RideWalkerGearActionImpl_ChangeWeapon_Hook();
        Uninstall_WalkerGearDamageActionImpl_CorrectVelocity_Hook();
        Uninstall_WalkerGearBasicActionImpl_FallCheck_Hook();
        Uninstall_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook();
        Uninstall_WalkerGearBasicActionImpl_CorrectVelocity_Hook();
        Uninstall_RideWalkerGearActionImpl_StateDrive_SteeringHook();
        Uninstall_WalkerGearBasicActionImpl_Execute_Patch();
        Uninstall_RideWalkerGearActionImpl_StateDrive_Patches();
        Uninstall_WalkerGear2Impl_SetUpMainWeapon_Hook();
        Uninstall_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook();
        Uninstall_WalkerGearAttackActionImpl_State_FireStunGun_Hook();
        Uninstall_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook();
        Uninstall_BalloonSystemImpl_CreateImpl_Hook();
        Uninstall_BulletSystem3Impl_CreateBullet_Hook();
        battleGearSound::Uninstall();
        Uninstall_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook();
        battleGearWeapons::SetLoadoutActive(false);
        Uninstall_BuddyServiceImpl_BuddyGearHead_Hooks();
        Uninstall_BuddyServiceImpl_SetBuddyGearMainWP_Hooks();
        Uninstall_BuddyServiceImpl_GetBuddyGear_Hooks();
        Uninstall_AnimControl_PoseToBoneMatrices_Hook();
        battleGearEffects::Uninstall();
        battleGearLegs::Uninstall();
        Uninstall_RealizedWalkerGear2Impl_Execute1_Hook();
        Uninstall_TppModelViewerBuddyCommand_Execute_Hook();
        battleGearLoadoutSlots::Uninstall();
        Uninstall_MissionPreparationSystemImpl_GetParamBuddyPartsFromVars_Hook();
        Uninstall_BuddyServiceImpl_BuddyEquipSettings_Hook();
        Uninstall_Buddy2BlockControllerImpl_CallBuddyInit_Hook();
        Uninstall_CustomizeUtilityImpl_GetBuddyUsageCost_Hook();
        Uninstall_CharacterSelectorCallbackImpl_SetupBuddyList_Patch();
        Uninstall_BuddyServiceImpl_CanSortieBuddyType_Hooks();
        Uninstall_MbDvcManageEv_UpdateCallBuddyPhaseOpen_Patch();
        Uninstall_Buddy2BlockControllerImpl_GetBuddyGameObjectType_Patches();
        Uninstall_BuddyServiceImpl_InitBuddyCommand_Hooks();
        Uninstall_Buddy2BlockControllerImpl_SubBlockSetting_Hook();
        Uninstall_Buddy2BlockControllerImpl_GetPathAtSub_Hook();
    }
}

namespace battleGear
{
    bool Install()
    {
        if (g_Installed.load(std::memory_order_acquire))
            return true;
        g_IsFobMode = reinterpret_cast<IsFobMode_t>(ResolveGameAddress(gAddr.Barrier_IsFobMode));
        if (!g_IsFobMode)
        {
            LogError("BattleGear: FOB check address missing on this build, Battle Gear is off");
            return false;
        }
        if (!Buddy2BlockControllerImpl_GetPathAtBuddyType_Installed())
        {
            LogError("BattleGear: the buddy pack lookup (BuddyOutfit module) is not installed, Battle Gear is off");
            return false;
        }
        if (!Install_Buddy2BlockControllerImpl_GetPathAtSub_Hook()
            || !Install_Buddy2BlockControllerImpl_SubBlockSetting_Hook()
            || !Install_BuddyServiceImpl_InitBuddyCommand_Hooks()
            || !Install_Buddy2BlockControllerImpl_GetBuddyGameObjectType_Patches())
        {
            UninstallParts();
            return false;
        }
        Install_MbDvcManageEv_UpdateCallBuddyPhaseOpen_Patch();
        Install_BuddyServiceImpl_CanSortieBuddyType_Hooks();
        Install_CharacterSelectorCallbackImpl_SetupBuddyList_Patch();
        Install_CustomizeUtilityImpl_GetBuddyUsageCost_Hook();
        Install_Buddy2BlockControllerImpl_CallBuddyInit_Hook();
        Install_BuddyServiceImpl_BuddyEquipSettings_Hook();
        if (Install_MissionPreparationSystemImpl_GetParamBuddyPartsFromVars_Hook())
            battleGearLoadoutSlots::Install();
        Install_TppModelViewerBuddyCommand_Execute_Hook();
        Install_RealizedWalkerGear2Impl_Execute1_Hook();
        if (battleGearLegs::Install())
        {
            Install_AnimControl_PoseToBoneMatrices_Hook();
            battleGearEffects::Install();
            Install_BattleGearCarry_Patches();
        }
        battleGearWeapons::SetLoadoutActive(Install_BuddyServiceImpl_GetBuddyGear_Hooks() && Install_BuddyServiceImpl_SetBuddyGearMainWP_Hooks());
        Install_BuddyServiceImpl_BuddyGearHead_Hooks();
        Install_WalkerGearAttackActionImpl_GetMuzzleMatrix_Hook();
        Install_BulletSystem3Impl_CreateBullet_Hook();
        Install_BalloonSystemImpl_CreateImpl_Hook();
        battleGearSound::Install();
        Install_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook();
        Install_WalkerGearAttackActionImpl_State_FireStunGun_Hook();
        Install_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook();
        Install_WalkerGear2Impl_SetUpMainWeapon_Hook();
        Install_RideWalkerGearActionImpl_StateDrive_Patches();
        if (Install_WalkerGearBasicActionImpl_Execute_Patch() && !Install_RideWalkerGearActionImpl_StateDrive_SteeringHook())
            Uninstall_WalkerGearBasicActionImpl_Execute_Patch();
        Install_WalkerGearBasicActionImpl_CorrectVelocity_Hook();
        Install_WalkerGearBasicActionImpl_CheckLowStepOnMove_Hook();
        Install_WalkerGearBasicActionImpl_FallCheck_Hook();
        Install_WalkerGearDamageActionImpl_CorrectVelocity_Hook();
        Install_RideWalkerGearActionImpl_ChangeWeapon_Hook();
        battleGearRoofGunSight::Install();
        Install_RideWalkerGearActionImpl_SubjectiveCamera_Hooks();
        if (!Install_BattleGearCustomizePage_Patches())
            Install_MotherBaseDeviceTopMenuTableImpl_ApplyConditionVisibility_Hook();   // without the page the entry would open nothing
        Install_BattleGearAnnounce_Patches();
        Install_BuddyCommand_GetBuddyCommandName_Hooks();
        g_Installed.store(true, std::memory_order_release);
        RefreshHostActive();
        return true;
    }

    void Uninstall()
    {
        g_Installed.store(false, std::memory_order_release);
        g_HostActive.store(0, std::memory_order_release);
        UninstallParts();
    }

    bool IsHostedType(std::int32_t buddyType)
    {
        if (buddyType != kBuddyType)
            return false;
        RefreshHostActive();
        return g_HostActive.load(std::memory_order_acquire) != 0;
    }

    const void* HostActiveFlag()
    {
        return &g_HostActive;
    }

    bool IsBattleGearOut()
    {
        if (!g_HostActive.load(std::memory_order_acquire))
            return false;
        if (t_BlockSetUps > 0)
            return true;
        const std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        return vars && ReadVarSEH(vars, kVarBuddyType) == kBuddyType;
    }

    BlockSetUp::BlockSetUp(const void* blockController)
    {
        NoteBuddyBlock(blockController);
        ++t_BlockSetUps;
    }

    BlockSetUp::~BlockSetUp()
    {
        --t_BlockSetUps;
    }

    bool SettingBlockUp()
    {
        return t_BlockSetUps > 0;
    }

    bool Redeploy()
    {
        if (!IsBattleGearOut() || !battleGearWeapons::OnField())
            return false;
        const auto getTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        void* const* instance = static_cast<void* const*>(ResolveGameAddress(gAddr.Buddy2BlockController_Instance));
        const auto setForceChange = reinterpret_cast<SetForceChange_t>(ResolveGameAddress(gAddr.Buddy2BlockController_SetForceChange));
        const auto callBuddyWithHalo = reinterpret_cast<CallBuddyWithHalo_t>(ResolveGameAddress(gAddr.Buddy2BlockController_CallBuddyWithHalo));
        if (!getTable || !instance || !*instance || !setForceChange || !callBuddyWithHalo)
            return false;
        return RedeploySEH(getTable, *instance, setForceChange, callBuddyWithHalo);
    }

    void NoteBuddyBlock(const void* blockController)
    {
        if (blockController)
            g_BlockController.store(static_cast<const std::uint8_t*>(blockController), std::memory_order_release);
    }

    bool IsBattleGearLoaded()
    {
        const std::uint8_t* controller = g_BlockController.load(std::memory_order_acquire);
        return controller && g_HostActive.load(std::memory_order_acquire) && LoadedBuddyTypeSEH(controller) == kBuddyType;
    }

    bool HoldsAnotherBuddy()
    {
        const std::uint8_t* controller = g_BlockController.load(std::memory_order_acquire);
        const std::uint8_t loaded = controller ? LoadedBuddyTypeSEH(controller) : 0;
        return loaded != 0 && loaded != kBuddyType;
    }

    std::uint64_t MainPackPath()
    {
        const std::uint64_t code = InstalledPack(g_MainPackCode, kMainPack);
        if (g_PackLookupLogged.fetch_add(1, std::memory_order_relaxed) < kLoggedPackLookups)
            LogDebug("BattleGear: main pack lookup in mission %u: %s", static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()),
                code ? "own pack buddy_btg_00.fpk" : "not installed, the D-Walker pack stands in");
        return code;
    }

    void EnsureLuaWraps(lua_State* L)
    {
        if (!L || !g_Installed.load(std::memory_order_acquire))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextWrapCheck.load(std::memory_order_relaxed))
            return;
        if (g_WrapBusy.exchange(true))
            return;
        g_NextWrapCheck.store(now + kPollIntervalMs, std::memory_order_relaxed);
        if (ResolveLuaApi() && V_FrameWork::LuaStackHasRoom(L, kLuaSlotsNeeded))
            WrapLuaFunction(L, "TppMission", "SetSortieBuddy", kOriginalSetSortieBuddyField, &l_SetSortieBuddy);
        g_WrapBusy.store(false);
    }

    void Pump()
    {
        if (!g_Installed.load(std::memory_order_acquire))
            return;
        LogBuddyBlockStateWhileWatched();
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextPump.load(std::memory_order_relaxed))
            return;
        g_NextPump.store(now + kPollIntervalMs, std::memory_order_relaxed);
        RefreshHostActive();
        if (!g_HostActive.load(std::memory_order_acquire) || !g_HangarComplete.load(std::memory_order_acquire))
            return;
        std::uint8_t* vars = buddyoutfit::ReadScriptVars();
        if (vars && KeepUnlockedSEH(vars) && g_ReassertLogged.fetch_add(1, std::memory_order_relaxed) < kLoggedReasserts)
            LogDebug("BattleGear: save bit was cleared in mission %u, set again",
                static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()));
    }

    bool SortieUnlocked()
    {
        RefreshHostActive();
        return g_HostActive.load(std::memory_order_acquire) != 0 && g_HangarComplete.load(std::memory_order_acquire);
    }
}
