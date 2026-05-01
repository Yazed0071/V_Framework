#include "pch.h"
#include "RangeAttackEffects.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "SetSupportWeaponTypeId.h"

// Custom-throwable chaff disruption.
//
// The engine's chaff pipeline is two queues hanging off the
// RangeAttackSystemImpl singleton (located via QuarkSystem→ApplicationSystem
// at +0x250 — store-back from the impl's constructor at
// mgsvtpp.exe.c:4355113):
//
//   +0xE0  pending queue (4 slots × 0x30 bytes — ChaffRequest structs)
//   +0x100 active  queue (4 slots × 0x40 bytes — ActiveChaff entries)
//
// `RequestToChaff` (retail 0x1412E0930 → .text 0x149D3B110) enqueues a
// ChaffRequest into the pending queue and returns the allocated request id.
// Per-frame, `UpdateChaff` promotes pending into active (`ActivateChaff`),
// at which point the engine's downstream consumers — security-camera AI
// (`StartChaff`/`EndChaff`), command-post radio (`InterruptRadioByChaff`,
// `StepRadioChaff`), soldier radio actions (`State_RadioChaffEnd`), and
// the visual effect (`CreateChaffEffect`) — all kick in for free.
//
// Two entry points:
//   1. Lua primitive  V_FrameWork.RequestChaffAt(x, y, z, radius, duration)
//      — direct trigger from Lua, useful for events that aren't tied to
//      a thrown projectile.
//   2. Auto-trigger via `chaffEffect = { radius, duration }` on a
//      registered support-weapon category. The hooks below intercept
//      ThrowingImpl::UpdateActionGrenade / UpdateActionSmoke and call
//      RequestChaffAt at the projectile's landing position when the
//      throwable's equipId is bound to such a category. The engine's
//      dispatcher only invokes UpdateAction* once the projectile's
//      "exploding now" flag (bit 0x8000000 of state +0x1c) is set, so
//      first call ≡ landing — exactly when chaff should fire.

namespace
{
    using GetQuarkSystemTable_t = void* (__cdecl*)();
    using RequestToChaff_t      = std::uint64_t (__fastcall*)(void* impl, void* req);
    using UpdateActionGrenade_t = void (__fastcall*)(
        void* this_, std::uint32_t projIdx, std::uint32_t equipId,
        std::int32_t param4, float param5);
    using UpdateActionSmoke_t   = void (__fastcall*)(
        void* this_, std::uint32_t projIdx, std::uint32_t equipId,
        std::uint8_t isFinish);

    RangeAttackEffects::Deps g_Deps{};
    bool g_DepsBound = false;

    UpdateActionGrenade_t g_OrigUpdateActionGrenade = nullptr;
    bool g_HookGrenadeInstalled = false;

    UpdateActionSmoke_t g_OrigUpdateActionSmoke = nullptr;
    bool g_HookSmokeInstalled = false;

    // ChaffRequest layout (0x30 bytes total) — reverse-engineered from the
    // RequestToChaff body (mgsvtpp.exe.c:7091027) which copies fields by
    // exact byte offset from the source struct into the pending-queue slot:
    //
    //   +0x00  ushort  category/type header (vanilla supply-call uses 0)
    //   +0x10..+0x1B  vec3 position (xyz floats — read by CreateChaffEffect
    //                  at named-build line 2927674 as `*(undefined1[12]*)
    //                  (param_1 + 0x10)`).
    //   +0x1C..+0x1F  unused (vec4 w slot)
    //   +0x20  float   radius (meters; copied verbatim into ActiveChaff
    //                  entry +0x20)
    //   +0x24  float   duration (seconds; copied into ActiveChaff +0x2C)
    //   +0x28  byte    flag bits — only bit 0 propagates (the function does
    //                  `dst.bit0 = src.bit0`). Vanilla helicopter chaff and
    //                  player-thrown chaff both set bit 0 = 1.
    //   +0x2A  ushort  request id slot — engine overwrites with the
    //                  allocated id on success.
    constexpr std::size_t kChaffRequestSize = 0x30;

