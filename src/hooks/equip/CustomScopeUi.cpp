#include "pch.h"
#include "CustomScopeUi.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr unsigned int kFirstOverlay = 39;
    constexpr unsigned int kLastOverlay = 99;
    constexpr unsigned int kTemplateOverlay = 15;
    constexpr std::uint64_t kTemplateWindow = 0x9ce017dc76f4ull;
    constexpr std::uint64_t kScopeLayoutAlias = 0xccd57c3edb0full;
    constexpr std::array<std::uint32_t, 39> kVanillaFactoryHashes = {
        0xbf169f98, 0xfe85dc28, 0xb977d062, 0x1fcfcb02, 0x4870e09e, 0xd1351706, 0xe508cf38, 0xb0dc435d,
        0xd0ac7a24, 0xeca40299, 0xf1073976, 0x56e753b2, 0x85a53590, 0x4217100d, 0x3f0af7fe, 0x3c2f4393,
        0x97dba7be, 0x1f607d6c, 0xb94ca953, 0xf978d371, 0x21b86dfc, 0xa435cd76, 0xe58e5a7b, 0x3ea0a455,
        0x691014dd, 0x4128e1a7, 0x57537a8c, 0x41481ba7, 0xef3c4c58, 0xf4c79e73, 0x12705704, 0xbafcf5f2,
        0xa50c4449, 0x0610783e, 0x34ed3d42, 0x90fbec98, 0xa61e9946, 0x352edc10, 0x4c71e1dc };

    struct WindowArray
    {
        std::uint32_t count;
        std::uint32_t capacity;
        void** data;
    };

    struct LayoutInfo
    {
        const char* path;
        std::uint64_t alias;
        std::uint64_t root;
        std::uint32_t field18;
        std::uint8_t field1c;
        std::uint8_t padding[3];
    };

    struct WindowInfo
    {
        std::uint64_t name;
        void* context;
        const LayoutInfo* layouts;
        void* children;
        void* replacements;
        std::uint16_t flags;
        std::uint8_t layoutCount;
        std::uint8_t childCount;
        std::uint8_t replacementCount;
        std::uint8_t padding[3];
    };

    struct ScopeFactory;

    struct FactoryVtable
    {
        void* (__fastcall* destroy)(ScopeFactory*, unsigned int);
        std::uint32_t (__fastcall* getName)(ScopeFactory*);
        void (__fastcall* create)(ScopeFactory*, const void*, WindowArray*);
        void (__fastcall* remove)(ScopeFactory*, const WindowArray*);
    };

    struct NativeFactory
    {
        const FactoryVtable* vtable;
        std::uint32_t id;
        std::uint32_t padding;
        void* handle;
    };

    struct ScopeFactory
    {
        NativeFactory native;
        std::string name;
        std::string path;
        std::uint32_t factoryHash;
        std::uint64_t windowHash;
        bool conflict = false;
        std::uint32_t liveWindows = 0;
    };

    struct CreationContext
    {
        ScopeFactory* factory;
        void* window = nullptr;
        bool captured = false;
        bool registered = false;
    };

    static_assert(sizeof(WindowArray) == 0x10);
    static_assert(sizeof(LayoutInfo) == 0x20);
    static_assert(sizeof(WindowInfo) == 0x30);
    static_assert(offsetof(WindowInfo, layoutCount) == 0x2a);
    static_assert(sizeof(NativeFactory) == 0x18);
    static_assert(offsetof(ScopeFactory, native) == 0);

    using FindFactoryFn = void* (__fastcall*)(void*, std::uint32_t);
    using CreateWindowFn = void* (__fastcall*)(void*, const WindowInfo*, unsigned int, unsigned int);
    using SetHandleFn = bool (__fastcall*)(void*, void*, unsigned int);
    using NativeCreateFn = void (__fastcall*)(NativeFactory*, const void*, WindowArray*);
    using NativeDeleteFn = void (__fastcall*)(void*, const WindowArray*);

    FindFactoryFn g_FindFactory = nullptr;
    CreateWindowFn g_CreateWindow = nullptr;
    SetHandleFn g_SetHandle = nullptr;
    NativeCreateFn g_NativeCreate = nullptr;
    NativeDeleteFn g_NativeDelete = nullptr;
    std::array<void*, 3> g_Targets{};
    std::atomic<bool> g_Ready{ false };
    std::mutex g_Mutex;
    std::map<std::uint32_t, std::unique_ptr<ScopeFactory>> g_Factories;
    std::map<unsigned int, ScopeFactory*> g_Sights;
    std::map<unsigned int, unsigned int> g_VanillaSights;
    std::array<bool, kLastOverlay + 1> g_OverlayHeld{};
    thread_local CreationContext* t_Creation = nullptr;

    unsigned int AcquireOverlayLocked(ScopeFactory* factory)
    {
        if (factory->native.id != 0)
            return factory->native.id;
        for (unsigned int id = kFirstOverlay; id <= kLastOverlay; ++id)
        {
            if (g_OverlayHeld[id])
                continue;
            g_OverlayHeld[id] = true;
            factory->native.id = id;
            return id;
        }
        return 0;
    }

    void ReleaseWindowsLocked(ScopeFactory* factory, std::uint32_t count)
    {
        factory->liveWindows = count < factory->liveWindows ? factory->liveWindows - count : 0;
        if (factory->liveWindows != 0 || factory->native.id == 0)
            return;
        g_OverlayHeld[factory->native.id] = false;
        factory->native.id = 0;
    }

    int VanillaFactoryId(const std::string& name)
    {
        if (name.empty())
            return -1;
        const std::uint32_t hash = FoxHashes::StrCode32(name);
        for (std::size_t i = 0; i < kVanillaFactoryHashes.size(); ++i)
            if (kVanillaFactoryHashes[i] == hash)
                return static_cast<int>(i);
        return -1;
    }

    bool MatchesVanillaLayout(const std::string& factoryName, const std::string& uilbPath)
    {
        constexpr std::size_t kPrefix = 5;
        constexpr std::size_t kSuffix = 7;
        if (factoryName.size() <= kPrefix + kSuffix
            || factoryName.compare(0, kPrefix, "Sight") != 0
            || factoryName.compare(factoryName.size() - kSuffix, kSuffix, "Factory") != 0)
            return false;
        std::string art = factoryName.substr(kPrefix, factoryName.size() - kPrefix - kSuffix);
        for (char& c : art)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string expected = "/Assets/tpp/ui/LayoutAsset/hud_wpscope/UI_wpscope_" + art + ".uilb";
        return _stricmp(FoxHashes::NormalizeAssetPath(uilbPath).c_str(), expected.c_str()) == 0;
    }

    void* __fastcall FactoryDestroy(ScopeFactory* factory, unsigned int)
    {
        return factory;
    }

    std::uint32_t __fastcall FactoryName(ScopeFactory* factory)
    {
        return factory->factoryHash;
    }

    void InvokeNativeFactory(NativeFactory* factory, const void* context,
                             WindowArray* windows, CreationContext* creation)
    {
        CreationContext* previous = t_Creation;
        t_Creation = creation;
        __try
        {
            g_NativeCreate(factory, context, windows);
        }
        __finally
        {
            t_Creation = previous;
        }
    }

    void __fastcall FactoryCreate(ScopeFactory* factory, const void* context,
                                   WindowArray* windows)
    {
        if (!g_Ready.load(std::memory_order_acquire) || !context || !windows)
            return;
        NativeFactory native{ factory->native.vtable, kTemplateOverlay, 0, nullptr };
        CreationContext creation{ factory };
        const std::uint32_t before = windows->count;
        InvokeNativeFactory(&native, context, windows, &creation);
        bool owned = false;
        for (std::uint32_t i = before; i < windows->count; ++i)
            owned = owned || (creation.window && windows->data[i] == creation.window);
        if (creation.registered && owned)
            return;
        for (std::uint32_t i = before; i < windows->count;)
        {
            if (windows->data[i] != creation.window)
            {
                ++i;
                continue;
            }
            --windows->count;
            std::memmove(windows->data + i, windows->data + i + 1,
                         (windows->count - i) * sizeof(void*));
        }
        if (creation.window)
        {
            WindowArray failed{ 1, 1, &creation.window };
            g_NativeDelete(factory, &failed);
        }
        if (creation.registered)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            ReleaseWindowsLocked(factory, 1);
        }
        LogWarn("ScopeUI: factory %s window create or register failed, no scope overlay shown",
            factory->name.c_str());
    }

    void __fastcall FactoryDelete(ScopeFactory* factory, const WindowArray* windows)
    {
        if (g_NativeDelete)
            g_NativeDelete(factory, windows);
        if (windows)
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            ReleaseWindowsLocked(factory, windows->count);
        }
    }

    const FactoryVtable g_Vtable{ &FactoryDestroy, &FactoryName, &FactoryCreate, &FactoryDelete };

    void* __fastcall FindFactory(void* collector, std::uint32_t hash)
    {
        void* native = g_FindFactory(collector, hash);
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_Factories.find(hash);
        if (it == g_Factories.end())
            return native;
        ScopeFactory* factory = it->second.get();
        if (native)
        {
            if (!factory->conflict)
                LogWarn("ScopeUI: factory %s collides with a native factory, custom overlay off",
                    factory->name.c_str());
            factory->conflict = true;
            return native;
        }
        return g_Ready.load(std::memory_order_acquire) && !factory->conflict ? factory : nullptr;
    }

    void* __fastcall HookCreateWindow(void* callback, const WindowInfo* info,
                                   unsigned int flags, unsigned int priority)
    {
        CreationContext* creation = t_Creation;
        if (!creation || creation->captured)
            return g_CreateWindow(callback, info, flags, priority);
        creation->captured = true;
        if (!info || info->name != kTemplateWindow || !info->layouts
            || info->layoutCount == 0 || info->layoutCount > 4)
            return nullptr;
        WindowInfo custom = *info;
        std::array<LayoutInfo, 4> layouts{};
        std::memcpy(layouts.data(), info->layouts, info->layoutCount * sizeof(LayoutInfo));
        unsigned int replaced = 0;
        for (unsigned int i = 0; i < info->layoutCount; ++i)
        {
            if (layouts[i].alias != kScopeLayoutAlias)
                continue;
            layouts[i].path = creation->factory->path.c_str();
            ++replaced;
        }
        if (replaced != 1)
            return nullptr;
        custom.name = creation->factory->windowHash;
        custom.layouts = layouts.data();
        creation->window = g_CreateWindow(callback, &custom, flags, priority);
        return creation->window;
    }

    bool __fastcall SetHandle(void* manager, void* window, unsigned int id)
    {
        CreationContext* creation = t_Creation;
        if (!creation || id != kTemplateOverlay)
            return g_SetHandle(manager, window, id);
        if (!window || !creation->captured || window != creation->window)
            return false;
        ScopeFactory* factory = creation->factory;
        unsigned int overlay = 0;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            overlay = AcquireOverlayLocked(factory);
        }
        if (overlay == 0)
        {
            LogWarn("ScopeUI: all %u custom overlay ids held by loaded scopes, factory %s shows no overlay",
                kLastOverlay - kFirstOverlay + 1, factory->name.c_str());
            return false;
        }
        creation->registered = g_SetHandle(manager, window, overlay);
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (creation->registered)
                ++factory->liveWindows;
            else
                ReleaseWindowsLocked(factory, 0);
        }
        if (!creation->registered)
            LogWarn("ScopeUI: HUD has no free slot for factory %s overlay %u, window destroyed",
                factory->name.c_str(), overlay);
        return creation->registered;
    }

    bool BytesMatch(const void* address, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return address && std::memcmp(address, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EntryMatches(const void* address, std::uintptr_t implementation,
                      const std::uint8_t* bytes, std::size_t size)
    {
        if (!address || !implementation)
            return false;
        __try
        {
            const void* expected = ResolveGameAddress(implementation);
            if (!expected)
                return false;
            if (address == expected)
                return BytesMatch(address, bytes, size);
            const auto* code = static_cast<const std::uint8_t*>(address);
            if (!code || code[0] != 0xe9)
                return false;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, code + 1, sizeof(displacement));
            const auto target = reinterpret_cast<std::uintptr_t>(code) + 5
                + static_cast<std::intptr_t>(displacement);
            return target == reinterpret_cast<std::uintptr_t>(expected);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool InstallHook(std::size_t index, void* target, void* detour, void** original)
    {
        const MH_STATUS created = MH_CreateHook(target, detour, original);
        if (created != MH_OK)
        {
            LogError("ScopeUI: hook create failed at %p (%s), custom scope UI off", target, MhStatusName(created));
            return false;
        }
        g_Targets[index] = target;
        const MH_STATUS enabled = EnableOrQueueHook(target);
        if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
        {
            LogError("ScopeUI: hook enable failed at %p (%s), custom scope UI off", target, MhStatusName(enabled));
            return false;
        }
        return true;
    }
}

namespace CustomScopeUi
{
    bool IsCreating()
    {
        return t_Creation != nullptr;
    }

    bool Install()
    {
        if (g_Ready.load(std::memory_order_acquire))
            return true;
        if (!gAddr.WindowFactoryCollector_FindWindowFactory
            || !gAddr.WindowFunction_CreateWindow
            || !gAddr.CommonDataManager_SetSubjectiveWindowHandle
            || !gAddr.WindowFactoryCollector_FindWindowFactory_Implementation
            || !gAddr.CommonDataManager_SetSubjectiveWindowHandle_Implementation)
        {
            LogError("ScopeUI: custom scope UI unsupported on %s, sights keep their borrowed overlay", GetGameBuildName(gGameBuild));
            return false;
        }
        const std::uint8_t findBytes[] = {
            0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
            0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
        const std::uint8_t createBytes[] = {
            0x48, 0x83, 0xec, 0x38, 0x44, 0x89, 0x4c, 0x24, 0x28,
            0x44, 0x89, 0x44, 0x24, 0x20, 0x45, 0x33, 0xc0 };
        const std::uint8_t setBytes[] = {
            0x45, 0x33, 0xd2, 0x48, 0x8d, 0x81, 0x80, 0x0f, 0x00, 0x00,
            0x45, 0x8b, 0xca, 0x0f, 0x1f, 0x00 };
        void* findTarget = ResolveGameAddress(gAddr.WindowFactoryCollector_FindWindowFactory);
        void* createTarget = ResolveGameAddress(gAddr.WindowFunction_CreateWindow);
        void* setTarget = ResolveGameAddress(gAddr.CommonDataManager_SetSubjectiveWindowHandle);
        if (!EntryMatches(findTarget, gAddr.WindowFactoryCollector_FindWindowFactory_Implementation,
                          findBytes, sizeof(findBytes))
            || !BytesMatch(createTarget, createBytes, sizeof(createBytes))
            || !EntryMatches(setTarget, gAddr.CommonDataManager_SetSubjectiveWindowHandle_Implementation,
                             setBytes, sizeof(setBytes)))
        {
            LogError("ScopeUI: factory hook bytes mismatch on %s, sights keep their borrowed overlay", GetGameBuildName(gGameBuild));
            return false;
        }
        g_NativeCreate = reinterpret_cast<NativeCreateFn>(
            ResolveGameAddress(gAddr.SightCommonFactory_CreateWindow));
        g_NativeDelete = reinterpret_cast<NativeDeleteFn>(
            ResolveGameAddress(gAddr.SightCommonFactory_DeleteWindow));
        if (!g_NativeCreate || !g_NativeDelete)
            return false;
        if (!InstallHook(0, createTarget, reinterpret_cast<void*>(&HookCreateWindow),
                         reinterpret_cast<void**>(&g_CreateWindow))
            || !InstallHook(1, setTarget, reinterpret_cast<void*>(&SetHandle),
                            reinterpret_cast<void**>(&g_SetHandle))
            || !InstallHook(2, findTarget, reinterpret_cast<void*>(&FindFactory),
                            reinterpret_cast<void**>(&g_FindFactory)))
        {
            Uninstall();
            return false;
        }
        g_Ready.store(true, std::memory_order_release);
        return true;
    }

    void Uninstall()
    {
        g_Ready.store(false, std::memory_order_release);
        for (std::size_t i = g_Targets.size(); i > 0; --i)
        {
            void*& target = g_Targets[i - 1];
            if (target)
                DisableAndRemoveHook(target);
            target = nullptr;
        }
    }

    bool Configure(int scopeId, const std::string& factoryName, const std::string& uilbPath)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const unsigned int key = static_cast<unsigned int>(scopeId);
        if (factoryName.empty() && uilbPath.empty())
        {
            g_Sights.erase(key);
            g_VanillaSights.erase(key);
            return true;
        }
        if (scopeId <= 24 || scopeId >= 65536)
        {
            LogWarn("SetSight %d: windowFactoryName and scopeUiPath need a custom scopeId, sight not written", scopeId);
            return false;
        }
        const int vanillaId = VanillaFactoryId(factoryName);
        if (vanillaId >= 0)
        {
            if (!uilbPath.empty() && !MatchesVanillaLayout(factoryName, uilbPath))
                LogWarn("SetSight %d: %s is a vanilla factory with its own layout, scopeUiPath ignored",
                    scopeId, factoryName.c_str());
            g_Sights.erase(key);
            g_VanillaSights[key] = static_cast<unsigned int>(vanillaId);
            return true;
        }
        const std::string path = FoxHashes::NormalizeAssetPath(uilbPath);
        if (factoryName.empty() || factoryName.find('\0') != std::string::npos
            || factoryName.size() > 255 || path.find('\0') != std::string::npos
            || path.compare(0, 8, "/Assets/") != 0 || path.size() < 6
            || _stricmp(path.c_str() + path.size() - 5, ".uilb") != 0)
        {
            LogWarn("SetSight %d: needs windowFactoryName and an /Assets/... .uilb scopeUiPath", scopeId);
            return false;
        }
        const std::uint32_t hash = FoxHashes::StrCode32(factoryName);
        const std::uint64_t windowHash = FoxHashes::StrCode64("V_Framework_Scope_" + factoryName);
        if (!hash || !windowHash)
            return false;
        auto found = g_Factories.find(hash);
        if (found != g_Factories.end())
        {
            const ScopeFactory& factory = *found->second;
            if (factory.name != factoryName || factory.path != path || factory.conflict)
            {
                LogWarn("SetSight %d: factory %s conflicts with an existing name or path, rejected",
                    scopeId, factoryName.c_str());
                return false;
            }
        }
        else
        {
            auto factory = std::make_unique<ScopeFactory>();
            factory->native = { &g_Vtable, 0, 0, nullptr };
            factory->name = factoryName;
            factory->path = path;
            factory->factoryHash = hash;
            factory->windowHash = windowHash;
            found = g_Factories.emplace(hash, std::move(factory)).first;
        }
        g_VanillaSights.erase(key);
        g_Sights[key] = found->second.get();
        return true;
    }

    void BindAlias(int scopeId, int alias)
    {
        if (scopeId <= 24 || scopeId >= 65536 || alias <= 24 || alias > 255)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const unsigned int from = static_cast<unsigned int>(scopeId);
        const unsigned int to = static_cast<unsigned int>(alias);
        const auto source = g_Sights.find(from);
        if (source != g_Sights.end())
            g_Sights[to] = source->second;
        else
            g_Sights.erase(to);
        const auto vanilla = g_VanillaSights.find(from);
        if (vanilla != g_VanillaSights.end())
            g_VanillaSights[to] = vanilla->second;
        else
            g_VanillaSights.erase(to);
    }

    bool TryGetOverlay(unsigned int scopeId, unsigned int& overlayId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto vanilla = g_VanillaSights.find(scopeId);
        if (vanilla != g_VanillaSights.end())
        {
            overlayId = vanilla->second;
            return true;
        }
        const auto found = g_Sights.find(scopeId);
        if (found == g_Sights.end() || !g_Ready.load(std::memory_order_acquire))
            return false;
        overlayId = found->second->conflict ? 0 : found->second->native.id;
        return true;
    }
}
