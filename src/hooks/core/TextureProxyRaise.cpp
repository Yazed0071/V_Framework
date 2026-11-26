#include <pch.h>
#include "TextureProxyRaise.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MinHook.h"
#include "NativeCallPatch.h"
#include "TexturePools.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kVanillaProxies = 0x1310;   // 4880
    constexpr std::uint32_t kNewProxies     = 0x2000;   // 8192, the TextureManager's slots after TexturePools

    // the streamer's per-proxy tables live inside its 0x72B20-byte object; they move to a tail behind it
    constexpr std::size_t kVanillaStreamerBytes = 0x72B20;
    constexpr std::size_t kProxyUseAt     = 0x72B40;
    constexpr std::size_t kProxyBitsAt[]  = { 0x7AB40, 0x7AF40, 0x7B340, 0x7B740 };
    constexpr std::size_t kDrawListsAt    = 0x7BB40;
    constexpr std::size_t kTaggedAt       = 0x8BB40;
    constexpr std::size_t kReleasedAt     = 0x9BB80;
    constexpr std::size_t kListAt         = 0xC0;      // the streamer's proxy list, its node code addresses it from here
    constexpr std::size_t kListRingAt     = 0xABBC0;
    constexpr std::size_t kListNodesAt    = 0xBBC00;
    constexpr std::size_t kListNodes      = kNewProxies + 2;
    constexpr std::size_t kListNodeBytes  = 0x18;
    constexpr std::size_t kStreamerBytes  = kListNodesAt + kListNodes * kListNodeBytes;

    static_assert(kProxyUseAt >= kVanillaStreamerBytes && kProxyUseAt + kNewProxies * 4 <= kProxyBitsAt[0], "use counters");
    static_assert(kProxyBitsAt[1] - kProxyBitsAt[0] >= kNewProxies / 8 && kProxyBitsAt[2] - kProxyBitsAt[1] >= kNewProxies / 8
                  && kProxyBitsAt[3] - kProxyBitsAt[2] >= kNewProxies / 8 && kDrawListsAt - kProxyBitsAt[3] >= kNewProxies / 8,
                  "proxy bits");
    static_assert(kDrawListsAt + 2 * kNewProxies * 4 <= kTaggedAt, "draw lists");
    static_assert(kTaggedAt + kNewProxies * 8 + 4 <= kReleasedAt, "tagged proxies and their count");
    static_assert(kReleasedAt + kNewProxies * 8 + 4 <= kListRingAt, "released records and their count");
    static_assert(kListRingAt + kListNodes * 8 <= kListNodesAt, "list ring");

    constexpr std::size_t kPatchBytes = 6;

    using AddressField = uintptr_t AddressSetRuntime::AddressSet::*;

    struct Site
    {
        std::uint8_t  anchor;
        std::uint16_t offset;
        std::uint8_t  length;
        std::uint8_t  field;   // where the displacement or immediate starts inside the instruction
        std::uint8_t  code[11];
        std::uint32_t raised;
    };

    enum Anchor : std::uint8_t
    {
        ProxyListInit,
        StreamerBaseCtor,
        StreamerCtor,
        ProxyUseRead,
        AddSearchTag,
        ProxyListNodeAlloc,
        CancelUpgrade,
        ProxyListReset,
        ProxyBitsClear,
        ProxyListNodeFree,
        DeleteSearchTag,
        ProxyIndexCheck,
        ReleaseQueueFlush,
        UpgradeRelease,
        DoUpgrade2,
        DoUpgradeEntry,
        ProxyBitsReset,
        GetProxy,
        ProxyBitTest,
        InitStaticResourceData,
        ProxyIndexCheck2,
        PostCreation,
        ProxyUseAdd,
        ProxyUseAddBySlot,
        SearchProxyFromPath,
        ProxyUseSub,
        ProxyUseSubBySlot,
        Planner,
        UpdateProxyLoadedStatus,
        AllocateHandlableProxy,
        DoDegrade,
        PftxsWaitForDecompression,
        ProxyBitsClear2,
        SafeFreeResourceDataMembers,
        AnchorCount
    };

    const AddressField kAnchors[AnchorCount] = {
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListInit,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_StreamerBaseCtor,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_StreamerCtor,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyUseRead,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_AddSearchTag,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListNodeAlloc,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_CancelUpgrade,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListReset,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyBitsClear,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListNodeFree,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_DeleteSearchTag,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyIndexCheck,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ReleaseQueueFlush,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_UpgradeRelease,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_DoUpgrade2,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_DoUpgradeEntry,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyBitsReset,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_GetProxy,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyBitTest,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_InitStaticResourceData,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyIndexCheck2,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_PostCreation,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyUseAdd,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyUseAddBySlot,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_SearchProxyFromPath,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyUseSub,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyUseSubBySlot,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_Planner,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_UpdateProxyLoadedStatus,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_AllocateHandlableProxy,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_DoDegrade,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_PftxsWaitForDecompression,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyBitsClear2,
        &AddressSetRuntime::AddressSet::TextureProxyRaise_SafeFreeResourceDataMembers,
    };

    const Site kSites[] = {
        { ProxyListInit, 0x05D, 8, 4, { 0x48, 0x8B, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov rdx, qword ptr [rcx + rax*8 + 0x1c9b0]
        { ProxyListInit, 0x06C, 5, 1, { 0x3D, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp eax, 0x1312
        { ProxyListInit, 0x081, 10, 6, { 0x81, 0xB9, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // cmp dword ptr [rcx + 0x26248], 0x1311
        { ProxyListInit, 0x08D, 10, 6, { 0xC7, 0x81, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26248], 0x1311
        { ProxyListInit, 0x09E, 10, 6, { 0xC7, 0x81, 0x40, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26240], 0x1311
        { ProxyListInit, 0x0BA, 8, 4, { 0x48, 0x89, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [rcx + rax*8 + 0x1c9b0], rdx
        { StreamerBaseCtor, 0x035, 5, 1, { 0xBA, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov edx, 0x1311
        { StreamerCtor, 0x05E, 5, 1, { 0xBA, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov edx, 0x1311
        { StreamerCtor, 0x124, 7, 3, { 0x48, 0x8D, 0x83, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // lea rax, [rbx + 0x2afc0]
        { StreamerCtor, 0x12B, 5, 1, { 0xB9, 0x31, 0x01, 0x00, 0x00 }, 0x200 },   // mov ecx, 0x131
        { StreamerCtor, 0x392, 7, 3, { 0x48, 0x8D, 0x83, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // lea rax, [rbx + 0x5f8f4]
        { StreamerCtor, 0x399, 5, 1, { 0xB9, 0x0F, 0x13, 0x00, 0x00 }, 0x1FFF },   // mov ecx, 0x130f
        { StreamerCtor, 0x3AB, 7, 3, { 0x44, 0x89, 0xBB, 0x74, 0x91, 0x06, 0x00 }, 0x9BB40 },   // mov dword ptr [rbx + 0x69174], r15d
        { StreamerCtor, 0x3B4, 6, 2, { 0x41, 0xB8, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov r8d, 0x9880
        { StreamerCtor, 0x3BA, 7, 3, { 0x48, 0x8D, 0x8B, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // lea rcx, [rbx + 0x5f8f4]
        { StreamerCtor, 0x3C6, 7, 3, { 0x48, 0x8D, 0x8B, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // lea rcx, [rbx + 0x69178]
        { StreamerCtor, 0x3D0, 5, 1, { 0xBA, 0x62, 0x02, 0x00, 0x00 }, 0x400 },   // mov edx, 0x262
        { StreamerCtor, 0x408, 7, 3, { 0x44, 0x89, 0xB9, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov dword ptr [rcx + 0x9880], r15d
        { StreamerCtor, 0x40F, 6, 2, { 0x41, 0xB8, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov r8d, 0x9880
        { StreamerCtor, 0x461, 7, 3, { 0x48, 0x8D, 0x8B, 0xA0, 0x0A, 0x03, 0x00 }, 0x7BB40 },   // lea rcx, [rbx + 0x30aa0]
        { StreamerCtor, 0x46A, 6, 2, { 0x41, 0xB8, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov r8d, 0x9880
        { StreamerCtor, 0x48B, 6, 2, { 0x41, 0xB8, 0x40, 0x4C, 0x00, 0x00 }, 0x8000 },   // mov r8d, 0x4c40
        { StreamerCtor, 0x491, 7, 3, { 0x48, 0x8D, 0x8B, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // lea rcx, [rbx + 0x2afc0]
        { StreamerCtor, 0x561, 7, 3, { 0x48, 0x8D, 0x8B, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // lea rcx, [rbx + 0x300a0]
        { StreamerCtor, 0x56A, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { StreamerCtor, 0x575, 7, 3, { 0x48, 0x8D, 0x8B, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // lea rcx, [rbx + 0x30310]
        { StreamerCtor, 0x57E, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { StreamerCtor, 0x589, 7, 3, { 0x48, 0x8D, 0x8B, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // lea rcx, [rbx + 0x30580]
        { StreamerCtor, 0x592, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { StreamerCtor, 0x59D, 7, 3, { 0x48, 0x8D, 0x8B, 0xF0, 0x07, 0x03, 0x00 }, 0x7B740 },   // lea rcx, [rbx + 0x307f0]
        { StreamerCtor, 0x5A6, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { StreamerCtor, 0x613, 6, 2, { 0x41, 0xB8, 0x84, 0x98, 0x00, 0x00 }, 0x10004 },   // mov r8d, 0x9884
        { StreamerCtor, 0x619, 7, 3, { 0x48, 0x8D, 0x8B, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // lea rcx, [rbx + 0x5f8f4]
        { ProxyUseRead, 0x0D7, 7, 3, { 0x8B, 0x84, 0x9E, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // mov eax, dword ptr [rsi + rbx*4 + 0x2afc0]
        { AddSearchTag, 0x011, 7, 3, { 0x44, 0x8B, 0x91, 0x74, 0x91, 0x06, 0x00 }, 0x9BB40 },   // mov r10d, dword ptr [rcx + 0x69174]
        { AddSearchTag, 0x01F, 7, 3, { 0x48, 0x8D, 0xB1, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // lea rsi, [rcx + 0x5f8f4]
        { AddSearchTag, 0x0DA, 6, 2, { 0x8B, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rsi + 0x9880]
        { AddSearchTag, 0x0ED, 6, 2, { 0x8B, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rsi + 0x9880]
        { AddSearchTag, 0x0FC, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { AddSearchTag, 0x128, 6, 2, { 0xFF, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // inc dword ptr [rsi + 0x9880]
        { AddSearchTag, 0x15B, 7, 3, { 0x41, 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp r10d, 0x1310
        { AddSearchTag, 0x188, 6, 2, { 0xFF, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // inc dword ptr [rsi + 0x9880]
        { ProxyListNodeAlloc, 0x006, 8, 4, { 0x48, 0x8B, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov rdx, qword ptr [rcx + rax*8 + 0x1c9b0]
        { ProxyListNodeAlloc, 0x018, 5, 1, { 0x3D, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp eax, 0x1312
        { ProxyListNodeAlloc, 0x027, 10, 6, { 0x81, 0xB9, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // cmp dword ptr [rcx + 0x26248], 0x1311
        { ProxyListNodeAlloc, 0x03E, 10, 6, { 0xC7, 0x81, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26248], 0x1311
        { ProxyListNodeAlloc, 0x04A, 10, 6, { 0xC7, 0x81, 0x40, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26240], 0x1311
        { ProxyListNodeAlloc, 0x066, 8, 4, { 0x48, 0x89, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [rcx + rax*8 + 0x1c9b0], rdx
        { CancelUpgrade, 0x0EB, 6, 2, { 0x8B, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rdi + 0x729f8]
        { CancelUpgrade, 0x0F1, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { CancelUpgrade, 0x0F8, 8, 4, { 0x48, 0x89, 0x8C, 0xC7, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rdi + rax*8 + 0x69178], rcx
        { CancelUpgrade, 0x100, 6, 2, { 0xFF, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rdi + 0x729f8]
        { ProxyListReset, 0x056, 8, 4, { 0x48, 0x8B, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov rdx, qword ptr [rcx + rax*8 + 0x1c9b0]
        { ProxyListReset, 0x065, 5, 1, { 0x3D, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp eax, 0x1312
        { ProxyListReset, 0x07A, 10, 6, { 0x81, 0xB9, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // cmp dword ptr [rcx + 0x26248], 0x1311
        { ProxyListReset, 0x086, 10, 6, { 0xC7, 0x81, 0x48, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26248], 0x1311
        { ProxyListReset, 0x097, 10, 6, { 0xC7, 0x81, 0x40, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [rcx + 0x26240], 0x1311
        { ProxyListReset, 0x0B3, 8, 4, { 0x48, 0x89, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [rcx + rax*8 + 0x1c9b0], rdx
        { ProxyBitsClear, 0x07C, 7, 3, { 0x21, 0x84, 0x2A, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and dword ptr [rdx + rbp + 0x300a0], eax
        { ProxyBitsClear, 0x09E, 7, 3, { 0x21, 0x84, 0x2A, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // and dword ptr [rdx + rbp + 0x30310], eax
        { ProxyBitsClear, 0x0B7, 8, 4, { 0x44, 0x21, 0x84, 0x85, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // and dword ptr [rbp + rax*4 + 0x30580], r8d
        { ProxyListNodeFree, 0x021, 7, 3, { 0x41, 0x81, 0xFA, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp r10d, 0x1312
        { ProxyListNodeFree, 0x042, 8, 4, { 0x49, 0x39, 0x94, 0xC9, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // cmp qword ptr [r9 + rcx*8 + 0x1c9b0], rdx
        { ProxyListNodeFree, 0x04E, 5, 1, { 0x3D, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp eax, 0x1312
        { ProxyListNodeFree, 0x06F, 8, 4, { 0x4B, 0x8B, 0x8C, 0xD1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov rcx, qword ptr [r9 + r10*8 + 0x1c9b0]
        { ProxyListNodeFree, 0x077, 7, 3, { 0x4C, 0x8B, 0x82, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov r8, qword ptr [rdx + 0x1c9b0]
        { ProxyListNodeFree, 0x07E, 7, 3, { 0x48, 0x89, 0x8A, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [rdx + 0x1c9b0], rcx
        { ProxyListNodeFree, 0x08C, 8, 4, { 0x4D, 0x89, 0x84, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [r9 + rax*8 + 0x1c9b0], r8
        { ProxyListNodeFree, 0x09B, 8, 4, { 0x49, 0x8B, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov rdx, qword ptr [r9 + rax*8 + 0x1c9b0]
        { ProxyListNodeFree, 0x0A3, 8, 4, { 0x4D, 0x89, 0x9C, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [r9 + rax*8 + 0x1c9b0], r11
        { ProxyListNodeFree, 0x0C0, 5, 1, { 0x3D, 0x12, 0x13, 0x00, 0x00 }, 0x2002 },   // cmp eax, 0x1312
        { ProxyListNodeFree, 0x0E8, 11, 7, { 0x41, 0xC7, 0x81, 0x44, 0x62, 0x02, 0x00, 0x11, 0x13, 0x00, 0x00 }, 0x2001 },   // mov dword ptr [r9 + 0x26244], 0x1311
        { ProxyListNodeFree, 0x0FA, 8, 4, { 0x49, 0x89, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [r9 + rax*8 + 0x1c9b0], rdx
        { ProxyListNodeFree, 0x117, 8, 4, { 0x49, 0x89, 0x94, 0xC1, 0xB0, 0xC9, 0x01, 0x00 }, 0xABB00 },   // mov qword ptr [r9 + rax*8 + 0x1c9b0], rdx
        { DeleteSearchTag, 0x021, 6, 2, { 0x8B, 0x8E, 0x74, 0x91, 0x06, 0x00 }, 0x9BB40 },   // mov ecx, dword ptr [rsi + 0x69174]
        { DeleteSearchTag, 0x027, 7, 3, { 0x48, 0x81, 0xC6, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // add rsi, 0x5f8f4
        { DeleteSearchTag, 0x0E5, 6, 2, { 0x8B, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rsi + 0x9880]
        { DeleteSearchTag, 0x0FA, 6, 2, { 0xFF, 0x8E, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // dec dword ptr [rsi + 0x9880]
        { DeleteSearchTag, 0x100, 6, 2, { 0x8B, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rsi + 0x9880]
        { ProxyIndexCheck, 0x034, 6, 2, { 0x81, 0xFB, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp ebx, 0x1310
        { ReleaseQueueFlush, 0x025, 7, 3, { 0x48, 0x8D, 0x99, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // lea rbx, [rcx + 0x69178]
        { ReleaseQueueFlush, 0x02C, 6, 2, { 0x8B, 0x83, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rbx + 0x9880]
        { ReleaseQueueFlush, 0x08C, 7, 3, { 0x41, 0x89, 0xB6, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov dword ptr [r14 + 0x729f8], esi
        { ReleaseQueueFlush, 0x095, 6, 2, { 0x89, 0xB1, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov dword ptr [rcx + 0x729f8], esi
        { UpgradeRelease, 0x38B, 6, 2, { 0x8B, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rsi + 0x729f8]
        { UpgradeRelease, 0x391, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { UpgradeRelease, 0x398, 8, 4, { 0x48, 0x89, 0x8C, 0xC6, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rsi + rax*8 + 0x69178], rcx
        { UpgradeRelease, 0x3A0, 6, 2, { 0xFF, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rsi + 0x729f8]
        { DoUpgrade2, 0x09D, 7, 3, { 0x41, 0x8B, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [r14 + 0x729f8]
        { DoUpgrade2, 0x0A4, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { DoUpgrade2, 0x0AB, 8, 4, { 0x49, 0x89, 0x8C, 0xC6, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [r14 + rax*8 + 0x69178], rcx
        { DoUpgrade2, 0x0B3, 7, 3, { 0x41, 0xFF, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [r14 + 0x729f8]
        { DoUpgradeEntry, 0x283, 6, 2, { 0x8B, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rsi + 0x729f8]
        { DoUpgradeEntry, 0x289, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { DoUpgradeEntry, 0x290, 8, 4, { 0x48, 0x89, 0x8C, 0xC6, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rsi + rax*8 + 0x69178], rcx
        { DoUpgradeEntry, 0x298, 6, 2, { 0xFF, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rsi + 0x729f8]
        { ProxyBitsReset, 0x1A5, 6, 2, { 0x21, 0x82, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and dword ptr [rdx + 0x300a0], eax
        { ProxyBitsReset, 0x1C4, 6, 2, { 0x21, 0x82, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // and dword ptr [rdx + 0x30310], eax
        { ProxyBitsReset, 0x1E3, 6, 2, { 0x21, 0x82, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // and dword ptr [rdx + 0x30580], eax
        { GetProxy, 0x009, 6, 2, { 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edx, 0x1310
        { ProxyBitTest, 0x030, 7, 3, { 0x8B, 0x84, 0x81, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // mov eax, dword ptr [rcx + rax*4 + 0x300a0]
        { InitStaticResourceData, 0x013, 5, 1, { 0xB9, 0x00, 0x7C, 0x47, 0x00 }, 0x780000 },   // mov ecx, 0x477c00
        { InitStaticResourceData, 0x025, 6, 2, { 0x41, 0xB8, 0x00, 0x7C, 0x47, 0x00 }, 0x780000 },   // mov r8d, 0x477c00
        { InitStaticResourceData, 0x049, 5, 1, { 0xB9, 0x00, 0x72, 0x15, 0x00 }, 0x240000 },   // mov ecx, 0x157200
        { InitStaticResourceData, 0x05B, 5, 1, { 0xBA, 0x20, 0x26, 0x00, 0x00 }, 0x4000 },   // mov edx, 0x2620
        { ProxyIndexCheck2, 0x029, 6, 2, { 0x81, 0xFB, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp ebx, 0x1310
        { PostCreation, 0x0E5, 6, 2, { 0x8B, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rsi + 0x729f8]
        { PostCreation, 0x0EB, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { PostCreation, 0x0F2, 8, 4, { 0x48, 0x89, 0xAC, 0xC6, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rsi + rax*8 + 0x69178], rbp
        { PostCreation, 0x0FA, 6, 2, { 0xFF, 0x86, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rsi + 0x729f8]
        { ProxyUseAdd, 0x021, 6, 2, { 0x8B, 0x86, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // mov eax, dword ptr [rsi + 0x2afc0]
        { ProxyUseAdd, 0x05A, 7, 3, { 0x48, 0x8D, 0x8E, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // lea rcx, [rsi + 0x2afc0]
        { ProxyUseAddBySlot, 0x045, 6, 2, { 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edx, 0x1310
        { SearchProxyFromPath, 0x015, 6, 2, { 0x8B, 0x81, 0x74, 0x91, 0x06, 0x00 }, 0x9BB40 },   // mov eax, dword ptr [rcx + 0x69174]
        { SearchProxyFromPath, 0x01D, 7, 3, { 0x48, 0x8D, 0xB1, 0xF4, 0xF8, 0x05, 0x00 }, 0x8BB40 },   // lea rsi, [rcx + 0x5f8f4]
        { SearchProxyFromPath, 0x0BF, 6, 2, { 0x8B, 0x86, 0x80, 0x98, 0x00, 0x00 }, 0x10000 },   // mov eax, dword ptr [rsi + 0x9880]
        { ProxyUseSub, 0x021, 6, 2, { 0x8B, 0x86, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // mov eax, dword ptr [rsi + 0x2afc0]
        { ProxyUseSub, 0x057, 7, 3, { 0x48, 0x8D, 0x8E, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // lea rcx, [rsi + 0x2afc0]
        { ProxyUseSubBySlot, 0x045, 6, 2, { 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edx, 0x1310
        { Planner, 0x032, 7, 3, { 0x48, 0x69, 0xC0, 0x40, 0x4C, 0x00, 0x00 }, 0x8000 },   // imul rax, rax, 0x4c40
        { Planner, 0x039, 7, 3, { 0x48, 0x8D, 0xBE, 0xA0, 0x0A, 0x03, 0x00 }, 0x7BB40 },   // lea rdi, [rsi + 0x30aa0]
        { Planner, 0x066, 7, 3, { 0x48, 0x69, 0xC0, 0x40, 0x4C, 0x00, 0x00 }, 0x8000 },   // imul rax, rax, 0x4c40
        { Planner, 0x06D, 6, 2, { 0x48, 0x05, 0xA0, 0x0A, 0x03, 0x00 }, 0x7BB40 },   // add rax, 0x30aa0
        { Planner, 0x143, 7, 3, { 0x48, 0x69, 0xC0, 0x40, 0x4C, 0x00, 0x00 }, 0x8000 },   // imul rax, rax, 0x4c40
        { Planner, 0x14A, 6, 2, { 0x48, 0x05, 0xA0, 0x0A, 0x03, 0x00 }, 0x7BB40 },   // add rax, 0x30aa0
        { Planner, 0x18E, 7, 3, { 0x48, 0x8D, 0x86, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // lea rax, [rsi + 0x30310]
        { Planner, 0x19C, 7, 3, { 0x48, 0x8D, 0x86, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // lea rax, [rsi + 0x30580]
        { Planner, 0x2D5, 5, 1, { 0xBA, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov edx, 0x264
        { Planner, 0x2F0, 5, 1, { 0xBA, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov edx, 0x264
        { Planner, 0x30D, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { Planner, 0x31D, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { Planner, 0x333, 7, 3, { 0x4C, 0x8D, 0x9E, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // lea r11, [rsi + 0x2afc0]
        { Planner, 0x33A, 7, 3, { 0x4C, 0x8D, 0x96, 0x80, 0xB0, 0x02, 0x00 }, 0x72C00 },   // lea r10, [rsi + 0x2b080]
        { Planner, 0x341, 5, 1, { 0xBB, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // mov ebx, 0x1310
        { Planner, 0x3A9, 5, 1, { 0xBA, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov edx, 0x264
        { Planner, 0x3C2, 6, 2, { 0x41, 0xB8, 0x64, 0x02, 0x00, 0x00 }, 0x400 },   // mov r8d, 0x264
        { Planner, 0x457, 7, 3, { 0x8B, 0xBC, 0x86, 0xF0, 0x07, 0x03, 0x00 }, 0x7B740 },   // mov edi, dword ptr [rsi + rax*4 + 0x307f0]
        { Planner, 0x45E, 7, 3, { 0x23, 0xBC, 0x86, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and edi, dword ptr [rsi + rax*4 + 0x300a0]
        { Planner, 0x53B, 7, 3, { 0x41, 0x81, 0xFE, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp r14d, 0x1310
        { Planner, 0x5C0, 8, 4, { 0x46, 0x8B, 0x8C, 0x86, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // mov r9d, dword ptr [rsi + r8*4 + 0x300a0]
        { Planner, 0xA6D, 6, 2, { 0x81, 0xFB, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp ebx, 0x1310
        { Planner, 0xEC6, 7, 3, { 0x8B, 0x9C, 0x86, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // mov ebx, dword ptr [rsi + rax*4 + 0x30580]
        { Planner, 0xED1, 7, 3, { 0x0B, 0x9C, 0x86, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // or ebx, dword ptr [rsi + rax*4 + 0x30310]
        { Planner, 0xEE2, 7, 3, { 0x23, 0x9C, 0x86, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and ebx, dword ptr [rsi + rax*4 + 0x300a0]
        { Planner, 0xFCE, 5, 1, { 0xBF, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // mov edi, 0x1310
        { Planner, 0xFDB, 6, 2, { 0x81, 0xFF, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edi, 0x1310
        { UpdateProxyLoadedStatus, 0x08A, 8, 4, { 0x41, 0x21, 0x84, 0x8A, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and dword ptr [r10 + rcx*4 + 0x300a0], eax
        { UpdateProxyLoadedStatus, 0x092, 8, 4, { 0x41, 0x21, 0x84, 0x8A, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // and dword ptr [r10 + rcx*4 + 0x30310], eax
        { UpdateProxyLoadedStatus, 0x09A, 8, 4, { 0x41, 0x21, 0x84, 0x8A, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // and dword ptr [r10 + rcx*4 + 0x30580], eax
        { UpdateProxyLoadedStatus, 0x0AB, 8, 4, { 0x45, 0x09, 0x84, 0x8A, 0xF0, 0x07, 0x03, 0x00 }, 0x7B740 },   // or dword ptr [r10 + rcx*4 + 0x307f0], r8d
        { UpdateProxyLoadedStatus, 0x0B5, 8, 4, { 0x45, 0x09, 0x84, 0x8A, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // or dword ptr [r10 + rcx*4 + 0x300a0], r8d
        { UpdateProxyLoadedStatus, 0x0C3, 8, 4, { 0x45, 0x09, 0x84, 0x8A, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // or dword ptr [r10 + rcx*4 + 0x30310], r8d
        { UpdateProxyLoadedStatus, 0x0CB, 8, 4, { 0x45, 0x09, 0x84, 0x8A, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // or dword ptr [r10 + rcx*4 + 0x30580], r8d
        { UpdateProxyLoadedStatus, 0x0E0, 8, 4, { 0x45, 0x09, 0x84, 0x8A, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // or dword ptr [r10 + rcx*4 + 0x30310], r8d
        { UpdateProxyLoadedStatus, 0x0ED, 8, 4, { 0x45, 0x21, 0x84, 0x8A, 0xF0, 0x07, 0x03, 0x00 }, 0x7B740 },   // and dword ptr [r10 + rcx*4 + 0x307f0], r8d
        { UpdateProxyLoadedStatus, 0x0FC, 8, 4, { 0x41, 0x8B, 0x84, 0x92, 0xC0, 0xAF, 0x02, 0x00 }, 0x72B40 },   // mov eax, dword ptr [r10 + rdx*4 + 0x2afc0]
        { AllocateHandlableProxy, 0x083, 6, 2, { 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edx, 0x1310
        { AllocateHandlableProxy, 0x11D, 6, 2, { 0x81, 0xFA, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp edx, 0x1310
        { DoDegrade, 0x227, 7, 3, { 0x41, 0x8B, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [r15 + 0x729f8]
        { DoDegrade, 0x22E, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { DoDegrade, 0x235, 8, 4, { 0x49, 0x89, 0xAC, 0xC7, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [r15 + rax*8 + 0x69178], rbp
        { DoDegrade, 0x23D, 7, 3, { 0x41, 0xFF, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [r15 + 0x729f8]
        { PftxsWaitForDecompression, 0x293, 6, 2, { 0x8B, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rdi + 0x729f8]
        { PftxsWaitForDecompression, 0x299, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { PftxsWaitForDecompression, 0x2A0, 8, 4, { 0x48, 0x89, 0xB4, 0xC7, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rdi + rax*8 + 0x69178], rsi
        { PftxsWaitForDecompression, 0x2A8, 6, 2, { 0xFF, 0x87, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rdi + 0x729f8]
        { ProxyBitsClear2, 0x05A, 6, 2, { 0x21, 0x82, 0xA0, 0x00, 0x03, 0x00 }, 0x7AB40 },   // and dword ptr [rdx + 0x300a0], eax
        { ProxyBitsClear2, 0x077, 6, 2, { 0x21, 0x82, 0x10, 0x03, 0x03, 0x00 }, 0x7AF40 },   // and dword ptr [rdx + 0x30310], eax
        { ProxyBitsClear2, 0x094, 6, 2, { 0x21, 0x82, 0x80, 0x05, 0x03, 0x00 }, 0x7B340 },   // and dword ptr [rdx + 0x30580], eax
        { SafeFreeResourceDataMembers, 0x01D, 6, 2, { 0x8B, 0x81, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [rcx + 0x729f8]
        { SafeFreeResourceDataMembers, 0x023, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { SafeFreeResourceDataMembers, 0x02A, 8, 4, { 0x4C, 0x89, 0x84, 0xC1, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [rcx + rax*8 + 0x69178], r8
        { SafeFreeResourceDataMembers, 0x032, 6, 2, { 0xFF, 0x81, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [rcx + 0x729f8]
        { SafeFreeResourceDataMembers, 0x04D, 7, 3, { 0x41, 0x8B, 0x81, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // mov eax, dword ptr [r9 + 0x729f8]
        { SafeFreeResourceDataMembers, 0x054, 5, 1, { 0x3D, 0x10, 0x13, 0x00, 0x00 }, 0x2000 },   // cmp eax, 0x1310
        { SafeFreeResourceDataMembers, 0x05B, 8, 4, { 0x49, 0x89, 0x8C, 0xC1, 0x78, 0x91, 0x06, 0x00 }, 0x9BB80 },   // mov qword ptr [r9 + rax*8 + 0x69178], rcx
        { SafeFreeResourceDataMembers, 0x063, 7, 3, { 0x41, 0xFF, 0x81, 0xF8, 0x29, 0x07, 0x00 }, 0xABB80 },   // inc dword ptr [r9 + 0x729f8]
    };

    constexpr std::size_t kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

    constexpr std::size_t kListInitOff   = 0x22;
    constexpr std::size_t kListInitBytes = 30;
    const std::uint8_t kListInitCode[kListInitBytes] = {
        0x48, 0x8D, 0x81, 0xB0, 0xC9, 0x01, 0x00,                                // lea rax,[rcx+0x1c9b0]: the free ring
        0x48, 0x8B, 0xD1,                                                        // mov rdx,rcx: the first node
        0x41, 0xB8, 0x12, 0x13, 0x00, 0x00,                                      // mov r8d,0x1312: nodes
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
    constexpr AddressField kListInits[] = { &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListInit,
                                            &AddressSetRuntime::AddressSet::TextureProxyRaise_ProxyListReset };
    constexpr std::size_t kListInitCount = sizeof(kListInits) / sizeof(kListInits[0]);
    constexpr std::size_t kListInitParts = kListInitBytes / kPatchBytes;
    static_assert(kListInitBytes % kPatchBytes == 0, "list init goes in 6-byte parts");

    constexpr std::size_t kPatchCount = kSiteCount + kListInitCount * kListInitParts;

    const std::uint8_t kStreamerCtorEntry[] = { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56,
                                                0x41, 0x57, 0x48, 0x83, 0xEC, 0x58 };
    const std::uint8_t kPoolCtorEntry[]     = { 0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xFF, 0x8B, 0xEA,
                                                0x48, 0x8B, 0xD9, 0x48, 0x89, 0x39 };

    constexpr std::size_t kPoolMaxLiveOff = 0x04;
    constexpr std::size_t kPoolLiveOff    = 0x20;

    enum class State : int
    {
        NotAttempted,
        AddressMissing,
        SlotsVanilla,
        OtherCopy,
        AlreadyBuilt,
        AlreadyRaised,
        SiteUnexpected,
        HookFailed,
        PatchRefused,
        Applied,
    };

    enum class Fallback : int
    {
        None,
        TakenBack,
        NoRoom,
    };

    using StreamerCtor_t = void*(__fastcall*)(void*);
    using PoolCtor_t     = std::uint32_t*(__fastcall*)(std::uint32_t*, std::uint32_t);
    using GameMalloc_t   = void*(__fastcall*)(std::size_t);

    std::atomic<State>         g_State{ State::NotAttempted };
    std::uint8_t*              g_Anchors[AnchorCount] = {};
    std::uint8_t*              g_StreamerCtor = nullptr;
    std::uint8_t*              g_PoolCtor     = nullptr;
    std::uint8_t**             g_PoolSlot     = nullptr;
    std::uint8_t**             g_StreamerSlot = nullptr;
    StreamerCtor_t             g_OrigStreamerCtor = nullptr;
    PoolCtor_t                 g_OrigPoolCtor     = nullptr;
    GameMalloc_t               g_GameMalloc       = nullptr;
    NativeCodePatch            g_Patches[kPatchCount] = {};
    HANDLE                     g_Marker = nullptr;
    std::atomic<bool>          g_BuildRoomy{ false };
    std::atomic<bool>          g_RaisePool{ false };
    std::atomic<void*>         g_RoomyStreamer{ nullptr };
    std::atomic<std::uint32_t> g_PoolAsked{ 0 };
    std::atomic<Fallback>      g_Fallback{ Fallback::None };
    DWORD                      g_PatchError = 0;
    char                       g_Found[384] = {};
    char                       g_StreamerCtorHook[160] = {};
    char                       g_PoolCtorHook[160] = {};

    bool ClaimMarker()
    {
        wchar_t name[80]{};
        swprintf_s(name, L"Local\\V_FrameWork_TextureProxyRaise_%lu", GetCurrentProcessId());
        g_Marker = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(std::uint32_t), name);
        if (!g_Marker)
            return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return true;   // the handle stays open for the whole process
        CloseHandle(g_Marker);
        g_Marker = nullptr;
        return false;
    }

    bool IsBuilt(std::uint8_t** instance)
    {
        std::uint8_t* object = nullptr;
        return !ReadEntryBytesSEH(instance, &object, sizeof(object)) || object;
    }

    std::uint8_t* SiteAt(const Site& site)
    {
        return g_Anchors[site.anchor] + site.offset;
    }

    void RaisedCode(const Site& site, std::uint8_t* out)
    {
        std::memcpy(out, site.code, site.length);
        std::memcpy(out + site.field, &site.raised, sizeof(site.raised));
    }

    void RaisedListInit(std::uint8_t (&out)[kListInitBytes])
    {
        const std::uint32_t ring  = static_cast<std::uint32_t>(kListRingAt - kListAt);
        const std::uint32_t nodes = static_cast<std::uint32_t>(kListNodesAt - kListAt);
        const std::uint32_t count = static_cast<std::uint32_t>(kListNodes);
        const std::uint8_t code[kListInitBytes] = {
            0x48, 0x8D, 0x81, 0, 0, 0, 0,                                        // lea rax,[rcx+ring]
            0x48, 0x8D, 0x91, 0, 0, 0, 0,                                        // lea rdx,[rcx+nodes]
            0x41, 0xB8, 0, 0, 0, 0,                                              // mov r8d,count
            0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(out, code, sizeof(code));
        std::memcpy(out + 3, &ring, sizeof(ring));
        std::memcpy(out + 10, &nodes, sizeof(nodes));
        std::memcpy(out + 16, &count, sizeof(count));
    }

    void DescribeCode(const char* what, const void* at, const std::uint8_t* code, std::size_t len)
    {
        int used = std::snprintf(g_Found, sizeof(g_Found), "%s at %p:", what, at);
        for (std::size_t i = 0; i < len && used > 0 && static_cast<std::size_t>(used) < sizeof(g_Found); ++i)
            used += std::snprintf(g_Found + used, sizeof(g_Found) - used, " %02X", code[i]);
    }

    void QueueSitePatch(const Site& site, NativeCodePatch& patch)
    {
        const std::size_t end   = site.field + sizeof(site.raised);
        const std::size_t first = end > kPatchBytes ? end - kPatchBytes : 0;
        std::uint8_t raised[sizeof(Site::code)] = {};
        RaisedCode(site, raised);
        patch = NativeCodePatch{};
        patch.target = SiteAt(site) + first;
        patch.size   = static_cast<unsigned char>(end - first);
        std::memcpy(patch.expected, site.code + first, patch.size);
        std::memcpy(patch.replacement, raised + first, patch.size);
    }

    bool IsProxyPool(const std::uint32_t* pool)
    {
        std::uint8_t* current = nullptr;
        return ReadEntryBytesSEH(g_PoolSlot, &current, sizeof(current))
            && current == reinterpret_cast<const std::uint8_t*>(pool);
    }

    bool PoolNumbers(std::uint32_t& maxLive, std::uint32_t& live)
    {
        std::uint8_t* pool = nullptr;
        return ReadEntryBytesSEH(g_PoolSlot, &pool, sizeof(pool)) && pool
            && ReadEntryBytesSEH(pool + kPoolMaxLiveOff, &maxLive, sizeof(maxLive))
            && ReadEntryBytesSEH(pool + kPoolLiveOff, &live, sizeof(live));
    }

    bool OperatePatches(bool install)
    {
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            const bool done = install ? InstallNativeCodePatches(g_Patches, kPatchCount)
                                      : RemoveNativeCodePatches(g_Patches, kPatchCount);
            if (done)
                return true;
            g_PatchError = GetLastError();
            if (!NativePatchErrorIsTransient(g_PatchError))
                return false;
            Sleep(4);
        }
        return false;
    }

    std::uint32_t* __fastcall hkPoolCtor(std::uint32_t* pool, std::uint32_t count)
    {
        if (g_RaisePool.load(std::memory_order_acquire) && IsProxyPool(pool))
        {
            g_PoolAsked.store(count);
            if (count == kVanillaProxies || count > kNewProxies)   // the streamer's tables hold kNewProxies, no more
                count = kNewProxies;
        }
        return g_OrigPoolCtor(pool, count);
    }

    void* BuildWithoutRoom(void* block)
    {
        g_RaisePool = false;
        if (OperatePatches(false))   // no streamer exists yet, so no thread runs the code being put back
        {
            std::uint32_t maxLive = 0;
            std::uint32_t live    = 0;
            std::uint8_t* pool    = nullptr;
            if (PoolNumbers(maxLive, live) && maxLive > kVanillaProxies && live == 0
                && ReadEntryBytesSEH(g_PoolSlot, &pool, sizeof(pool)))
                g_OrigPoolCtor(reinterpret_cast<std::uint32_t*>(pool), kVanillaProxies);   // its first arrays stay allocated
            g_BuildRoomy = false;
            g_Fallback = Fallback::TakenBack;
            return g_OrigStreamerCtor(block);
        }
        g_Fallback = Fallback::NoRoom;
        void* room = VirtualAlloc(nullptr, kStreamerBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        return g_OrigStreamerCtor(room ? room : block);
    }

    void* __fastcall hkStreamerCtor(void* block)
    {
        if (!g_BuildRoomy.load(std::memory_order_acquire))
            return g_OrigStreamerCtor(block);
        void* roomy = g_GameMalloc(kStreamerBytes);
        if (!roomy)
            roomy = g_GameMalloc(kStreamerBytes);
        if (!roomy)
            return BuildWithoutRoom(block);
        std::memset(roomy, 0, kStreamerBytes);
        void* built = g_OrigStreamerCtor(roomy);   // every caller keeps the returned object; its own block stays unused
        g_RoomyStreamer.store(built);
        return built;
    }
}

void PreApplyTextureProxyRaise()
{
    State expected = State::NotAttempted;
    if (!g_State.compare_exchange_strong(expected, State::AddressMissing))
        return;
    if (!TexturePoolsSlotsRaised())
    {
        g_State = State::SlotsVanilla;
        return;
    }

    for (int i = 0; i < AnchorCount; ++i)
    {
        g_Anchors[i] = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*kAnchors[i]));
        if (!g_Anchors[i])
            return;
    }
    std::uint8_t* listInits[kListInitCount] = {};
    for (std::size_t i = 0; i < kListInitCount; ++i)
    {
        listInits[i] = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.*kListInits[i]));
        if (!listInits[i])
            return;
        listInits[i] += kListInitOff;
    }
    g_StreamerCtor = g_Anchors[StreamerCtor];
    g_PoolCtor     = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.TextureProxyRaise_PoolCtor));
    g_GameMalloc   = reinterpret_cast<GameMalloc_t>(ResolveGameAddress(gAddr.TextureProxyRaise_GameMalloc));
    g_PoolSlot     = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TexturePools_ProxyPool));
    g_StreamerSlot = static_cast<std::uint8_t**>(ResolveGameAddress(gAddr.TextureStoragePool_Streamer));
    if (!g_PoolCtor || !g_GameMalloc || !g_PoolSlot || !g_StreamerSlot)
        return;

    if (!ClaimMarker())
    {
        g_State = State::OtherCopy;
        return;
    }
    if (IsBuilt(g_PoolSlot) || IsBuilt(g_StreamerSlot))
    {
        g_State = State::AlreadyBuilt;
        return;
    }

    std::size_t raisedSites = 0;
    for (const Site& site : kSites)
    {
        std::uint8_t now[sizeof(Site::code)] = {};
        std::uint8_t raised[sizeof(Site::code)] = {};
        RaisedCode(site, raised);
        if (!ReadEntryBytesSEH(SiteAt(site), now, site.length))
        {
            std::snprintf(g_Found, sizeof(g_Found), "texture streamer code at %p unreadable", SiteAt(site));
            g_State = State::SiteUnexpected;
            return;
        }
        if (std::memcmp(now, raised, site.length) == 0)
        {
            ++raisedSites;
            continue;
        }
        if (std::memcmp(now, site.code, site.length) != 0)
        {
            DescribeCode("texture streamer code", SiteAt(site), now, site.length);
            g_State = State::SiteUnexpected;
            return;
        }
    }
    std::uint8_t raisedInit[kListInitBytes] = {};
    RaisedListInit(raisedInit);
    for (std::uint8_t* at : listInits)
    {
        std::uint8_t now[kListInitBytes] = {};
        const bool readable = ReadEntryBytesSEH(at, now, sizeof(now));
        if (readable && std::memcmp(now, raisedInit, sizeof(now)) == 0)
        {
            ++raisedSites;
            continue;
        }
        if (!readable || std::memcmp(now, kListInitCode, sizeof(now)) != 0)
        {
            DescribeCode("texture streamer proxy list setup", at, now, sizeof(now));
            g_State = State::SiteUnexpected;
            return;
        }
    }
    if (raisedSites == kSiteCount + kListInitCount)
    {
        g_State = State::AlreadyRaised;
        return;
    }
    if (raisedSites != 0)
    {
        std::snprintf(g_Found, sizeof(g_Found), "%zu of %zu texture streamer sites already changed by another module",
                      raisedSites, kSiteCount + kListInitCount);
        g_State = State::SiteUnexpected;
        return;
    }

    const HookEntry streamerEntry = CheckHookEntry(g_StreamerCtor, kStreamerCtorEntry, sizeof(kStreamerCtorEntry));
    const HookEntry poolEntry     = CheckHookEntry(g_PoolCtor, kPoolCtorEntry, sizeof(kPoolCtorEntry));
    if (streamerEntry == HookEntry::Unknown || poolEntry == HookEntry::Unknown)
    {
        std::uint8_t* odd = streamerEntry == HookEntry::Unknown ? g_StreamerCtor : g_PoolCtor;
        char entry[160] = {};
        DescribeFunctionEntry(odd, entry, sizeof(entry));
        std::snprintf(g_Found, sizeof(g_Found), "%s constructor %p: %s",
                      odd == g_StreamerCtor ? "texture streamer" : "proxy pool", odd, entry);
        g_State = State::SiteUnexpected;
        return;
    }
    if (streamerEntry == HookEntry::ForeignHook)
        DescribeFunctionEntry(g_StreamerCtor, g_StreamerCtorHook, sizeof(g_StreamerCtorHook));
    if (poolEntry == HookEntry::ForeignHook)
        DescribeFunctionEntry(g_PoolCtor, g_PoolCtorHook, sizeof(g_PoolCtorHook));

    const MH_STATUS mh = MH_Initialize();
    if ((mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED)
        || !CreateAndEnableHook(g_PoolCtor, reinterpret_cast<void*>(&hkPoolCtor), reinterpret_cast<void**>(&g_OrigPoolCtor))
        || !CreateAndEnableHook(g_StreamerCtor, reinterpret_cast<void*>(&hkStreamerCtor),
                                reinterpret_cast<void**>(&g_OrigStreamerCtor)))
    {
        g_State = State::HookFailed;   // a hook that went in passes straight through: neither switch below is on
        return;
    }
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&hkStreamerCtor), &pinned);   // the two hooks stay for the whole process

    for (std::size_t i = 0; i < kSiteCount; ++i)
        QueueSitePatch(kSites[i], g_Patches[i]);
    for (std::size_t list = 0; list < kListInitCount; ++list)
        for (std::size_t part = 0; part < kListInitParts; ++part)
        {
            NativeCodePatch& patch = g_Patches[kSiteCount + list * kListInitParts + part];
            patch = NativeCodePatch{};
            patch.target = listInits[list] + part * kPatchBytes;
            patch.size   = static_cast<unsigned char>(kPatchBytes);
            std::memcpy(patch.expected, kListInitCode + part * kPatchBytes, kPatchBytes);
            std::memcpy(patch.replacement, raisedInit + part * kPatchBytes, kPatchBytes);
        }

    g_BuildRoomy = true;   // a streamer built from here on gets the room whether or not the code below goes in
    if (!OperatePatches(true))
    {
        g_BuildRoomy = false;
        g_State = State::PatchRefused;
        return;
    }
    g_RaisePool = true;
    g_State = State::Applied;
}

void LogTextureProxyRaise()
{
    switch (g_State.load())
    {
    case State::Applied:
        LogInfo("TexturePools: streamer proxies %u -> %u", kVanillaProxies, kNewProxies);
        if (g_StreamerCtorHook[0])
            LogInfo("TexturePools: the texture streamer constructor was already hooked by another module (%s), "
                    "V_FrameWork's hook sits on top", g_StreamerCtorHook);
        if (g_PoolCtorHook[0])
            LogInfo("TexturePools: the streamer proxy pool constructor was already hooked by another module (%s), "
                    "V_FrameWork's hook sits on top", g_PoolCtorHook);
        break;
    case State::NotAttempted:
    case State::SlotsVanilla:
        LogInfo("TexturePools: streamer proxies stay %u, the TextureManager slots were not raised", kVanillaProxies);
        break;
    case State::AddressMissing:
        LogWarn("TexturePools: streamer proxy addresses missing on %s, streamer proxies stay %u",
                GetGameBuildName(gGameBuild), kVanillaProxies);
        break;
    case State::OtherCopy:
        LogWarn("TexturePools: another copy in this process already raises the streamer proxies, V_FrameWork leaves "
                "them to it");
        break;
    case State::AlreadyBuilt:
        LogWarn("TexturePools: the texture streamer was built before V_FrameWork loaded, streamer proxies stay %u",
                kVanillaProxies);
        break;
    case State::AlreadyRaised:
        LogInfo("TexturePools: another module already raised the streamer proxies to %u the same way, left as they are",
                kNewProxies);
        break;
    case State::SiteUnexpected:
        LogError("TexturePools: %s, streamer proxies stay %u", g_Found[0] ? g_Found : "texture streamer code unexpected",
                 kVanillaProxies);
        break;
    case State::HookFailed:
        LogError("TexturePools: texture streamer constructor hooks failed, streamer proxies stay %u", kVanillaProxies);
        break;
    case State::PatchRefused:
        LogError("TexturePools: texture streamer patch refused (%s), nothing of it written, streamer proxies stay %u",
                 NativePatchErrorName(g_PatchError), kVanillaProxies);
        break;
    }
}

void NoteTextureProxyRaise()
{
    static bool fallbackNoted = false;
    static bool poolNoted     = false;
    static bool askedNoted    = false;
    static bool streamerNoted = false;
    static bool siteNoted[kPatchCount] = {};
    if (g_State.load() != State::Applied)
        return;

    const Fallback fallback = g_Fallback.load();
    if (fallback != Fallback::None && !fallbackNoted)
    {
        fallbackNoted = true;
        if (fallback == Fallback::TakenBack)
            LogError("TexturePools: no memory for the texture streamer's bigger tables, the raise was taken back, "
                     "streamer proxies stay %u", kVanillaProxies);
        else
            LogError("TexturePools: no memory for the texture streamer's bigger tables and another module changed the "
                     "streamer code so the raise could not be taken back; the streamer runs in memory the game will "
                     "free wrongly when it closes");
    }

    std::uint32_t maxLive = 0;
    std::uint32_t live    = 0;
    if (!poolNoted && PoolNumbers(maxLive, live) && maxLive != 0)
    {
        poolNoted = true;
        if (maxLive == kNewProxies)
            LogInfo("TexturePools: texture streamer proxy pool built with %u proxies", maxLive);
        else
            LogWarn("TexturePools: texture streamer proxy pool built with %u proxies, not %u", maxLive, kNewProxies);
    }
    const std::uint32_t asked = g_PoolAsked.load();
    if (!askedNoted && asked != 0 && asked != kVanillaProxies)
    {
        askedNoted = true;
        if (asked > kNewProxies)
            LogError("TexturePools: another module asked for %u streamer proxies; the streamer's tables hold %u, so the "
                     "pool got %u", asked, kNewProxies, kNewProxies);
        else
            LogWarn("TexturePools: another module asked for %u streamer proxies, kept (the streamer's tables hold %u)",
                    asked, kNewProxies);
    }

    std::uint8_t* streamer = nullptr;
    if (!streamerNoted && ReadEntryBytesSEH(g_StreamerSlot, &streamer, sizeof(streamer)) && streamer
        && streamer != g_RoomyStreamer.load() && g_Fallback.load() == Fallback::None)
    {
        streamerNoted = true;
        LogError("TexturePools: the texture streamer at %p was not built by V_FrameWork's constructor hook (another "
                 "module's hook skipped it), streamed textures past proxy %u overwrite memory", streamer, kVanillaProxies);
    }

    for (std::size_t i = 0; i < kPatchCount; ++i)
    {
        const NativeCodePatch& patch = g_Patches[i];
        std::uint8_t now[kPatchBytes] = {};
        if (siteNoted[i] || !patch.installed || !ReadEntryBytesSEH(patch.target, now, patch.size)
            || std::memcmp(now, patch.replacement, patch.size) == 0)
            continue;
        siteNoted[i] = true;
        DescribeCode("code", patch.target, now, patch.size);
        LogWarn("TexturePools: another module changed texture streamer code after V_FrameWork set it (%s)", g_Found);
    }
}
