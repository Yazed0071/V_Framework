#include "pch.h"

#include "SimplePartsEffectControllerImpl_CreateEffectImpl.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CreateEffectImpl_t   = void(__fastcall*)(void* self, std::uint32_t index, std::uint64_t name,
                                                   bool oneShot, bool immediate, std::uint32_t variation);
    using EffectCreate_t       = void**(__fastcall*)(void** out, void* desc);
    using GetEffectDescIndex_t = std::uint32_t(__fastcall*)(void* parts, std::uint32_t index, std::uint64_t name);

    CreateEffectImpl_t g_OrigCreateEffectImpl   = nullptr;
    EffectCreate_t     g_OrigEffectCreate       = nullptr;
    void*              g_CreateEffectImplTarget = nullptr;
    void*              g_EffectCreateTarget     = nullptr;

    constexpr std::uint64_t kNameMask    = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kMuzzleFlash = 0x5CFEED1BBBB6ull; // MuzzleFlash
    constexpr std::uint64_t kBackFire    = 0xFB95F09098D6ull; // BackFire
    constexpr std::uint64_t kTailSmoke   = 0x2B5968253BBAull; // TailSmoke

    constexpr std::size_t kSlot_GetEffectDescIndex = 0x218 / sizeof(void*);
    constexpr int         kMaxLines                = 60;

    std::atomic<std::uint64_t> g_QuietShotTick{ 0 };
    std::atomic<int>           g_Lines{ 0 };

    thread_local bool  t_Tracking = false;
    thread_local bool  t_Created  = false;
    thread_local void* t_Vfx      = nullptr;
    thread_local void* t_Effect   = nullptr;

    const char* NameOf(std::uint64_t name)
    {
        switch (name & kNameMask)
        {
        case kMuzzleFlash: return "MuzzleFlash";
        case kBackFire:    return "BackFire";
        case kTailSmoke:   return "TailSmoke";
        default:           return nullptr;
        }
    }

    std::uint32_t DescIndexSEH(void* self, std::uint32_t index, std::uint64_t name)
    {
        __try
        {
            void* parts = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + 0x38);
            const auto fn = reinterpret_cast<GetEffectDescIndex_t>((*reinterpret_cast<void***>(parts))[kSlot_GetEffectDescIndex]);
            return fn(parts, index, name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFFFFFFu;
        }
    }

    void** __fastcall hkEffectCreate(void** out, void* desc)
    {
        void** result = g_OrigEffectCreate(out, desc);
        if (t_Tracking)
        {
            t_Created = true;
            t_Vfx     = desc ? *reinterpret_cast<void**>(static_cast<std::uint8_t*>(desc) + 0x40) : nullptr;
            t_Effect  = result ? *result : nullptr;
        }
        return result;
    }

    void __fastcall hkCreateEffectImpl(void* self, std::uint32_t index, std::uint64_t name,
                                       bool oneShot, bool immediate, std::uint32_t variation)
    {
        const char* label = NameOf(name);
        const std::uint64_t sinceQuietShot = GetTickCount64() - g_QuietShotTick.load();
        const bool track = label && g_Lines.load() < kMaxLines
            && ((name & kNameMask) != kMuzzleFlash || sinceQuietShot < 500);
        if (!track)
        {
            g_OrigCreateEffectImpl(self, index, name, oneShot, immediate, variation);
            return;
        }

        const std::uint32_t descIndex = DescIndexSEH(self, index, name);
        t_Tracking = true;
        t_Created  = false;
        t_Vfx      = nullptr;
        t_Effect   = nullptr;
        g_OrigCreateEffectImpl(self, index, name, oneShot, immediate, variation);
        t_Tracking = false;

        ++g_Lines;
        LogInfo("FxDiag %s controller:%p index:%u descIndex:%u oneShot:%d created:%d vfx:%p effect:%p sinceQuietShot:%llums",
            label, self, index, descIndex, oneShot ? 1 : 0, t_Created ? 1 : 0, t_Vfx, t_Effect,
            static_cast<unsigned long long>(sinceQuietShot));
    }
}

void ShellFxDiag_NoteQuietShot(void* realizedEquipObject, std::uint32_t slot, std::uint32_t flagsIn,
                               std::uint32_t flagsOut, const std::uint8_t* gunInfo)
{
    if (g_Lines.load() >= kMaxLines)
        return;
    __try
    {
        if (gunInfo[0x7E] != 3)
            return;
        void* effects = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(realizedEquipObject) + 0x40);
        g_QuietShotTick = GetTickCount64();
        ++g_Lines;
        LogInfo("FxDiag QuietShot realized:%p effects:%p slot:%u flags:0x%X->0x%X gunType:%u",
            realizedEquipObject, effects, slot, flagsIn, flagsOut, static_cast<unsigned>(gunInfo[0x78]));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

bool Install_SimplePartsEffectControllerImpl_CreateEffectImpl_Hooks()
{
    if (g_CreateEffectImplTarget)
        return true;
    void* createImpl = ResolveGameAddress(gAddr.SimplePartsEffectControllerImpl_CreateEffectImpl);
    void* effectCreate = ResolveGameAddress(gAddr.FoxFxEffect_Create);
    if (!createImpl || !effectCreate)
    {
        LogWarn("FxDiag: addresses missing, effect diagnostic off");
        return true;
    }
    if (!CreateAndEnableHook(effectCreate, reinterpret_cast<void*>(&hkEffectCreate),
                             reinterpret_cast<void**>(&g_OrigEffectCreate)))
    {
        LogWarn("FxDiag: Effect::Create hook failed, effect diagnostic off");
        return true;
    }
    if (!CreateAndEnableHook(createImpl, reinterpret_cast<void*>(&hkCreateEffectImpl),
                             reinterpret_cast<void**>(&g_OrigCreateEffectImpl)))
    {
        DisableAndRemoveHook(effectCreate);
        g_OrigEffectCreate = nullptr;
        LogWarn("FxDiag: CreateEffectImpl hook failed, effect diagnostic off");
        return true;
    }
    g_EffectCreateTarget = effectCreate;
    g_CreateEffectImplTarget = createImpl;
    return true;
}

bool Uninstall_SimplePartsEffectControllerImpl_CreateEffectImpl_Hooks()
{
    if (!g_CreateEffectImplTarget)
        return true;
    DisableAndRemoveHook(g_CreateEffectImplTarget);
    DisableAndRemoveHook(g_EffectCreateTarget);
    g_CreateEffectImplTarget = nullptr;
    g_EffectCreateTarget = nullptr;
    g_OrigCreateEffectImpl = nullptr;
    g_OrigEffectCreate = nullptr;
    return true;
}
