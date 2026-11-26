#include <pch.h>
#include "BodyData_SetupBody.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "NativeCallPatch.h"
#include "VehicleData.h"
#include "log.h"

namespace
{
    using SetupBody_t        = void(__fastcall*)(std::uint8_t* dataBody);
    using GetBuilderMember_t = std::uint8_t*(__fastcall*)(void* partsBuildDirector, std::uint32_t builderType, std::int32_t index);

    constexpr std::size_t kDataBodyBody            = 0x18;
    constexpr std::size_t kDataBodyType            = 0x20;
    constexpr std::size_t kEntityPartsFile         = 0x98;
    constexpr std::size_t kEntityType              = 0xB1;   // vehicleTypeIndex
    constexpr std::size_t kEntityProxyType         = 0xB2;   // proxyVehicleTypeIndex
    constexpr std::size_t kModelBuilderShaderSwaps = 0x78;
    constexpr std::size_t kThunkSize               = 16;
    constexpr std::size_t kCallSize                = 5;

    const std::uint8_t kSetupBodyEntry[] = {
        0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xED,
        0x02, 0x01, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xF0, 0x00, 0x00, 0x00, 0x4C, 0x8B,
    };
    const std::uint8_t kSetupBodyEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    const std::uint64_t kGameVehicleShaderSwaps[] = {   // BodyBase::Initialize hands this list only to the game's own 13 vehicle models: a material's shader, then the one it is drawn with
        0xE910A72719EF, 0x7EADAB1040A0,   // tpp3DDF_Blin_LayerMul_RustDirty_LNM
        0x2FB504845227, 0xD005FD88CD63,   // tpp3DDF_LightCover_RustDirty_LNM
        0x8B8524014602, 0x48D191EE9D8E,   // tpp3DDF_RustDirty_LNM
        0x194947A823B5, 0x727A6B71F44B,   // tpp3DDF_RustDirty_UVSCroll_LNM
        0x8841E38BDAF0, 0xD619BCAE7B78,   // fox3DDF_Blin_LNM
        0x69CCC8871944, 0x3A7BD10798BB,   // fox3DDF_Blin_NC_LNM
        0xB3D1211042A1, 0xAEF5037FC7B2,   // fox3DDF_LightCover_LNM
        0xD41DF652A584, 0xCD914628B409,   // fox3DDF_Blin_LayerMul_Dirty_LNM
        0xB8A0BF169F98,                   // the game stops reading at this one
    };

    SetupBody_t        g_OrigSetupBody    = nullptr;
    void*              g_HookTarget       = nullptr;
    GetBuilderMember_t g_GetBuilderMember = nullptr;
    NativeCallPatch    g_ModelBuilderCall{};
    bool               g_ModelBuilderCallPatched = false;
    thread_local bool  t_RegisteredVehicleBody   = false;

    std::uint8_t GiveRegisteredVehicleItsType(std::uint8_t* entity, std::uint8_t& proxyType)
    {
        if (!entity || !VehicleData::ReadByte(entity, kEntityProxyType, proxyType))
            return 0;
        const std::uint8_t type = CustomVehicles::TypeOfPartsFile(VehicleData::PathOfFileField(entity, kEntityPartsFile));
        std::uint8_t typeInPack = 0;
        if (type == 0 || !VehicleData::ReadByte(entity, kEntityType, typeInPack))
            return 0;
        const bool gameType = typeInPack != 0 && typeInPack < CustomVehicles::kFirstType;   // a relief pack names the game vehicle it deploys as: left alone
        if (!gameType && !VehicleData::WriteByte(entity, kEntityType, type))
            return 0;
        return type;
    }