    void* ResolveRangeAttackSystemImplBase()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(
            ResolveGameAddress(gAddr.fox_GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;

        void* qs = getQuark();
        if (!qs)
            return nullptr;

        // QuarkSystemTable.ApplicationSystem lives at +0x98 (struct field).
        std::uint8_t* applicationSystem =
            *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(qs) + 0x98);
        if (!applicationSystem)
            return nullptr;

        // The RangeAttackSystem vtable subobject (impl+0x20) is stored in
        // ApplicationSystem at +0x250 by the impl's constructor. The retail
        // RequestToChaff function expects `this = impl + 0` (it reads
        // queue offsets +0xB8, +0xE0, +0xAC directly relative to that
        // base), so we subtract 0x20 to undo the subobject adjustment.
        std::uint8_t* rangeAttackSubobject =
            *reinterpret_cast<std::uint8_t**>(applicationSystem + 0x250);
        if (!rangeAttackSubobject)
            return nullptr;

        return rangeAttackSubobject - 0x20;
    }

    bool DoRequestChaffAt(float x, float y, float z, float radius, float duration)
    {
        auto fn = reinterpret_cast<RequestToChaff_t>(
            ResolveGameAddress(gAddr.RangeAttackSystemImpl_RequestToChaff));
        if (!fn)
        {
            Log("[RangeAttackEffects] RequestChaffAt: RequestToChaff address not configured (build is JP / address missing)\n");
            return false;
        }

        void* implBase = ResolveRangeAttackSystemImplBase();
        if (!implBase)
        {
            Log("[RangeAttackEffects] RequestChaffAt: RangeAttackSystemImpl singleton not available — engine not initialized?\n");
            return false;
        }

        // Build a 0x30-byte request struct on the stack with exact byte
        // offsets matching the engine's reads. Zero-initialize first so
        // unused/padding bytes don't carry stack garbage.
        alignas(8) std::uint8_t req[kChaffRequestSize];
        std::memset(req, 0, sizeof(req));

        *reinterpret_cast<float*>(req + 0x10) = x;
        *reinterpret_cast<float*>(req + 0x14) = y;
        *reinterpret_cast<float*>(req + 0x18) = z;
        *reinterpret_cast<float*>(req + 0x1C) = 1.0f;          // vec4 w
        *reinterpret_cast<float*>(req + 0x20) = radius;
        *reinterpret_cast<float*>(req + 0x24) = duration;
        req[0x28] = 0x01;                                      // player-source flag

        const std::uint64_t id = fn(implBase, req);
        const bool ok = (static_cast<std::uint16_t>(id) != 0);
        Log("[RangeAttackEffects] RequestChaffAt: pos=(%.2f, %.2f, %.2f) radius=%.2f duration=%.2f -> id=0x%X (%s)\n",
            x, y, z, radius, duration,
            static_cast<unsigned int>(id),
            ok ? "ok" : "queue-full");
        return ok;
    }

    // ---- Auto-trigger plumbing ----
    //
    // For each (projectile-slot, equipId) we fire chaff exactly once. The
    // engine's UpdateAction* runs every frame after landing, so we need a
    // tracker. Slot addresses are stable while the projectile is alive;
    // when a slot is reused for a new projectile, the equipId changes —
    // we detect that and re-fire for the new throwable.
    std::mutex g_FiredMutex;
    // Key = state-slot pointer (state struct lives at *(this+0x128)+0x20 +
    //       projIdx*0x24 — unique per active projectile slot).
    // Value = equipId we last fired for in that slot.
    std::unordered_map<std::uintptr_t, std::uint32_t> g_FiredByStateSlot;

