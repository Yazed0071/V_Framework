#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace outfit::headmotion
{
    struct Clip
    {
        std::string   name;
        std::uint64_t code = 0;
    };

    struct Effect
    {
        std::string   name;
        std::uint64_t vfx   = 0;
        std::uint64_t point = 0;
        std::string   clip;
    };

    std::uint64_t ParseClipId(const char* text);
    std::uint64_t ParseEffectPoint(const char* name);

    void SetBranchMotion(const char* headKey, std::uint8_t playerType,
                         std::uint64_t mtarCode, std::vector<Clip> clips);
    void SetBranchEffects(const char* headKey, std::uint8_t playerType, std::vector<Effect> effects);
    void SetBranchRest(const char* headKey, std::uint8_t playerType, const char* restClip);

    bool Play(const char* clipName);
    bool PlayEffect(const char* name);
    bool StopEffect(const char* name);
    bool PlaySound(const char* eventName, const char* pointName);

    using EffectUpdate_t         = bool (__fastcall*)(void* instance, float delta, void* context);
    using EffectContextAcquire_t = void* (__fastcall*)(void* pool);

    std::uintptr_t WorkActMotionAddress();
    std::uintptr_t EffectUpdateAddress();
    std::uintptr_t EffectContextAcquireAddress();
    void SetEffectUpdateOriginals(EffectUpdate_t update, EffectContextAcquire_t acquire);
    void NoteEffectContextPool(void* pool);
    bool DeferEffectUpdate(void* instance, float delta);
    void OnWorkActMotion(void* work, const float* timeBase);
    void OnPlayerPartsReloaded();
    void Shutdown();
}