    std::uint8_t* CreatedBodySEH(const std::uint8_t* dataBody)
    {
        __try
        {
            return *reinterpret_cast<std::uint8_t* const*>(dataBody + kDataBodyBody);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void GiveModelTheGameVehicleShadersSEH(std::uint8_t* modelBuilder)
    {
        __try
        {
            *reinterpret_cast<const std::uint64_t**>(modelBuilder + kModelBuilderShaderSwaps) = kGameVehicleShaderSwaps;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint8_t* __fastcall ModelBuilderOfBody(void* partsBuildDirector, std::uint32_t builderType, std::int32_t index)
    {
        std::uint8_t* modelBuilder = g_GetBuilderMember(partsBuildDirector, builderType, index);
        if (modelBuilder && t_RegisteredVehicleBody)
            GiveModelTheGameVehicleShadersSEH(modelBuilder);
        return modelBuilder;
    }

    void __fastcall hk_SetupBody(std::uint8_t* dataBody)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetupBody, dataBody);
        std::uint8_t proxyType = 0;
        const std::uint8_t type = GiveRegisteredVehicleItsType(VehicleData::EntityOfDataBody(dataBody), proxyType);
        t_RegisteredVehicleBody = type != 0;
        g_OrigSetupBody(dataBody);
        t_RegisteredVehicleBody = false;
        if (type == 0)
            return;
        std::uint8_t typeOfBody = 0;
        if (VehicleData::ReadByte(dataBody, kDataBodyType, typeOfBody) && typeOfBody == type)   // the body in the player's relief pack gets type 0 and keeps its own proxy
            CustomVehicles::SetProxyType(type, proxyType);
        CustomVehicles::ApplyBodySpecs(type, CreatedBodySEH(dataBody));
    }

    void* MakeThunk(void* site, void* function)
    {
        auto* thunk = static_cast<std::uint8_t*>(HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), kThunkSize));
        if (!thunk)
            return nullptr;
        const std::uint64_t target = reinterpret_cast<std::uint64_t>(function);
        std::memset(thunk, 0xCC, kThunkSize);   // int3 filler so a stray jump traps right away
        thunk[0] = 0x48;   // mov rax, <our function>
        thunk[1] = 0xB8;
        std::memcpy(thunk + 2, &target, sizeof(target));
        thunk[10] = 0xFF;   // jmp rax
        thunk[11] = 0xE0;
        FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
        return thunk;
    }

    bool Calls(const std::uint8_t* site, const void* function)
    {
        std::uint8_t code[kCallSize] = {};
        if (!ReadEntryBytesSEH(site, code, sizeof(code)) || code[0] != 0xE8)   // call rel32
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, code + 1, sizeof(rel));
        return reinterpret_cast<std::uintptr_t>(site) + kCallSize + rel == reinterpret_cast<std::uintptr_t>(function);
    }

    void PatchModelBuilderCall()
    {
        auto* site = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleBodyBase_Initialize_ModelBuilderCall));
        g_GetBuilderMember = reinterpret_cast<GetBuilderMember_t>(ResolveGameAddress(gAddr.PartsBuildDirector_GetBuilderMemberFromTypeIndex));
        if (!site || !g_GetBuilderMember || !Calls(site, reinterpret_cast<const void*>(g_GetBuilderMember)))
        {
            LogError("CustomVehicles: unexpected code where a vehicle body picks its model's shaders on %s, registered vehicles keep their models' own shaders (no rust or dirt)", GetGameBuildName(gGameBuild));
            return;
        }
        void* thunk = MakeThunk(site, reinterpret_cast<void*>(&ModelBuilderOfBody));
        if (!thunk)
        {
            LogError("CustomVehicles: no memory near the game for the model shader thunk, registered vehicles keep their models' own shaders (no rust or dirt)");
            return;
        }
        g_ModelBuilderCall = {};
        g_ModelBuilderCall.target = site;
        g_ModelBuilderCall.destination = thunk;
        g_ModelBuilderCall.size = kCallSize;
        std::memcpy(g_ModelBuilderCall.expected, site, kCallSize);
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (InstallNativeCallPatches(&g_ModelBuilderCall, 1))
            {
                g_ModelBuilderCallPatched = true;
                return;
            }
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(1);
        }
        LogError("CustomVehicles: patching the vehicle model shader call failed (%s), registered vehicles keep their models' own shaders (no rust or dirt)",
                 NativePatchErrorName(GetLastError()));
    }

    void RestoreModelBuilderCall()
    {
        if (!g_ModelBuilderCallPatched)
            return;
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (RemoveNativeCallPatches(&g_ModelBuilderCall, 1) || !NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(1);
        }
        g_ModelBuilderCallPatched = false;
    }
}

bool Install_BodyData_SetupBody_Hook()
{
    static_assert(sizeof(kSetupBodyEntry) == sizeof(kSetupBodyEntryMask), "one mask byte per entry byte");
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleBodyData_SetupBody));
    if (!target)
    {
        LogError("CustomVehicles: no address for the vehicle body setup on %s, RegisterVehicle is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntryMasked(target, kSetupBodyEntry, kSetupBodyEntryMask, sizeof(kSetupBodyEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("CustomVehicles: vehicle body setup does not match %s (%s), RegisterVehicle is off", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CustomVehicles: BodyData::DataBody::SetupBody", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetupBody), reinterpret_cast<void**>(&g_OrigSetupBody)))
    {
        LogError("CustomVehicles: BodyData::DataBody::SetupBody hook failed, RegisterVehicle is off");
        return false;
    }
    g_HookTarget = target;
    PatchModelBuilderCall();
    return true;
}

void Uninstall_BodyData_SetupBody_Hook()
{
    RestoreModelBuilderCall();
    if (!g_HookTarget)
        return;
    DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_OrigSetupBody = nullptr;
}