    bool ShouldFireOnce(std::uintptr_t slotAddr, std::uint32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_FiredMutex);
        const auto it = g_FiredByStateSlot.find(slotAddr);
        if (it != g_FiredByStateSlot.end() && it->second == equipId)
            return false;
        g_FiredByStateSlot[slotAddr] = equipId;
        return true;
    }

    // Returns the address of the per-slot state block at (state_array_base
    // + 0x20 + projIdx * 0x24). State array base = *(this+0x128). Returns
    // 0 if the layout doesn't deref cleanly.
    std::uintptr_t ResolveStateSlotAddr(void* this_, std::uint32_t projIdx)
    {
        if (!this_)
            return 0;
        std::uint8_t* base = *reinterpret_cast<std::uint8_t**>(
            reinterpret_cast<std::uint8_t*>(this_) + 0x128);
        if (!base)
            return 0;
        return reinterpret_cast<std::uintptr_t>(
            base + 0x20 + static_cast<std::ptrdiff_t>(projIdx) * 0x24);
    }

    void MaybeFireChaffForThrowable(void* this_, std::uint32_t projIdx, std::uint32_t equipId)
    {
        float radius = 0.0f;
        float duration = 0.0f;
        if (!SupportWeaponType::TryGetChaffEffectForEquipId(
                static_cast<int>(equipId), radius, duration))
            return;

        const std::uintptr_t slotAddr = ResolveStateSlotAddr(this_, projIdx);
        if (!slotAddr)
            return;

        if (!ShouldFireOnce(slotAddr, equipId))
            return;

        // Projectile world position table at *(this+0x110), 0x10 stride
        // per slot (vec4 layout — first 12 bytes are xyz floats; w is
        // either 1.0 or unused). Read at named-build line 4905295:
        //   pfVar2 = *(this + 0x110) + projIdx * 0x10
        std::uint8_t* posTableBase =
            *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(this_) + 0x110);
        if (!posTableBase)
            return;

        const float* pos =
            reinterpret_cast<const float*>(posTableBase + static_cast<std::ptrdiff_t>(projIdx) * 0x10);

        Log("[RangeAttackEffects] auto-chaff for equipId=0x%X projIdx=%u pos=(%.2f, %.2f, %.2f) radius=%.1f duration=%.1f\n",
            equipId, projIdx, pos[0], pos[1], pos[2], radius, duration);
        (void)DoRequestChaffAt(pos[0], pos[1], pos[2], radius, duration);
    }

    void __fastcall hkUpdateActionGrenade(
        void* this_, std::uint32_t projIdx, std::uint32_t equipId,
        std::int32_t param4, float param5)
    {
        if (g_OrigUpdateActionGrenade)
            g_OrigUpdateActionGrenade(this_, projIdx, equipId, param4, param5);

        // Parent dispatcher (ThrowingImpl::UpdateAction at mgsvtpp.exe.c:
        // 2847070) only invokes UpdateActionGrenade once bit 0x8000000 of
        // state +0x1c is set — the "this projectile is now exploding"
        // gate. So every call here is on/after landing; firing on first
        // observed (slot, equipId) puts the chaff on the same frame as
        // the engine's native explosion.
        MaybeFireChaffForThrowable(this_, projIdx, equipId);
    }

    void __fastcall hkUpdateActionSmoke(
        void* this_, std::uint32_t projIdx, std::uint32_t equipId,
        std::uint8_t isFinish)
    {
        if (g_OrigUpdateActionSmoke)
            g_OrigUpdateActionSmoke(this_, projIdx, equipId, isFinish);

        // Same dispatch gate as Grenade — bit 0x8000000 of state +0x1c —
        // so first call is the landing tick.
        MaybeFireChaffForThrowable(this_, projIdx, equipId);
    }
}

