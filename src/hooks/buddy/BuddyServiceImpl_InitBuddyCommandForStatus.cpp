#include "pch.h"

#include "BuddyServiceImpl_InitBuddyCommandForStatus.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BuddyCodeCave.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    BuddyCodeCave g_StatusCave{ "D-Dog gear order for status" };
    BuddyCodeCave g_VariantCave{ "D-Dog gear order variant" };

    constexpr std::uint8_t kInitCommandBytes[] =
    {
        0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00, 0x80, 0xF9, 0x08, 0x72, 0x16, 0xB8, 0x0B, 0x00, 0x00,
        0x00, 0x89, 0x83, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20,
        0x5F, 0xC3, 0x0F, 0xB6, 0xC1, 0x48, 0x8D, 0x0D, 0x88, 0x92, 0x86, 0x01,
    };
    constexpr std::uint8_t kCommandVariantBytes[] =
    {
        0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00, 0x80, 0xF9, 0x08, 0x73, 0x37, 0x84, 0xC9, 0x74, 0x33,
        0x0F, 0xB6, 0xC1, 0x48, 0x8D, 0x0D, 0x9B, 0x94, 0x86, 0x01, 0x8B, 0x04, 0x81, 0x89, 0x43, 0x7C,
        0xC6, 0x83, 0x71, 0x01, 0x00, 0x00, 0x01, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
    };

    constexpr std::size_t   kPatchSize       = 12;   // movzx ecx, gear + cmp cl, 8 + jcc8
    constexpr std::uint8_t  kKeepBusyCommand = 0x0B;

    std::uintptr_t ResolveWindow(std::uintptr_t siteAbs, const std::uint8_t* bytes, std::size_t size, const char* name)
    {
        const auto* site = static_cast<const std::uint8_t*>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!site || std::memcmp(site, bytes, size) != 0)
        {
            LogError("BuddyOutfit: %s code differs on this build, that part is off", name);
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(site);
    }

    void EmitCustomCommandLoad(CaveCode& c)
    {
        c.Emit({ 0x0F, 0xB6, 0xC1 });                           // movzx eax, cl
        c.Emit({ 0x48, 0xB9 });                                 // mov rcx, order table
        c.Imm64(reinterpret_cast<std::uint64_t>(buddyoutfit::DogCommands()));
        c.Emit({ 0x8B, 0x04, 0x81 });                           // mov eax, [rcx+rax*4]
    }

    bool InstallStatus()
    {
        const std::uintptr_t siteAbs = gAddr.BuddyServiceImpl_InitBuddyCommandForStatus_GearCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kInitCommandBytes, sizeof(kInitCommandBytes), g_StatusCave.name);
        if (!base)
            return false;

        CaveCode c;
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x80, 0xF9, 0x08 });                           // cmp cl, 8
        const std::size_t custom = c.Jcc8(0x73);                // jae custom
        c.JmpAbs(base + 0x22);
        c.Bind8(custom);
        EmitCustomCommandLoad(c);
        c.JmpAbs(base + 0x11);

        return InstallBuddyCodeCave(g_StatusCave, siteAbs, kInitCommandBytes, kPatchSize, c);
    }

    bool InstallVariant()
    {
        const std::uintptr_t siteAbs = gAddr.BuddyServiceImpl_DogCommandVariant_GearCheck;
        const std::uintptr_t base = ResolveWindow(siteAbs, kCommandVariantBytes, sizeof(kCommandVariantBytes), g_VariantCave.name);
        if (!base)
            return false;
        const std::uintptr_t keepDefault = base + 0x43;

        CaveCode c;
        c.Emit({ 0x0F, 0xB6, 0x88, 0x49, 0x2E, 0x00, 0x00 });   // movzx ecx, byte [rax+0x2E49]
        c.Emit({ 0x80, 0xF9, 0x08 });                           // cmp cl, 8
        const std::size_t custom = c.Jcc8(0x73);                // jae custom
        c.JmpAbs(base + kPatchSize);
        c.Bind8(custom);
        EmitCustomCommandLoad(c);
        c.Emit({ 0x83, 0xF8, kKeepBusyCommand });               // cmp eax, keep busy
        const std::size_t store = c.Jcc8(0x75);                 // jne store
        c.JmpAbs(keepDefault);
        c.Bind8(store);
        c.JmpAbs(base + 0x1D);

        return InstallBuddyCodeCave(g_VariantCave, siteAbs, kCommandVariantBytes, kPatchSize, c);
    }
}

bool Install_BuddyServiceImpl_InitBuddyCommandForStatus_Patches()
{
    bool ok = InstallStatus();
    ok &= InstallVariant();
    return ok;
}

bool Uninstall_BuddyServiceImpl_InitBuddyCommandForStatus_Patches()
{
    UninstallBuddyCodeCave(g_VariantCave);
    UninstallBuddyCodeCave(g_StatusCave);
    return true;
}
