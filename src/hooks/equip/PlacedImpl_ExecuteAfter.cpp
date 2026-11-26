#include <pch.h>
#include "PlacedImpl_ExecuteAfter.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "../buddy/BuddyCodeCave.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "SupportWeaponRegistry.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using ExecuteAfter_t                = void(__fastcall*)(std::uint8_t* placedInterface);
    using SendOnActivatePlacedMessage_t = void(__fastcall*)(std::uint8_t* impl, std::uint32_t index);

    constexpr std::uint8_t kExecuteAfterEntry[]   = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x59, 0xE0 };
    constexpr std::uint8_t kSendOnActivateEntry[] = { 0x4C, 0x8B, 0xDC, 0x53, 0x48, 0x83, 0xEC, 0x50 };
    constexpr std::uint8_t kMineIdLoad[]          = { 0x0F, 0xB7, 0x04, 0x88, 0x3D, 0x9D, 0x01, 0x00, 0x00 };   // movzx eax,[rax+rcx*4]; cmp eax,0x19D

    constexpr std::size_t kImplInterface = 0x20;   // ExecuteAfter gets PlacedImpl+0x20
    constexpr std::size_t kImplCount     = 0x50;
    constexpr std::size_t kImplCells     = 0x88;   // {u16 equipId, u16 user} per record
    constexpr std::size_t kImplExtra     = 0x90;
    constexpr std::size_t kImplFlags     = 0x98;

    constexpr std::size_t kGstPlacedSystem = 0x208;   // GameSystemTable -> PlacedSystem interface
    constexpr std::size_t kSysPositions    = 0x08;
    constexpr std::size_t kSysCells        = 0x18;
    constexpr std::size_t kSysCount        = 0x60;
    constexpr std::size_t kSysFlags        = 0x90;

    constexpr std::size_t   kCellStride     = 4;
    constexpr std::size_t   kFlagStride     = 3;
    constexpr std::size_t   kExtraStride    = 0x14;
    constexpr std::size_t   kExtraFadeTime  = 0x00;   // UpdateFadeout fades the model out over this, then removes the record
    constexpr std::size_t   kExtraOwner     = 0x0A;
    constexpr std::size_t   kPositionStride = 0x10;
    constexpr std::uint8_t  kAlive          = 0x01;   // flag byte 0
    constexpr std::uint8_t  kActivated      = 0x04;   // flag byte 0
    constexpr std::uint8_t  kDone           = 0x01;   // flag byte 2
    constexpr std::uint32_t kMaxRecords     = 0x400;
    constexpr int           kRequestLife    = 64;
    constexpr std::uint32_t kTrackedHandles = 0x1000;

    struct PendingRequest
    {
        CustomPlaced::Request kind;
        std::uint32_t         handle;
        int                   age;
    };

    struct Candidate
    {
        std::uint32_t index;
        std::uint16_t engineId;
        std::uint16_t owner;
    };

    struct ImplView
    {
        std::uint32_t count = 0;
        std::uint32_t firstHandle = 0;
        std::uint8_t* cells = nullptr;
        std::uint8_t* extra = nullptr;
        std::uint8_t* flags = nullptr;
    };

    ExecuteAfter_t                g_Orig            = nullptr;
    void*                         g_Target          = nullptr;
    SendOnActivatePlacedMessage_t g_SendOnActivate  = nullptr;
    BuddyCodeCave                 g_InterrogateCave = { "EquipCustomPlaced: interrogation mine check" };
    BuddyCodeCave                 g_DogCave         = { "EquipCustomPlaced: D-Dog mine sniffing" };

    std::mutex                  g_RequestLock;
    std::vector<PendingRequest> g_Requests;

    std::array<std::atomic<std::uint64_t>, kTrackedHandles / 64> g_PlaceAnnounced{};

    std::uint32_t PlacedDonor(std::uint32_t engineId)
    {
        const std::uint32_t donor = SupportWeapon::FamilyFrom(engineId);
        return SupportWeapon::IsPlaceAsDonor(donor) ? donor : 0;
    }

    std::uint32_t __fastcall MineCheckId(std::uint32_t engineId)   // called from the caves: custom mines count as their donor
    {
        const std::uint32_t donor = PlacedDonor(engineId);
        return donor != 0 ? donor : engineId;
    }

    std::uint8_t* PlacedSystemSEH()
    {
        __try
        {
            std::uint8_t* gst = ThrowingSystemLayout::GameSystemTable();
            return gst ? *reinterpret_cast<std::uint8_t**>(gst + kGstPlacedSystem) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ReadRecordSEH(std::uint32_t handle, std::uint16_t& engineId, bool& alive, float position[3])
    {
        __try
        {
            std::uint8_t* system = PlacedSystemSEH();
            if (!system || handle >= *reinterpret_cast<std::uint32_t*>(system + kSysCount))
                return false;
            const std::uint8_t* cells     = *reinterpret_cast<std::uint8_t**>(system + kSysCells);
            const std::uint8_t* flags     = *reinterpret_cast<std::uint8_t**>(system + kSysFlags);
            const std::uint8_t* positions = *reinterpret_cast<std::uint8_t**>(system + kSysPositions);
            if (!cells || !flags || !positions)
                return false;
            engineId = *reinterpret_cast<const std::uint16_t*>(cells + handle * kCellStride);
            alive    = (flags[handle * kFlagStride] & kAlive) != 0;
            const float* p = reinterpret_cast<const float*>(positions + handle * kPositionStride);
            position[0] = p[0];
            position[1] = p[1];
            position[2] = p[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadImplSEH(std::uint8_t* impl, ImplView& view)
    {
        __try
        {
            view.count = *reinterpret_cast<std::uint32_t*>(impl + kImplCount);
            view.cells = *reinterpret_cast<std::uint8_t**>(impl + kImplCells);
            view.extra = *reinterpret_cast<std::uint8_t**>(impl + kImplExtra);
            view.flags = *reinterpret_cast<std::uint8_t**>(impl + kImplFlags);
            if (view.count == 0 || view.count > kMaxRecords || !view.cells || !view.extra || !view.flags)
                return false;
            view.firstHandle = 0;
            std::uint8_t* system = PlacedSystemSEH();
            const std::uint8_t* systemFlags = system ? *reinterpret_cast<std::uint8_t**>(system + kSysFlags) : nullptr;
            if (systemFlags && view.flags >= systemFlags)
                view.firstHandle = static_cast<std::uint32_t>((view.flags - systemFlags) / kFlagStride);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadLiveCellSEH(const ImplView& view, std::uint32_t index, std::uint16_t& engineId)
    {
        __try
        {
            const std::uint8_t* f = view.flags + index * kFlagStride;
            if ((f[0] & kAlive) == 0 || (f[2] & kDone) != 0)
                return false;
            engineId = *reinterpret_cast<const std::uint16_t*>(view.cells + index * kCellStride);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPlacementSEH(const ImplView& view, std::uint32_t index, std::uint16_t& engineId, std::uint16_t& owner)
    {
        __try
        {
            if ((view.flags[index * kFlagStride] & kAlive) == 0)
                return false;
            engineId = *reinterpret_cast<const std::uint16_t*>(view.cells + index * kCellStride);
            owner    = *reinterpret_cast<const std::uint16_t*>(view.extra + index * kExtraStride + kExtraOwner);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void AnnouncePlacements(const ImplView& view)
    {
        for (std::uint32_t index = 0; index < view.count; ++index)
        {
            const std::uint32_t handle = view.firstHandle + index;
            if (handle >= kTrackedHandles)
                break;
            std::atomic<std::uint64_t>& announced = g_PlaceAnnounced[handle / 64];
            const std::uint64_t bit = 1ull << (handle % 64);
            std::uint16_t engineId = 0;
            std::uint16_t owner    = 0;
            if (!ReadPlacementSEH(view, index, engineId, owner))
            {
                announced.fetch_and(~bit);   // slot empty again, its next item gets its own message
                continue;
            }
            const std::uint32_t donor = PlacedDonor(engineId);
            if (donor == 0 || SupportWeapon::IsDecoyDonor(donor) || (announced.fetch_or(bit) & bit) != 0)
                continue;
            V_FrameWork::QueueMessage("Placed", "OnPlaceCustom", handle, equip::FromEngine(engineId), owner);
        }
    }

    void SetActivatedSEH(const ImplView& view, std::uint32_t index)
    {
        __try
        {
            view.flags[index * kFlagStride] |= kActivated;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void MarkDoneSEH(const ImplView& view, std::uint32_t index)
    {
        __try
        {
            *reinterpret_cast<float*>(view.extra + index * kExtraStride + kExtraFadeTime) = 0.0f;
            view.flags[index * kFlagStride + 2] |= kDone;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool CollectActivatedSEH(const ImplView& view, Candidate* out, std::uint32_t& found)
    {
        __try
        {
            found = 0;
            for (std::uint32_t index = 0; index < view.count; ++index)
            {
                const std::uint8_t* f = view.flags + index * kFlagStride;
                if ((f[0] & kAlive) == 0 || (f[0] & kActivated) == 0 || (f[2] & kDone) != 0)
                    continue;
                Candidate& c = out[found++];
                c.index    = index;
                c.engineId = *reinterpret_cast<const std::uint16_t*>(view.cells + index * kCellStride);
                c.owner    = *reinterpret_cast<const std::uint16_t*>(view.extra + index * kExtraStride + kExtraOwner);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void SendVanillaMessageSEH(std::uint8_t* impl, std::uint32_t index)
    {
        __try
        {
            if (g_SendOnActivate)
                g_SendOnActivate(impl, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ApplyRequests(const ImplView& view)
    {
        std::lock_guard<std::mutex> lock(g_RequestLock);
        for (std::size_t i = 0; i < g_Requests.size();)
        {
            PendingRequest& request = g_Requests[i];
            const bool here = request.handle >= view.firstHandle && request.handle < view.firstHandle + view.count;
            if (here)
            {
                const std::uint32_t index = request.handle - view.firstHandle;
                std::uint16_t engineId = 0;
                if (ReadLiveCellSEH(view, index, engineId) && PlacedDonor(engineId) != 0)
                {
                    if (request.kind == CustomPlaced::Request::Activate)
                        SetActivatedSEH(view, index);
                    else
                        MarkDoneSEH(view, index);
                }
            }
            if (here || ++request.age > kRequestLife)
                g_Requests.erase(g_Requests.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
    }

    void RunCustomPlaced(std::uint8_t* impl)
    {
        ImplView view;
        if (!ReadImplSEH(impl, view))
            return;
        AnnouncePlacements(view);
        ApplyRequests(view);

        thread_local std::vector<Candidate> candidates;
        candidates.resize(view.count);
        std::uint32_t found = 0;
        if (!CollectActivatedSEH(view, candidates.data(), found))
            return;
        for (std::uint32_t i = 0; i < found; ++i)
        {
            const Candidate& c = candidates[i];
            const std::uint32_t donor = PlacedDonor(c.engineId);
            if (donor == 0 || SupportWeapon::IsDecoyDonor(donor))
                continue;
            MarkDoneSEH(view, c.index);
            SendVanillaMessageSEH(impl, c.index);
            V_FrameWork::QueueMessage("Placed", "OnActivateCustom", view.firstHandle + c.index,
                                      equip::FromEngine(c.engineId), c.owner);
        }
    }

    void __fastcall hkExecuteAfter(std::uint8_t* placedInterface)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            RunCustomPlaced(placedInterface - kImplInterface);
        g_Orig(placedInterface);
    }

    CaveCode MineCheckCave(std::uintptr_t resume)
    {
        CaveCode c;
        c.Emit({ 0x0F, 0xB7, 0x04, 0x88 });                                         // movzx eax, word ptr [rax+rcx*4]
        c.Emit({ 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0 });                 // push rbx; mov rbx, rsp; and rsp, -16
        c.Emit({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 });     // push rcx, rdx, r8-r11
        c.Emit({ 0x48, 0x83, 0xEC, 0x60 });                                         // sub rsp, 0x60
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x04, 0x24 });                                   // movdqu [rsp], xmm0
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x50 });                             // movdqu [rsp+0x50], xmm5
        c.Emit({ 0x48, 0x83, 0xEC, 0x20, 0x89, 0xC1, 0x48, 0xB8 });                 // sub rsp, 0x20; mov ecx, eax; mov rax, imm64
        c.Imm64(reinterpret_cast<std::uintptr_t>(&MineCheckId));
        c.Emit({ 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20 });                             // call rax; add rsp, 0x20
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x04, 0x24 });                                   // movdqu xmm0, [rsp]
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x10 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x20 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x30 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x40 });
        c.Emit({ 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x50 });                             // movdqu xmm5, [rsp+0x50]
        c.Emit({ 0x48, 0x83, 0xC4, 0x60 });                                         // add rsp, 0x60
        c.Emit({ 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59 });     // pop r11-r8, rdx, rcx
        c.Emit({ 0x48, 0x89, 0xDC, 0x5B });                                         // mov rsp, rbx; pop rbx
        c.Emit({ 0x3D, 0x9D, 0x01, 0x00, 0x00 });                                   // cmp eax, 0x19D
        c.JmpAbs(resume);
        return c;
    }

    void InstallMineCave(BuddyCodeCave& cave, std::uintptr_t site)
    {
        if (!site)
        {
            LogError("%s: address missing on this build, custom mines count as no mine there", cave.name);
            return;
        }
        void* live = ResolveGameAddress(site);
        if (!live)
            return;
        const std::uintptr_t resume = reinterpret_cast<std::uintptr_t>(live) + sizeof(kMineIdLoad);
        InstallBuddyCodeCave(cave, site, kMineIdLoad, sizeof(kMineIdLoad), MineCheckCave(resume));
    }
}

namespace CustomPlaced
{
    bool IsCustom(std::uint32_t handle)
    {
        std::uint16_t engineId = 0;
        bool alive = false;
        float position[3] = {};
        return ReadRecordSEH(handle, engineId, alive, position) && alive && PlacedDonor(engineId) != 0;
    }

    bool GetPosition(std::uint32_t handle, float out[3])
    {
        std::uint16_t engineId = 0;
        bool alive = false;
        return ReadRecordSEH(handle, engineId, alive, out) && alive;
    }

    void Queue(Request kind, std::uint32_t handle)
    {
        std::lock_guard<std::mutex> lock(g_RequestLock);
        g_Requests.push_back({ kind, handle, 0 });
    }
}

bool Install_PlacedImpl_ExecuteAfter_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.PlacedImpl_ExecuteAfter);
    void* send   = ResolveGameAddress(gAddr.PlacedImpl_SendOnActivatePlacedMessage);
    if (!target || !send)
    {
        LogError("EquipCustomPlaced: placed item addresses missing on this build, custom placed items do nothing when triggered");
        return false;
    }
    if (CheckHookEntry(send, kSendOnActivateEntry, sizeof(kSendOnActivateEntry)) == HookEntry::Unknown)
    {
        LogError("EquipCustomPlaced: SendOnActivatePlacedMessage differs on this build, custom placed items do nothing when triggered");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kExecuteAfterEntry, sizeof(kExecuteAfterEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("EquipCustomPlaced: PlacedImpl::ExecuteAfter differs on this build (%s), custom placed items do nothing when triggered", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("EquipCustomPlaced: PlacedImpl::ExecuteAfter", target);
    g_SendOnActivate = reinterpret_cast<SendOnActivatePlacedMessage_t>(send);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExecuteAfter), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("EquipCustomPlaced: PlacedImpl::ExecuteAfter hook failed, custom placed items do nothing when triggered");
        g_SendOnActivate = nullptr;
        return false;
    }
    g_Target = target;
    InstallMineCave(g_InterrogateCave, gAddr.Soldier2InterrogateUtil_CheckMineIdSite);
    InstallMineCave(g_DogCave, gAddr.BuddyDog_MineSniffIdSite);
    return true;
}

bool Uninstall_PlacedImpl_ExecuteAfter_Hook()
{
    UninstallBuddyCodeCave(g_DogCave);
    UninstallBuddyCodeCave(g_InterrogateCave);
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_SendOnActivate = nullptr;
    for (auto& announced : g_PlaceAnnounced)
        announced.store(0);
    std::lock_guard<std::mutex> lock(g_RequestLock);
    g_Requests.clear();
    return true;
}