namespace RangeAttackEffects
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
        g_DepsBound = true;
    }

    bool RequestChaffAt(float x, float y, float z, float radius, float duration)
    {
        return DoRequestChaffAt(x, y, z, radius, duration);
    }

    int __cdecl Lua_RequestChaffAt(lua_State* L)
    {
        if (!L || !g_DepsBound || !g_Deps.ResolveLuaApi || !g_Deps.ResolveLuaApi())
            return 0;
        if (!g_Deps.LuaType || !g_Deps.GetLuaNumber || !g_Deps.PushLuaNumber)
            return 0;

        constexpr int kLuaTNumber = 3;

        const int t1 = g_Deps.LuaType(L, 1);
        const int t2 = g_Deps.LuaType(L, 2);
        const int t3 = g_Deps.LuaType(L, 3);
        if (t1 != kLuaTNumber || t2 != kLuaTNumber || t3 != kLuaTNumber)
        {
            Log("[RangeAttackEffects] Lua_RequestChaffAt: expected (number x, number y, number z [, number radius=15] [, number duration=20])\n");
            g_Deps.PushLuaNumber(L, 0.0f);
            return 1;
        }

        const float x = g_Deps.GetLuaNumber(L, 1);
        const float y = g_Deps.GetLuaNumber(L, 2);
        const float z = g_Deps.GetLuaNumber(L, 3);

        float radius   = 15.0f;
        float duration = 20.0f;
        if (g_Deps.LuaType(L, 4) == kLuaTNumber)
            radius = g_Deps.GetLuaNumber(L, 4);
        if (g_Deps.LuaType(L, 5) == kLuaTNumber)
            duration = g_Deps.GetLuaNumber(L, 5);

        const bool ok = DoRequestChaffAt(x, y, z, radius, duration);
        g_Deps.PushLuaNumber(L, ok ? 1.0f : 0.0f);
        return 1;
    }

    bool Install_ThrowingImpl_UpdateActionGrenade_Hook()
    {
        if (g_HookGrenadeInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateActionGrenade);
        if (!target)
        {
            Log("[RangeAttackEffects] UpdateActionGrenade: address not configured (build is JP / address missing) — auto-trigger disabled for grenade behavior\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkUpdateActionGrenade),
            reinterpret_cast<void**>(&g_OrigUpdateActionGrenade));
        if (!ok)
        {
            Log("[RangeAttackEffects] Failed to install UpdateActionGrenade hook\n");
            return false;
        }

        g_HookGrenadeInstalled = true;
        Log("[RangeAttackEffects] UpdateActionGrenade hook installed\n");
        return true;
    }

    bool Uninstall_ThrowingImpl_UpdateActionGrenade_Hook()
    {
        if (!g_HookGrenadeInstalled)
            return true;
        if (void* target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateActionGrenade))
            DisableAndRemoveHook(target);
        g_OrigUpdateActionGrenade = nullptr;
        g_HookGrenadeInstalled = false;
        return true;
    }

    bool Install_ThrowingImpl_UpdateActionSmoke_Hook()
    {
        if (g_HookSmokeInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateActionSmoke);
        if (!target)
        {
            Log("[RangeAttackEffects] UpdateActionSmoke: address not configured (build is JP / address missing) — auto-trigger disabled for smoke behavior\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkUpdateActionSmoke),
            reinterpret_cast<void**>(&g_OrigUpdateActionSmoke));
        if (!ok)
        {
            Log("[RangeAttackEffects] Failed to install UpdateActionSmoke hook\n");
            return false;
        }

        g_HookSmokeInstalled = true;
        Log("[RangeAttackEffects] UpdateActionSmoke hook installed\n");
        return true;
    }

    bool Uninstall_ThrowingImpl_UpdateActionSmoke_Hook()
    {
        if (!g_HookSmokeInstalled)
            return true;
        if (void* target = ResolveGameAddress(gAddr.ThrowingImpl_UpdateActionSmoke))
            DisableAndRemoveHook(target);
        g_OrigUpdateActionSmoke = nullptr;
        g_HookSmokeInstalled = false;
        return true;
    }
}
