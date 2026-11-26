#include <pch.h>

#include <Windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "DevelopArrayGrow.h"
#include "EquipDevelop_SetEquipUndeveloped.h"
#include "GunBasicInject.h"
#include "../outfit/EquipDevelopControllerImpl_GetSuitDevelopInfoIndex.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "EquipAliasLease.h"
#include "EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex.h"

namespace equip
{
    namespace
    {
        constexpr std::uint32_t kOldRows        = 1024;
        constexpr std::uint32_t kNewRows        = kMaxFlowSlots;
        constexpr std::uint32_t kRecStride      = 0x68;
        constexpr std::uint32_t kParStride      = 0x10;
        constexpr std::uint32_t kOldAllocSize   = 0x1FFD0;
        constexpr std::uint32_t kOldCtlOff      = 0x1E028;
        constexpr std::uint32_t kNewCtlOff      = 0x700000;
        constexpr std::uint32_t kCtlSize        = kOldAllocSize - kOldCtlOff;
        constexpr std::uint32_t kCtlDelta       = kNewCtlOff - kOldCtlOff;
        constexpr std::uint32_t kNewParallelOff =
            (kNewCtlOff + kCtlSize + 0xF) & ~0xFu;
        constexpr std::uint32_t kNewAllocSize   =
            kNewParallelOff + kNewRows * kParStride;
        constexpr std::uint32_t kNewRecMemset   = kNewRows * kRecStride;
        constexpr std::uint32_t kNewParMemset   = kNewRows * kParStride;
        constexpr std::uint32_t kNewParDisp20   = kNewParallelOff - 0x20;
        constexpr std::uint32_t kWorkBufDisp    = 0x1FEC0;
        constexpr std::uint32_t kNullSinkOff    = kNewCtlOff + kCtlSize;
        constexpr std::uint32_t kOldEquipEquipedWords = 0x20;
        constexpr std::uint32_t kNewEquipEquipedWords = (kNewRows + 31) / 32;

        static_assert(0x28 + kNewRows * kRecStride <= kNewCtlOff,
                      "records must not overrun the relocated control fields");
        static_assert(kNullSinkOff + 8 <= kNewParallelOff,
                      "the always-zero qword must fit between the control fields "
                      "and the parallel array");
        static_assert((kNullSinkOff & 7) == 0,
                      "the always-zero qword must be 8-byte aligned");
        static_assert(kNewCtlOff + kCtlSize <= kNewParallelOff,
                      "control fields must not overrun the parallel array");
        static_assert(kOldEquipEquipedWords * 32 == kOldRows,
                      "the vanilla equip-equiped bitset spans exactly 1024 rows");
        static_assert(kNewEquipEquipedWords * 32 >= kNewRows,
                      "the grown equip-equiped bitset must span every grown row");

        struct ImmPatch
        {
            std::uintptr_t addr;
            const char*    what;
            std::uint8_t   opLen;
            std::uint8_t   op[5];
            std::uint32_t  oldImm;
            std::uint32_t  newImm;
        };

        const ImmPatch kPatchTemplate[] = {
            { 0, "block alloc size",     1, { 0xB9 },                         0x1FFD0, kNewAllocSize },
            { 0, "ctor loop count",      1, { 0xB9 },                         0x3FF,   kNewRows - 1 },
            { 0, "ctor parallel base",   3, { 0x48, 0x8D, 0x83 },             0x1A028, kNewParallelOff },
            { 0, "reset records size",   2, { 0x41, 0xB8 },                   0x1A000, kNewRecMemset },
            { 0, "reset parallel base",  3, { 0x48, 0x8D, 0x8B },             0x1A028, kNewParallelOff },
            { 0, "reset parallel size",  2, { 0x41, 0xB8 },                   0x4000,  kNewParMemset },
            { 0, "regist scan bound",    1, { 0xB9 },                         0x400,   kNewRows },
            { 0, "regist probe disp",    5, { 0x66, 0x43, 0x83, 0xBC, 0xC1 }, 0x1A008, kNewParDisp20 },
            { 0, "regist store disp+0",  4, { 0x41, 0x89, 0x84, 0xC9 },       0x1A008, kNewParDisp20 },
            { 0, "regist store disp+4",  4, { 0x41, 0x89, 0x84, 0xC9 },       0x1A00C, kNewParDisp20 + 4 },
            { 0, "regist store disp+8",  4, { 0x41, 0x89, 0x84, 0xC9 },       0x1A010, kNewParDisp20 + 8 },
            { 0, "regist store disp+C",  4, { 0x41, 0x89, 0x84, 0xC9 },       0x1A014, kNewParDisp20 + 12 },

            { 0, "equipid-from-idx bound",   1, { 0xB8 },       0x400, kNewRows },
            { 0, "is-visible bound",         1, { 0xB8 },       0x400, kNewRows },
            { 0, "grade getter bound",       1, { 0xB8 },       0x400, kNewRows },
            { 0, "packed getter bound",      1, { 0xB8 },       0x400, kNewRows },
            { 0, "count-by-type scan bound", 2, { 0x41, 0xB8 }, 0x400, kNewRows },
            { 0, "provider count bound",     2, { 0x41, 0xBF }, 0x400, kNewRows },
            { 0, "provider fill bound",      2, { 0x41, 0xBD }, 0x400, kNewRows },
            { 0, "menu list gate bound",     1, { 0xB8 },       0x400, kNewRows },

            { 0, "equip-equiped bits alloc words", 1, { 0xB9 },
              kOldEquipEquipedWords, kNewEquipEquipedWords },
            { 0, "equip-equiped bits clear size",  2, { 0x41, 0xB8 },
              kOldEquipEquipedWords * 4, kNewEquipEquipedWords * 4 },
        };

        constexpr std::size_t kPatchCount =
            sizeof(kPatchTemplate) / sizeof(kPatchTemplate[0]);

        ImmPatch kPatches[kPatchCount] = {};

        struct ByteFix
        {
            std::uintptr_t addr;
            const char*    what;
            std::uint8_t   len;
            std::uint8_t   oldBytes[4];
            std::uint8_t   newBytes[4];
        };

        const ByteFix kByteFixTemplate[] = {
            { 0, "sibling retire loop index init", 3,
              { 0x40, 0x32, 0xF6 },       { 0x33, 0xF6, 0x90, 0x00 } },
            { 0, "sibling retire loop index read", 4,
              { 0x40, 0x0F, 0xB6, 0xC6 }, { 0x8B, 0xC6, 0x90, 0x90 } },
            { 0, "sibling retire loop index inc",  3,
              { 0x40, 0xFE, 0xC6 },       { 0xFF, 0xC6, 0x90, 0x00 } },
            { 0, "sibling retire loop index test", 4,
              { 0x40, 0x0F, 0xB6, 0xC6 }, { 0x8B, 0xC6, 0x90, 0x90 } },
            { 0, "develop-list count miss branch", 2,
              { 0x73, 0x09 },             { 0x74, 0x09 } },
            { 0, "develop-list apply miss branch", 4,
              { 0x0F, 0x83, 0xFA, 0x00 }, { 0x0F, 0x84, 0xFA, 0x00 } },
        };

        constexpr std::size_t kByteFixCount =
            sizeof(kByteFixTemplate) / sizeof(kByteFixTemplate[0]);

        ByteFix kByteFixes[kByteFixCount] = {};

        constexpr std::uint16_t kSiblingListCap = 1024;

        using SiblingCount_t =
            std::uint16_t (__fastcall*)(void*, std::uint16_t, char);
        SiblingCount_t g_OrigSiblingCount = nullptr;
        bool           g_ClampInstalled   = false;

        using IsVisible_t = bool (__fastcall*)(void*, std::uint16_t);
        IsVisible_t   g_OrigIsVisible      = nullptr;
        bool          g_IsVisibleInstalled = false;

        bool          g_VisArmed  = false;
        bool          g_VisValid  = false;
        std::uint64_t g_VisStamp  = 0;

        constexpr std::uint64_t kVisTtlMs = 250;
        void*         g_VisSelf   = nullptr;
        DWORD         g_VisThread = 0;
        std::uint8_t  g_VisCache[kNewRows] = {};

        std::uint16_t g_CustomBluePrintDevelopIds[kNewRows] = {};

        constexpr std::size_t  kRecDevelopIdOff             = 0x8;
        constexpr std::size_t  kRecEquipIdOff               = 0xC;   // equipId << 4
        constexpr std::size_t  kRecDisplayInfoOff           = 0x58;
        constexpr std::uint8_t kDisplayInfoMask             = 0x7;
        constexpr std::uint8_t kDisplayInfoSkillOrBluePrint = 1;
        constexpr std::uint8_t kDisplayInfoBluePrintOnly    = 4;

        struct CtlDispSite
        {
            std::uintptr_t addr;
            std::uint32_t  oldDisp;
            std::uint8_t   dispOff;
            std::uint8_t   maxOff;
        };

        struct BoundImmSite
        {
            std::uintptr_t addr;
            std::uint8_t   immOff;
        };

        using AddrField = std::uintptr_t AddressSetRuntime::AddressSet::*;
        using AS = AddressSetRuntime::AddressSet;

        struct CtlDispSiteField
        {
            AddrField     addr;
            std::uint32_t oldDisp;
            std::uint8_t  dispOff;
            std::uint8_t  maxOff;
        };

        struct BoundImmSiteField
        {
            AddrField    addr;
            std::uint8_t immOff;
        };

        static const CtlDispSiteField kCtlDispSiteFields[] = {
            { &AS::DevelopArrayGrow_CtlDispSite001, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite002, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite003, 0x1E038u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite004, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite005, 0x1FE60u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite006, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite007, 0x1FE70u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite008, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite009, 0x1FE80u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite010, 0x1FE88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite011, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite012, 0x1FE98u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite013, 0x1FEA0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite014, 0x1FEA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite015, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite016, 0x1FEB8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite017, 0x1FEC0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite018, 0x1FEC8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite019, 0x1FFC5u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite020, 0x1FFC7u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite021, 0x1FFC8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite022, 0x1E038u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite023, 0x1FED0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite024, 0x1FF50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite025, 0x1FF58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite026, 0x1FF60u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite027, 0x1FF68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite028, 0x1FF70u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite029, 0x1FF78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite030, 0x1FF80u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite031, 0x1FF88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite032, 0x1FF90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite033, 0x1FF98u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite034, 0x1FF9Cu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite035, 0x1FF9Eu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite036, 0x1FFA6u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite037, 0x1FFAEu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite038, 0x1FFB6u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite039, 0x1FFBEu, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite040, 0x1FFC2u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite041, 0x1FFC4u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite042, 0x1FEC0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite043, 0x1FEC0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite044, 0x1FEC0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite045, 0x1FEC0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite046, 0x1FFA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite047, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite048, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite049, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite050, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite051, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite052, 0x1FE88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite053, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite054, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite055, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite056, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite057, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite058, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite059, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite060, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite061, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite062, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite063, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite064, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite065, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite066, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite067, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite068, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite069, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite070, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite071, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite072, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite073, 0x1FE60u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite074, 0x1FE60u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite075, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite076, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite077, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite078, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite079, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite080, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite081, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite082, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite083, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite084, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite085, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite086, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite087, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite088, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite089, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite090, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite091, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite092, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite093, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite094, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite095, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite096, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite097, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite098, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite099, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite100, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite101, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite102, 0x1FE70u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite103, 0x1FE40u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite104, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite105, 0x1FEA8u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite106, 0x1FE80u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite107, 0x1FFA6u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite108, 0x1FF7Eu, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite109, 0x1FF30u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite110, 0x1FF30u, 255, 5 },
            { &AS::DevelopArrayGrow_CtlDispSite111, 0x1FF7Eu, 255, 5 },
            { &AS::DevelopArrayGrow_CtlDispSite112, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite113, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite114, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite115, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite116, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite117, 0x1FE40u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite118, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite119, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite120, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite121, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite122, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite123, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite124, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite125, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite126, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite127, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite128, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite129, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite130, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite131, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite132, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite133, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite134, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite135, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite136, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite137, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite138, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite139, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite140, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite141, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite142, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite143, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite144, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite145, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite146, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite147, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite148, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite149, 0x1FE70u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite150, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite151, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite152, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite153, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite154, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite155, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite156, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite157, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite158, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite159, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite160, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite161, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite162, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite163, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite164, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite165, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite166, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite167, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite168, 0x1FE18u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite169, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite170, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite171, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite172, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite173, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite174, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite175, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite176, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite177, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite178, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite179, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite180, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite181, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite182, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite183, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite184, 0x1FEACu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite185, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite186, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite187, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite188, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite189, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite190, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite191, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite192, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite193, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite194, 0x1FEADu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite195, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite196, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite197, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite198, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite199, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite200, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite201, 0x1FEA0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite202, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite203, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite204, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite205, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite206, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite207, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite208, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite209, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite210, 0x1FFC6u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite211, 0x1FEA0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite212, 0x1FEA0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite213, 0x1FFA5u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite214, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite215, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite216, 0x1FFA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite217, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite218, 0x1FFA7u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite219, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite220, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite221, 0x1FE50u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite222, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite223, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite224, 0x1FEACu, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite225, 0x1FEACu, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite226, 0x1FFA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite227, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite228, 0x1FEADu, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite229, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite230, 0x1FFA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite231, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite232, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite233, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite234, 0x1FE60u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite235, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite236, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite237, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite238, 0x1FEA0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite239, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite240, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite241, 0x1FF7Eu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite242, 0x1FF86u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite243, 0x1FF8Eu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite244, 0x1FF96u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite245, 0x1FF9Eu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite246, 0x1FFA2u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite247, 0x1FFA4u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite248, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite249, 0x1FFA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite250, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite251, 0x1E008u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite252, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite253, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite254, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite255, 0x1FFA5u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite256, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite257, 0x1FE38u, 255, 6 },
            { &AS::DevelopArrayGrow_CtlDispSite258, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite259, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite260, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite261, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite262, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite263, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite264, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite265, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite266, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite267, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite268, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite269, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite270, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite271, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite272, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite273, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite274, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite275, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite276, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite277, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite278, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite279, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite280, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite281, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite282, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite283, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite284, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite285, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite286, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite287, 0x1FE18u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite288, 0x1FE38u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite289, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite290, 0x1FE3Cu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite291, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite292, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite293, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite294, 0x1E030u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite295, 0x1E028u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite296, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite297, 0x1FE48u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite298, 0x1FE50u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite299, 0x1FE58u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite300, 0x1FE60u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite301, 0x1FE68u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite302, 0x1FE70u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite303, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite304, 0x1FE80u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite305, 0x1FE88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite306, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite307, 0x1FE80u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite308, 0x1FE98u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite309, 0x1FFA6u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite310, 0x1FECEu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite311, 0x1FEC8u, 255, 5 },
            { &AS::DevelopArrayGrow_CtlDispSite312, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite313, 0x1FE98u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite314, 0x1FE98u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite315, 0x1FEA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite316, 0x1FED0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite317, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite318, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite319, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite320, 0x1FED0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite321, 0x1FECEu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite322, 0x1FEA8u, 255, 5 },
            { &AS::DevelopArrayGrow_CtlDispSite323, 0x1FEAEu, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite324, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite325, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite326, 0x1FE78u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite327, 0x1FE88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite328, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite329, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite330, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite331, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite332, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite333, 0x1FEAEu, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite334, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite335, 0x1FEA8u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite336, 0x1FE40u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite337, 0x1FFA5u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite338, 0x1FFA7u, 2, 2 },
            { &AS::DevelopArrayGrow_CtlDispSite339, 0x1FEADu, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite340, 0x1FEA8u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite341, 0x1FEA8u, 255, 4 },
            { &AS::DevelopArrayGrow_CtlDispSite342, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite343, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite344, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite345, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite346, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite347, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite348, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite349, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite350, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite351, 0x1E010u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite352, 0x1E018u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite353, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite354, 0x1FE88u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite355, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite356, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite357, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite358, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite359, 0x1FE90u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite360, 0x1FEB0u, 255, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite361, 0x1E008u, 3, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite362, 0x1FE58u, 3, 3 },
            { &AS::DevelopArrayGrow_CtlDispSite363, 0x1FE58u, 3, 3 },
        };

        static const BoundImmSiteField kBoundImmSiteFields[] = {
            { &AS::DevelopArrayGrow_BoundImmSite01, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite02, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite03, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite04, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite05, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite06, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite07, 2 },
            { &AS::DevelopArrayGrow_GetBaseId, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite09, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite10, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite11, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite12, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite13, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite14, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite15, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite16, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite17, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite18, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite19, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite20, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite21, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite22, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite23, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite24, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite25, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite26, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite27, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite28, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite29, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite30, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite31, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite32, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite33, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite34, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite35, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite36, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite37, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite38, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite39, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite40, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite41, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite42, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite43, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite44, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite45, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite46, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite47, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite48, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite49, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite50, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite51, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite52, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite53, 1 },
            { &AS::EquipDevelopCtrl_SetEnableDevelop, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite55, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite56, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite57, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite58, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite59, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite60, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite61, 2 },
            { &AS::DevelopArrayGrow_BoundImmSite62, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite63, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite64, 1 },
            { &AS::DevelopArrayGrow_BoundImmSite65, 3 },
        };

        constexpr std::size_t kMaxCtlSites = std::size(kCtlDispSiteFields);

        const CtlDispSite*  kCtlDispSites  = nullptr;
        const BoundImmSite* kBoundImmSites = nullptr;
        std::size_t         kCtlSiteCount   = 0;
        std::size_t         kBoundSiteCount = 0;

        std::uint8_t g_CtlDispOff[kMaxCtlSites] = {};

        CtlDispSite   g_CtlMut[kMaxCtlSites] = {};
        std::intptr_t g_CloneDelta[4]        = {};
        std::size_t   g_CloneDeltaCount      = 0;

        static const AddrField kSweepFpFields[] = {
            &AS::DevelopArrayGrow_SweepAllow01,
            &AS::DevelopArrayGrow_SweepAllow02,
            &AS::DevelopArrayGrow_SweepAllow03,
            &AS::DevelopArrayGrow_SweepAllow04,
            &AS::DevelopArrayGrow_SweepAllow05,
            &AS::DevelopArrayGrow_SweepAllow06,
            &AS::DevelopArrayGrow_SweepAllow07,
            &AS::DevelopArrayGrow_SweepAllow08,
            &AS::DevelopArrayGrow_SweepAllow09,
            &AS::DevelopArrayGrow_SweepAllow10,
            &AS::DevelopArrayGrow_SweepAllow11,
            &AS::DevelopArrayGrow_SweepAllow12,
            &AS::DevelopArrayGrow_SweepAllow13,
            &AS::DevelopArrayGrow_SweepAllow14,
            &AS::DevelopArrayGrow_SweepAllow15,
            &AS::DevelopArrayGrow_SweepAllow16,
            &AS::DevelopArrayGrow_SweepAllow17,
            &AS::DevelopArrayGrow_SweepAllow18,
            &AS::DevelopArrayGrow_SweepAllow19,
            &AS::DevelopArrayGrow_SweepAllow20,
            &AS::DevelopArrayGrow_SweepAllow21,
            &AS::DevelopArrayGrow_SweepAllow22,
            &AS::DevelopArrayGrow_SweepAllow23,
            &AS::DevelopArrayGrow_SweepAllow24,
            &AS::DevelopArrayGrow_SweepAllow25,
            &AS::DevelopArrayGrow_SweepAllow26,
            &AS::DevelopArrayGrow_SweepAllow27,
            &AS::DevelopArrayGrow_SweepAllow28,
            &AS::DevelopArrayGrow_SweepAllow29,
            &AS::DevelopArrayGrow_SweepAllow30,
            &AS::DevelopArrayGrow_SweepAllow31,
            &AS::DevelopArrayGrow_SweepAllow32,
            &AS::DevelopArrayGrow_SweepAllow33,
            &AS::DevelopArrayGrow_SweepAllow34,
            &AS::DevelopArrayGrow_SweepAllow35,
            &AS::DevelopArrayGrow_SweepAllow36,
            &AS::DevelopArrayGrow_SweepAllow37,
            &AS::DevelopArrayGrow_SweepAllow38,
            &AS::DevelopArrayGrow_SweepAllow39,
            &AS::DevelopArrayGrow_SweepAllow40,
            &AS::DevelopArrayGrow_SweepAllow41,
            &AS::DevelopArrayGrow_SweepAllow42,
        };

        struct BuildAddrs
        {
            std::uintptr_t patch[kPatchCount];
            std::uintptr_t byteFix[kByteFixCount];
            std::uint8_t   byteFixOld[kByteFixCount][4];
            std::uintptr_t blockReset;
            std::uintptr_t setDeveloped;
            std::uintptr_t setUndeveloped;
            std::uintptr_t findByDevId;
            std::uintptr_t findByEquipId;
            std::uintptr_t listDevCount;
            std::uintptr_t listDevFill;
            std::uintptr_t equipIdCount;
            std::uintptr_t equipIdRow;
            std::uintptr_t getBaseId;
            std::uintptr_t getDerivationDevelopIndex;
            std::uintptr_t siblingCount;
            std::uintptr_t isVisible;
            std::uintptr_t quarkAlloc;
            std::uintptr_t blockVtblA;
            std::uintptr_t blockVtblB;
            std::uintptr_t base20VtblA;
            std::uintptr_t base20VtblB;
            std::uintptr_t edcDtor;

            const CtlDispSite*    ctl;
            std::size_t           ctlCount;
            const BoundImmSite*   bnd;
            std::size_t           bndCount;
            const std::uintptr_t* sweepFp;
            std::size_t           sweepFpCount;
        };

        CtlDispSite    g_CtlSites[std::size(kCtlDispSiteFields)]   = {};
        BoundImmSite   g_BoundSites[std::size(kBoundImmSiteFields)] = {};
        std::uintptr_t g_SweepFp[std::size(kSweepFpFields)]        = {};
        BuildAddrs     g_Addrs                                    = {};

        const BuildAddrs* AddrsFromAddressSet()
        {
            static const AddrField kPatchFields[kPatchCount] = {
                &AS::DevelopArrayGrow_Patch_BlockAllocSize,
                &AS::DevelopArrayGrow_Patch_CtorLoopCount,
                &AS::DevelopArrayGrow_Patch_CtorParallelBase,
                &AS::DevelopArrayGrow_Patch_ResetRecordsSize,
                &AS::DevelopArrayGrow_Patch_ResetParallelBase,
                &AS::DevelopArrayGrow_Patch_ResetParallelSize,
                &AS::DevelopArrayGrow_Patch_RegistScanBound,
                &AS::DevelopArrayGrow_Patch_RegistProbeDisp,
                &AS::DevelopArrayGrow_Patch_RegistStoreDisp0,
                &AS::DevelopArrayGrow_Patch_RegistStoreDisp4,
                &AS::DevelopArrayGrow_Patch_RegistStoreDisp8,
                &AS::DevelopArrayGrow_Patch_RegistStoreDispC,
                &AS::DevelopArrayGrow_Patch_EquipidFromIdxBound,
                &AS::DevelopArrayGrow_Patch_IsVisibleBound,
                &AS::DevelopArrayGrow_Patch_GradeGetterBound,
                &AS::DevelopArrayGrow_Patch_PackedGetterBound,
                &AS::DevelopArrayGrow_Patch_CountByTypeScanBound,
                &AS::DevelopArrayGrow_Patch_ProviderCountBound,
                &AS::DevelopArrayGrow_Patch_ProviderFillBound,
                &AS::DevelopArrayGrow_Patch_MenuListGateBound,
                &AS::DevelopArrayGrow_Patch_EquipEquipedBitsAllocWords,
                &AS::DevelopArrayGrow_Patch_EquipEquipedBitsClearSize,
            };
            static const AddrField kByteFixFields[kByteFixCount] = {
                &AS::DevelopArrayGrow_ByteFix_SiblingRetireLoopIndexInit,
                &AS::DevelopArrayGrow_ByteFix_SiblingRetireLoopIndexRead,
                &AS::DevelopArrayGrow_ByteFix_SiblingRetireLoopIndexInc,
                &AS::DevelopArrayGrow_ByteFix_SiblingRetireLoopIndexTest,
                &AS::DevelopArrayGrow_ByteFix_DevelopListCountMissBranch,
                &AS::DevelopArrayGrow_ByteFix_DevelopListApplyMissBranch,
            };
            static const std::uint8_t kByteFixOld[kByteFixCount][4] = { { 0x40, 0x32, 0xF6 }, { 0x40, 0x0F, 0xB6, 0xC6 }, { 0x40, 0xFE, 0xC6 }, { 0x40, 0x0F, 0xB6, 0xC6 }, { 0x73, 0x09 }, { 0x0F, 0x83, 0xFA, 0x00 } };
            const AS& a = gAddr;
            for (std::size_t i = 0; i < kPatchCount; ++i)
                g_Addrs.patch[i] = a.*kPatchFields[i];
            for (std::size_t i = 0; i < kByteFixCount; ++i)
                g_Addrs.byteFix[i] = a.*kByteFixFields[i];
            std::memcpy(g_Addrs.byteFixOld, kByteFixOld, sizeof(kByteFixOld));
            g_Addrs.blockReset                = a.DevelopArrayGrow_BlockReset;
            g_Addrs.setDeveloped              = a.EquipDevelopCtrl_SetEquipDeveloped;
            g_Addrs.setUndeveloped            = a.EquipDevelopCtrl_SetEquipUndeveloped;
            g_Addrs.findByDevId               = a.EquipDevelopCtrl_GetEquipDevelopIndex;
            g_Addrs.findByEquipId             = a.DevelopArrayGrow_FindByEquipId;
            g_Addrs.listDevCount              = a.DevelopArrayGrow_ListDevCount;
            g_Addrs.listDevFill               = a.DevelopArrayGrow_ListDevFill;
            g_Addrs.equipIdCount              = a.DevelopArrayGrow_EquipIdCount;
            g_Addrs.equipIdRow                = a.DevelopArrayGrow_EquipIdRow;
            g_Addrs.getBaseId                 = a.DevelopArrayGrow_GetBaseId;
            g_Addrs.getDerivationDevelopIndex = a.DevelopArrayGrow_GetDerivationDevelopIndex;
            g_Addrs.siblingCount              = a.DevelopArrayGrow_SiblingCount;
            g_Addrs.isVisible                 = a.EquipDevCtrl_IsEquipVisile;
            g_Addrs.quarkAlloc                = a.Dm_Alloc;
            g_Addrs.blockVtblA                = a.DevelopArrayGrow_BlockVtblA;
            g_Addrs.blockVtblB                = a.DevelopArrayGrow_BlockVtblB;
            g_Addrs.base20VtblA               = a.DevelopArrayGrow_Base20VtblA;
            g_Addrs.base20VtblB               = a.DevelopArrayGrow_Base20VtblB;
            g_Addrs.edcDtor                   = a.DevelopArrayGrow_EdcDtor;
            for (std::size_t i = 0; i < std::size(kCtlDispSiteFields); ++i)
            {
                const CtlDispSiteField& f = kCtlDispSiteFields[i];
                g_CtlSites[i] = { a.*f.addr, f.oldDisp, f.dispOff, f.maxOff };
            }
            for (std::size_t i = 0; i < std::size(kBoundImmSiteFields); ++i)
                g_BoundSites[i] = { a.*kBoundImmSiteFields[i].addr, kBoundImmSiteFields[i].immOff };
            std::size_t sweep = 0;
            for (const AddrField f : kSweepFpFields)
                if (a.*f)   // the EN allow-list is shorter, its spare slots are 0
                    g_SweepFp[sweep++] = a.*f;
            g_Addrs.ctl          = g_CtlSites;
            g_Addrs.ctlCount     = std::size(g_CtlSites);
            g_Addrs.bnd          = g_BoundSites;
            g_Addrs.bndCount     = std::size(g_BoundSites);
            g_Addrs.sweepFp      = g_SweepFp;
            g_Addrs.sweepFpCount = sweep;
            return &g_Addrs;
        }

        const BuildAddrs* g_A = nullptr;

        bool SelectBuildTables()
        {
            g_A = AddrsFromAddressSet();
            for (std::size_t i = 0; i < kPatchCount; ++i)
            {
                kPatches[i]      = kPatchTemplate[i];
                kPatches[i].addr = g_A->patch[i];
            }
            for (std::size_t i = 0; i < kByteFixCount; ++i)
            {
                kByteFixes[i]      = kByteFixTemplate[i];
                kByteFixes[i].addr = g_A->byteFix[i];
                std::memcpy(kByteFixes[i].oldBytes, g_A->byteFixOld[i], 4);
            }
            for (std::size_t i = 0; i < g_A->ctlCount && i < kMaxCtlSites; ++i)
                g_CtlMut[i] = g_A->ctl[i];
            kCtlDispSites   = g_CtlMut;
            kCtlSiteCount   = g_A->ctlCount;
            kBoundImmSites  = g_A->bnd;
            kBoundSiteCount = g_A->bndCount;
            return true;
        }

        bool g_Active = false;

        enum class PrePatchState : int
        {
            NotAttempted = 0,
            Applied,
            WrongBuild,
            DisabledByMarker,
            BlockExists,
            VerifyFailed,
            ApplyFailed,
            RacedMidPatch,
            RetryPending,
        };
        PrePatchState g_PrePatch       = PrePatchState::NotAttempted;
        int           g_PrePatchDetail = -1;
        std::uintptr_t g_GrownBlock    = 0;
        std::uintptr_t* g_MigratedSlot = nullptr;
        std::uintptr_t g_MigratedOldBase20 = 0;
        char          g_PreVerifyDetail[192] = {};

        bool GrowDisabledByMarker()
        {
            wchar_t path[MAX_PATH];
            const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return false;
            wchar_t* slash = std::wcsrchr(path, L'\\');
            if (!slash)
                return false;
            slash[1] = L'\0';
            if (std::wcslen(path) + 40 >= MAX_PATH)
                return false;
            wcscat_s(path, MAX_PATH, L"V_FrameWork_no_develop_grow.txt");
            return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
        }

        void NoteVerifyFail(const char* fmt, ...)
        {
            va_list args;
            va_start(args, fmt);
            vsnprintf(g_PreVerifyDetail, sizeof(g_PreVerifyDetail), fmt, args);
            va_end(args);
        }

        std::atomic<std::uintptr_t> g_ArmedBase20{ 0 };

        constexpr std::size_t kFlagsShadowSize = 0x10000;
        static_assert(kFlagsShadowSize >= kNewRows, "shadow must cover all rows");
        std::uint8_t  g_FlagsShadow[kFlagsShadowSize] = {};
        std::uint32_t g_EquipEquipedBits[kNewEquipEquipedWords] = {};
        std::uint8_t* g_SvarsFlags = nullptr;
        void*         g_BlockSeen  = nullptr;

        std::uint8_t  g_SyncMirror[kOldRows] = {};
        std::uint32_t g_LastSyncTick   = 0;

        using BlockReset_t     = void (__fastcall*)(std::uintptr_t block);
        using SetDeveloped_t   = void (__fastcall*)(std::uintptr_t base20,
                                                    std::uint16_t idx);
        using SetUndeveloped_t = void (__fastcall*)(std::uintptr_t base20,
                                                    std::uint16_t idx, char notify);
        using FindFlow_t       = std::uint16_t (__fastcall*)(std::uintptr_t base20,
                                                             std::uint16_t key);
        using ListDevCount_t   = std::uint16_t (__fastcall*)(std::uintptr_t base20);
        using ListDevFill_t    = void (__fastcall*)(std::uintptr_t base20,
                                                    std::uint16_t maxCount,
                                                    std::uint16_t* outBuf);
        using RowListGate_t    = char (__fastcall*)(std::uintptr_t block,
                                                    std::uint16_t idx);
        using SetItemDevelopedLv_t =
            void (__fastcall*)(std::uintptr_t block, std::uint16_t idx);
        using EquipIdQuery_t   = long long (__fastcall*)(std::uintptr_t base20,
                                                         std::uint16_t equipId);
        using RowQueryTail_t   = long long (__fastcall*)(std::uintptr_t block,
                                                         std::uint32_t equipId,
                                                         std::uint32_t foundIdx);
        using ExtCount_t       = int (__fastcall*)(std::uintptr_t self);
        using GetQuark_t       = std::uint8_t* (__fastcall*)();
        using GetBaseId_t      = std::uint16_t (__fastcall*)(std::uintptr_t base20,
                                                             std::uint16_t row);
        using GetDerivationDevelopIndex_t =
            std::uint16_t (__fastcall*)(std::uintptr_t block, std::uint16_t row,
                                        std::uint8_t nth);

        using EdcDtor_t = void* (__fastcall*)(void* self, unsigned int flags);

        BlockReset_t     g_OrigBlockReset     = nullptr;
        EdcDtor_t        g_OrigEdcDtor        = nullptr;
        SetDeveloped_t   g_OrigSetDeveloped   = nullptr;
        SetUndeveloped_t g_OrigSetUndeveloped = nullptr;
        FindFlow_t       g_OrigFindByDevId    = nullptr;
        FindFlow_t       g_OrigFindByEquipId  = nullptr;
        ListDevCount_t   g_OrigListDevCount   = nullptr;
        ListDevFill_t    g_OrigListDevFill    = nullptr;
        EquipIdQuery_t   g_OrigEquipIdCount   = nullptr;
        EquipIdQuery_t   g_OrigEquipIdRow     = nullptr;
        GetBaseId_t      g_OrigGetBaseId      = nullptr;
        GetDerivationDevelopIndex_t g_OrigGetDerivationDevelopIndex = nullptr;

        void*            g_HookBlockReset     = nullptr;
        void*            g_HookSetDeveloped   = nullptr;
        void*            g_HookSetUndeveloped = nullptr;
        void*            g_HookFindByDevId    = nullptr;
        void*            g_HookFindByEquipId  = nullptr;
        void*            g_HookListDevCount   = nullptr;
        void*            g_HookListDevFill    = nullptr;
        void*            g_HookEquipIdCount   = nullptr;
        void*            g_HookEquipIdRow     = nullptr;
        void*            g_HookGetBaseId      = nullptr;
        void*            g_HookGetDerivationDevelopIndex = nullptr;
        void*            g_HookEdcDtor        = nullptr;
        bool             g_DtorGuardInstalled = false;

        void* __fastcall hkEdcDtorGuard(void* self, unsigned int flags)
        {
            if (g_GrownBlock
                && reinterpret_cast<std::uintptr_t>(self) != g_GrownBlock)
                return self;
            __try
            {
                return g_OrigEdcDtor ? g_OrigEdcDtor(self, flags) : self;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogWarn("DevelopArrayGrow: EquipDevelopController dtor faulted on %p, "
                        "teardown stopped part-way", self);
                return self;
            }
        }

#define kAddr_BlockReset     (g_A->blockReset)
#define kAddr_SetDeveloped   (g_A->setDeveloped)
#define kAddr_SetUndeveloped (g_A->setUndeveloped)
#define kAddr_FindByDevId    (g_A->findByDevId)
#define kAddr_FindByEquipId  (g_A->findByEquipId)
#define kAddr_EdcDtor        (g_A->edcDtor)
#define kAddr_ListDevCount   (g_A->listDevCount)
#define kAddr_ListDevFill    (g_A->listDevFill)
#define kAddr_EquipIdCount   (g_A->equipIdCount)
#define kAddr_EquipIdRow     (g_A->equipIdRow)
#define kAddr_GetBaseId      (g_A->getBaseId)
#define kAddr_GetDerivationDevelopIndex (g_A->getDerivationDevelopIndex)
#define kAddr_SiblingCount   (g_A->siblingCount)
#define kAddr_IsVisible      (g_A->isVisible)

#define kAddr_QuarkBlockHeapAlloc (g_A->quarkAlloc)
#define kAddr_BlockVtblA          (g_A->blockVtblA)
#define kAddr_BlockVtblB          (g_A->blockVtblB)
#define kAddr_Base20VtblA         (g_A->base20VtblA)
#define kAddr_Base20VtblB         (g_A->base20VtblB)

        constexpr std::size_t kBlock_FlagsPtrOff        = kNewCtlOff;
        constexpr std::size_t kBase20_FlagsPtrOff       = kNewCtlOff - 0x20;
        constexpr std::size_t kBase20_RecFieldOff       = 0x8;
        constexpr std::size_t kBase20_RecDevelopTypeOff = 0x3E;

        constexpr std::uint8_t  kReqAnnouncedBit = 0x4;
        constexpr std::uint32_t kTrueVanillaRows = 922; // 920 stock rows + Infinite Heaven's two filler rows

        std::atomic<std::uint32_t> g_FirstCustomRow{ kTrueVanillaRows };
        std::atomic<bool>          g_LayoutSettled{ false };
        std::atomic<std::uint32_t> g_ClearLoadedRowsBegin{ 0 };
        std::atomic<std::uint32_t> g_ClearLoadedRowsEnd{ 0 };
        std::atomic<bool>          g_LayoutPersistPending{ false };
        std::atomic<bool>          g_RejectLoadedRows{ true };
        std::atomic<std::int32_t>  g_PendingFirstCustomRow{ 0 };
        std::atomic<std::int32_t>  g_PendingNativeRowsHash{ 0 };

        std::uint32_t FirstCustomRow()
        {
            return g_FirstCustomRow.load(std::memory_order_acquire);
        }

        std::atomic<std::uint32_t> g_GunsmithClaimMask{ 0 };

        struct AnnouncedRowWork
        {
            std::int32_t developId;
            std::int32_t flowIndex;
            bool         reqAnnounced;
        };

        void CollectAnnouncedRowWork(std::vector<AnnouncedRowWork>& out)
        {
            V_FrameWorkState::ForEachManagedDevelopRow(
                [&out](std::int32_t developId, std::int32_t flowIndex,
                       bool reqAnnounced)
                {
                    out.push_back({ developId, flowIndex, reqAnnounced });
                });
        }

        bool ServeFromIndex(std::uintptr_t base20, std::uint16_t key,
                            bool equipField, std::uint16_t& out);

        std::uint32_t ResolveAnnouncedRow(std::int32_t developId)
        {
            if (developId <= 0 || developId > 0xFFFF)
                return 0;
            const std::uintptr_t base20 =
                g_ArmedBase20.load(std::memory_order_relaxed);
            if (base20 == 0)
                return 0;
            std::uint16_t row = 0;
            if (ServeFromIndex(base20, static_cast<std::uint16_t>(developId),
                               false, row))
                return row;
            return 0;
        }

        bool RowHoldsDevelopId(std::uint32_t row, std::int32_t developId)
        {
            const std::uintptr_t base20 =
                g_ArmedBase20.load(std::memory_order_relaxed);
            if (base20 == 0 || row >= kNewRows)
                return false;
            std::uint16_t devId = 0;
            __try
            {
                std::memcpy(&devId,
                    reinterpret_cast<const void*>(
                        base20 + kBase20_RecFieldOff
                               + static_cast<std::size_t>(row) * kRecStride),
                    sizeof(devId));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
            return devId == static_cast<std::uint16_t>(developId);
        }

        std::uint32_t AnnouncedRowOf(std::int32_t developId,
                                     std::int32_t persistedFlow)
        {
            const std::uint32_t first = FirstCustomRow();
            const std::uint32_t persisted =
                static_cast<std::uint32_t>(persistedFlow);
            const bool persistedUsable =
                persistedFlow > 0 && persisted >= first
                && persisted < kNewRows && !IsReservedFlowRow(persisted);

            if (persistedUsable && RowHoldsDevelopId(persisted, developId))
                return persisted;

            const std::uint32_t live = ResolveAnnouncedRow(developId);
            if (live >= first && live < kNewRows
                && !IsReservedFlowRow(live)
                && RowHoldsDevelopId(live, developId))
                return live;

            return persistedUsable ? persisted : 0;
        }

        void RestoreGrownAnnouncedBits()
        {
            std::vector<AnnouncedRowWork> work;
            CollectAnnouncedRowWork(work);

            std::vector<std::int32_t> nowAnnounced;
            for (const AnnouncedRowWork& w : work)
            {
                const std::uint32_t row =
                    AnnouncedRowOf(w.developId, w.flowIndex);
                if (row == 0)
                    continue;

                const bool initial = EquipDevelop_IsDevelopInitiallyAvailable(
                    static_cast<std::uint32_t>(w.developId));
                if (!w.reqAnnounced && !initial)
                {
                    g_FlagsShadow[row] &= static_cast<std::uint8_t>(
                        ~kReqAnnouncedBit);
                    continue;
                }
                g_FlagsShadow[row] |= kReqAnnouncedBit;
                if (!w.reqAnnounced)
                    nowAnnounced.push_back(w.developId);
            }

            for (std::int32_t developId : nowAnnounced)
                V_FrameWorkState::SetDevReqAnnouncedByDevelopId(developId, true);
        }

        void CaptureGrownAnnouncedBits()
        {
            std::vector<AnnouncedRowWork> work;
            CollectAnnouncedRowWork(work);

            std::vector<std::int32_t> justAnnounced;
            for (const AnnouncedRowWork& w : work)
            {
                if (w.reqAnnounced)
                    continue;
                const std::uint32_t row =
                    AnnouncedRowOf(w.developId, w.flowIndex);
                if (row == 0)
                    continue;
                if (!RowHoldsDevelopId(row, w.developId))
                    continue;
                if (g_FlagsShadow[row] & kReqAnnouncedBit)
                    justAnnounced.push_back(w.developId);
            }

            for (std::int32_t developId : justAnnounced)
                V_FrameWorkState::SetDevReqAnnouncedByDevelopId(developId, true);
        }

        void MirrorFlagByte(std::uint32_t idx)
        {
            InvalidateDevelopVisibilityCache();
            if (g_SvarsFlags && idx < kOldRows)
            {
                g_SvarsFlags[idx] = g_FlagsShadow[idx];
                g_SyncMirror[idx] = g_FlagsShadow[idx];
            }
        }

        std::mutex                g_SilencedMutex;
        std::vector<std::int32_t> g_SilencedDevelopIds;

        void MarkSilencedRowsAnnounced()
        {
            std::lock_guard<std::mutex> lock(g_SilencedMutex);
            for (std::int32_t developId : g_SilencedDevelopIds)
            {
                const std::uint32_t row = AnnouncedRowOf(developId, -1);
                if (row == 0)
                    continue;
                g_FlagsShadow[row] |= kReqAnnouncedBit;
                MirrorFlagByte(row);
            }
        }

        void MarkReservedRowsAnnounced()
        {
            const std::uint32_t claimed =
                g_GunsmithClaimMask.load(std::memory_order_relaxed);
            for (std::uint32_t row = kGunsmithFlowFirst;
                 row <= kGunsmithFlowLast; ++row)
            {
                if (((claimed >> (row - kGunsmithFlowFirst)) & 1u) == 0)
                    continue;
                g_FlagsShadow[row] |= kReqAnnouncedBit;
                MirrorFlagByte(row);
            }
        }

        void SyncFlagsWithSvars(bool force)
        {
            if (!g_Active || !g_SvarsFlags)
                return;
            if (!force)
            {
                const std::uint32_t now = GetTickCount();
                if (now == g_LastSyncTick)
                    return;
                g_LastSyncTick = now;
            }
            EquipDevelop_RefreshDevelopTimerSnapshot();
            const std::uint32_t shadowOwnedFrom =
                g_LayoutSettled.load(std::memory_order_acquire) ? FirstCustomRow() : kOldRows;
            const std::uint32_t clearLoadedBegin =
                g_ClearLoadedRowsBegin.load(std::memory_order_relaxed);
            const std::uint32_t clearLoadedEnd =
                g_RejectLoadedRows.load(std::memory_order_relaxed)
                    ? g_ClearLoadedRowsEnd.load(std::memory_order_relaxed) : 0;
            std::uint32_t pulled = 0;
            __try
            {
                for (std::uint32_t i = 0; i < kOldRows; ++i)
                {
                    const std::uint8_t s = g_SvarsFlags[i];
                    const std::uint8_t h = g_FlagsShadow[i];
                    if (s == h)
                    {
                        g_SyncMirror[i] = s;
                        continue;
                    }
                    if (i >= shadowOwnedFrom)
                    {
                        g_SvarsFlags[i] = h;
                        g_SyncMirror[i] = h;
                        continue;
                    }
                    if (s != g_SyncMirror[i] && i >= clearLoadedBegin && i < clearLoadedEnd)
                    {
                        g_SvarsFlags[i] = h; // the save still holds the flags of the custom row that sat here before the native rows grew
                        g_SyncMirror[i] = h;
                        continue;
                    }
                    if (s != g_SyncMirror[i])
                    {
                        g_FlagsShadow[i] = s;
                        g_SyncMirror[i]  = s;
                        ++pulled;
                    }
                    else
                    {
                        g_SvarsFlags[i] = h;
                        g_SyncMirror[i] = h;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogWarn("DevelopArrayGrow: fault reading save develop flags at %p, "
                            "sync skipped", static_cast<void*>(g_SvarsFlags));
                }
                return;
            }
            MarkReservedRowsAnnounced();
            MarkSilencedRowsAnnounced();
            CaptureGrownAnnouncedBits();
            RestoreGrownAnnouncedBits();
            if (pulled && g_LayoutPersistPending.exchange(false, std::memory_order_acq_rel))
            {
                V_FrameWorkState::SetPersistedConstant("DevelopLayout", "firstCustomRow",
                    g_PendingFirstCustomRow.load(std::memory_order_relaxed));
                V_FrameWorkState::SetPersistedConstant("DevelopLayout", "nativeRowsHash",
                    g_PendingNativeRowsHash.load(std::memory_order_relaxed));
            }
            if (pulled)
            {
                InvalidateDevelopVisibilityCache();
                EquipDevelop_RequestDevelopRestore();
            }
        }

        bool         g_OtherThreadsSuspended = false;
        int          g_DeferredProtectFails  = 0;
        const char*  g_DeferredProtectWhat   = nullptr;
        void*        g_DeferredProtectAddr   = nullptr;
        DWORD        g_DeferredProtectErr    = 0;

        void ReportProtectFailure(const char* what, void* addr, DWORD err)
        {
            if (!g_OtherThreadsSuspended)
            {
                LogWarn("DevelopArrayGrow: site %s @%p: VirtualProtect failed (%lu)",
                        what, addr, err);
                return;
            }
            // a suspended thread may hold the log lock, so the warning waits for ResumeOtherThreads
            if (g_DeferredProtectFails++ == 0)
            {
                g_DeferredProtectWhat = what;
                g_DeferredProtectAddr = addr;
                g_DeferredProtectErr  = err;
            }
        }

        bool WriteImmAt(const ImmPatch& p, std::uint32_t value)
        {
            std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(
                const_cast<void*>(ResolveGameAddress(p.addr)));
            if (!addr)
                return false;
            DWORD oldProt = 0;
            if (!VirtualProtect(addr, p.opLen + 4, PAGE_EXECUTE_READWRITE,
                                &oldProt))
            {
                ReportProtectFailure(p.what, static_cast<void*>(addr), GetLastError());
                return false;
            }
            std::memcpy(addr + p.opLen, &value, 4);
            DWORD tmp = 0;
            VirtualProtect(addr, p.opLen + 4, oldProt, &tmp);
            FlushInstructionCache(GetCurrentProcess(), addr, p.opLen + 4);
            return true;
        }

        bool IsWorkBufFreeLoad(std::uintptr_t dispVa, std::uint32_t oldDisp)
        {
            if (oldDisp != kWorkBufDisp)
                return false;
            const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(dispVa - 3));
            if (!p)
                return false;
            __try
            {
                return p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x89;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        std::uint32_t CtlNewDisp(std::uintptr_t dispVa, std::uint32_t oldDisp)
        {
            return IsWorkBufFreeLoad(dispVa, oldDisp)
                       ? kNullSinkOff
                       : oldDisp + kCtlDelta;
        }

        bool WriteU32At(std::uintptr_t siteAddr, std::uint32_t byteOff,
                        std::uint32_t value)
        {
            std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(
                const_cast<void*>(ResolveGameAddress(siteAddr)));
            if (!addr)
                return false;
            addr += byteOff;
            DWORD oldProt = 0;
            if (!VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldProt))
            {
                ReportProtectFailure("displacement", static_cast<void*>(addr), GetLastError());
                return false;
            }
            std::memcpy(addr, &value, 4);
            DWORD tmp = 0;
            VirtualProtect(addr, 4, oldProt, &tmp);
            FlushInstructionCache(GetCurrentProcess(), addr, 4);
            return true;
        }

        bool VerifyCtlSiteAt(std::size_t i, std::uintptr_t va, bool quiet)
        {
            const CtlDispSite& s = kCtlDispSites[i];
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(va));
            if (!addr)
                return false;
            __try
            {
                if (s.dispOff != 0xFF)
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, addr + s.dispOff, 4);
                    if (v != s.oldDisp)
                    {
                        if (!quiet)
                            NoteVerifyFail("ctl site %p: disp at +%u is 0x%X, "
                                           "expected 0x%X",
                                           addr, s.dispOff, v, s.oldDisp);
                        return false;
                    }
                    g_CtlDispOff[i] = s.dispOff;
                    return true;
                }
                int found = -1;
                for (int off = 1; off <= s.maxOff; ++off)
                {
                    std::uint32_t v = 0;
                    std::memcpy(&v, addr + off, 4);
                    if (v == s.oldDisp)
                    {
                        if (found >= 0)
                        {
                            if (!quiet)
                                NoteVerifyFail("ctl site %p disp 0x%X: "
                                               "ambiguous (offsets %d and %d)",
                                               addr, s.oldDisp, found, off);
                            return false;
                        }
                        found = off;
                    }
                }
                if (found < 0)
                {
                    if (!quiet)
                        NoteVerifyFail("ctl site %p: disp 0x%X not found in "
                                       "the instruction window",
                                       addr, s.oldDisp);
                    return false;
                }
                g_CtlDispOff[i] = static_cast<std::uint8_t>(found);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                if (!quiet)
                    NoteVerifyFail("ctl site %p: unreadable", addr);
                return false;
            }
        }

        bool VerifyCtlSite(std::size_t i)
        {
            return VerifyCtlSiteAt(i, kCtlDispSites[i].addr, false);
        }

        bool RelocateCloneCtlSites(const std::size_t* failed,
                                   std::size_t nFailed)
        {
            std::size_t order[16];
            for (std::size_t i = 0; i < nFailed; ++i)
                order[i] = failed[i];
            for (std::size_t i = 1; i < nFailed; ++i)
                for (std::size_t j = i;
                     j > 0 && kCtlDispSites[order[j - 1]].addr
                              > kCtlDispSites[order[j]].addr; --j)
                {
                    const std::size_t t = order[j];
                    order[j]            = order[j - 1];
                    order[j - 1]        = t;
                }

            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(
                GetModuleHandleW(nullptr));
            if (!base)
                return false;

            std::uintptr_t placed[16] = {};
            __try
            {
                const IMAGE_DOS_HEADER* dos =
                    reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                const IMAGE_NT_HEADERS* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS*>(
                        base + dos->e_lfanew);
                const std::uint8_t* end =
                    base + nt->OptionalHeader.SizeOfImage;

                std::size_t g0 = 0;
                while (g0 < nFailed)
                {
                    std::size_t g1 = g0 + 1;
                    while (g1 < nFailed
                           && kCtlDispSites[order[g1]].addr
                              - kCtlDispSites[order[g1 - 1]].addr <= 0x1000)
                        ++g1;

                    const CtlDispSite& f0 = kCtlDispSites[order[g0]];
                    std::uintptr_t best[16] = {};
                    std::size_t    nComplete = 0;

                    MEMORY_BASIC_INFORMATION mbi;
                    const std::uint8_t* p = base;
                    while (p < end
                           && VirtualQuery(p, &mbi, sizeof(mbi))
                              == sizeof(mbi))
                    {
                        const std::uint8_t* rBeg =
                            static_cast<const std::uint8_t*>(
                                mbi.BaseAddress);
                        const std::uint8_t* rEnd = rBeg + mbi.RegionSize;
                        if (rEnd > end)
                            rEnd = end;
                        const bool scannable =
                            mbi.State == MEM_COMMIT
                            && (mbi.Protect
                                & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                                   | PAGE_EXECUTE_READWRITE
                                   | PAGE_EXECUTE_WRITECOPY)) != 0;
                        if (scannable && rEnd - rBeg >= 8)
                        {
                            for (const std::uint8_t* q = rBeg;
                                 q + 4 <= rEnd; ++q)
                            {
                                if (q[2] != 0x01 || q[3] != 0x00)
                                    continue;
                                std::uint32_t v = 0;
                                std::memcpy(&v, q, 4);
                                if (v != f0.oldDisp)
                                    continue;
                                const std::uintptr_t qva =
                                    0x140000000ull
                                    + static_cast<std::uintptr_t>(q - base);
                                const int offLo =
                                    f0.dispOff != 0xFF ? f0.dispOff : 1;
                                const int offHi =
                                    f0.dispOff != 0xFF ? f0.dispOff
                                                       : f0.maxOff;
                                for (int off = offLo; off <= offHi; ++off)
                                {
                                    const std::uintptr_t anchor = qva - off;
                                    if (anchor == f0.addr)
                                        continue;
                                    if (!VerifyCtlSiteAt(order[g0], anchor,
                                                         true))
                                        continue;
                                    std::uintptr_t cur[16];
                                    cur[0] = anchor;
                                    bool ok = true;
                                    for (std::size_t gi = g0 + 1;
                                         gi < g1 && ok; ++gi)
                                    {
                                        const std::uintptr_t expect =
                                            anchor
                                            + (kCtlDispSites[order[gi]].addr
                                               - f0.addr);
                                        std::uintptr_t found = 0;
                                        for (std::uintptr_t c =
                                                 expect - 0x80;
                                             c <= expect + 0x80; ++c)
                                        {
                                            if (c <= cur[gi - g0 - 1])
                                                continue;
                                            if (VerifyCtlSiteAt(order[gi],
                                                                c, true))
                                            {
                                                found = c;
                                                break;
                                            }
                                        }
                                        if (!found)
                                            ok = false;
                                        else
                                            cur[gi - g0] = found;
                                    }
                                    if (!ok)
                                        continue;
                                    if (nComplete == 0)
                                        for (std::size_t k = 0;
                                             k < g1 - g0; ++k)
                                            best[k] = cur[k];
                                    ++nComplete;
                                }
                            }
                        }
                        p = rEnd < end ? rEnd : end;
                    }

                    if (nComplete != 1)
                    {
                        NoteVerifyFail(
                            "clone relocation: %zu candidate placements for "
                            "a %zu-site moved clone block (need exactly 1) "
                            "- the clone table must be re-ported by hand",
                            nComplete, g1 - g0);
                        return false;
                    }
                    for (std::size_t k = 0; k < g1 - g0; ++k)
                        placed[g0 + k] = best[k];
                    g0 = g1;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                NoteVerifyFail("clone relocation: exception while scanning "
                               "the game image");
                return false;
            }

            for (std::size_t i = 0; i < nFailed; ++i)
            {
                const std::intptr_t d =
                    static_cast<std::intptr_t>(placed[i])
                    - static_cast<std::intptr_t>(
                        kCtlDispSites[order[i]].addr);
                g_CtlMut[order[i]].addr = placed[i];
                if (!VerifyCtlSiteAt(order[i],
                                     kCtlDispSites[order[i]].addr, false))
                    return false;
                bool dup = false;
                for (std::size_t k = 0; k < g_CloneDeltaCount; ++k)
                    if (g_CloneDelta[k] == d)
                        dup = true;
                if (!dup && g_CloneDeltaCount < 4)
                    g_CloneDelta[g_CloneDeltaCount++] = d;
            }
            return true;
        }

        constexpr std::uint32_t kSweepValLo = 0x1A008;
        constexpr std::uint32_t kSweepValHi = 0x1FFD0;
        constexpr int           kSweepReportCap = 64;

        struct SweepFind
        {
            std::uintptr_t va;
            std::uint32_t  disp;
        };
        SweepFind g_SweepUnknown[kSweepReportCap];
        int       g_SweepUnknownCount = 0;

        bool           g_SweepValue[kSweepValHi - kSweepValLo] = {};
        std::uintptr_t g_SweepAllow[600];
        std::size_t    g_SweepAllowCount = 0;

        void SweepAllowAdd(std::uintptr_t va)
        {
            if (g_SweepAllowCount < 600)
                g_SweepAllow[g_SweepAllowCount++] = va;
        }

        bool SweepAllowed(std::uintptr_t va)
        {
            for (std::size_t i = 0; i < g_SweepAllowCount; ++i)
                if (g_SweepAllow[i] == va)
                    return true;
            for (std::size_t d = 0; d < g_CloneDeltaCount; ++d)
                for (std::size_t i = 0; i < g_SweepAllowCount; ++i)
                    if (g_SweepAllow[i] + g_CloneDelta[d] == va)
                        return true;
            return false;
        }

        bool SweepImageForUnknownSites(bool quiet = false)
        {
            g_SweepAllowCount = 0;
            g_SweepUnknownCount = 0;
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
            {
                g_SweepValue[kCtlDispSites[i].oldDisp - kSweepValLo] = true;
                SweepAllowAdd(kCtlDispSites[i].addr + g_CtlDispOff[i]);
            }
            const std::uint32_t parVals[] =
                { 0x1A028, 0x1A008, 0x1A00C, 0x1A010, 0x1A014 };
            for (std::size_t i = 0; i < 5; ++i)
                g_SweepValue[parVals[i] - kSweepValLo] = true;
            for (std::size_t i = 0; i < kPatchCount; ++i)
                if (kPatches[i].addr
                    && kPatches[i].oldImm >= kSweepValLo
                    && kPatches[i].oldImm < kSweepValHi)
                    SweepAllowAdd(kPatches[i].addr + kPatches[i].opLen);
            for (std::size_t i = 0; i < g_A->sweepFpCount; ++i)
                SweepAllowAdd(g_A->sweepFp[i]);

            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(
                GetModuleHandleW(nullptr));
            if (!base)
            {
                NoteVerifyFail("image sweep: no game module base");
                return false;
            }

            int unknown = 0;
            __try
            {
                const IMAGE_DOS_HEADER* dos =
                    reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                const IMAGE_NT_HEADERS* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS*>(
                        base + dos->e_lfanew);
                const std::uint8_t* end =
                    base + nt->OptionalHeader.SizeOfImage;

                MEMORY_BASIC_INFORMATION mbi;
                const std::uint8_t* p = base;
                while (p < end
                       && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi))
                {
                    const std::uint8_t* rBeg =
                        static_cast<const std::uint8_t*>(mbi.BaseAddress);
                    const std::uint8_t* rEnd = rBeg + mbi.RegionSize;
                    if (rEnd > end)
                        rEnd = end;
                    const bool readable =
                        mbi.State == MEM_COMMIT
                        && mbi.Protect != 0
                        && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
                    const bool executable =
                        (mbi.Protect
                         & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                            | PAGE_EXECUTE_READWRITE
                            | PAGE_EXECUTE_WRITECOPY)) != 0;
                    if (readable && rEnd - rBeg >= 8)
                    {
                        for (const std::uint8_t* q = rBeg + 2;
                             q + 4 <= rEnd; ++q)
                        {
                            if (q[2] != 0x01 || q[3] != 0x00)
                                continue;
                            std::uint32_t v = 0;
                            std::memcpy(&v, q, 4);
                            if (v < kSweepValLo || v >= kSweepValHi
                                || !g_SweepValue[v - kSweepValLo])
                                continue;
                            const std::uint8_t b1 = q[-1];
                            const std::uint8_t b2 = q[-2];
                            const bool direct = (b1 & 0xC0) == 0x80
                                                && (b1 & 7) != 4;
                            const bool sib = (b2 & 0xC0) == 0x80
                                             && (b2 & 7) == 4;
                            if (!direct && !sib)
                                continue;
                            const std::uintptr_t va =
                                0x140000000ull
                                + static_cast<std::uintptr_t>(q - base);
                            if (SweepAllowed(va))
                                continue;
                            if (!executable)
                                continue;
                            bool detourTail = false;
                            for (int k = 1; k <= 4 && !detourTail; ++k)
                            {
                                if (q - k < rBeg)
                                    break;
                                if (q[-k] != 0xE9 && q[-k] != 0xE8)
                                    continue;
                                std::int32_t rel = 0;
                                std::memcpy(&rel, q - k + 1, 4);
                                const std::uintptr_t next =
                                    0x140000000ull
                                    + static_cast<std::uintptr_t>(
                                        (q - k + 5) - base);
                                const std::uintptr_t tgt = next
                                    + static_cast<std::uintptr_t>(
                                        static_cast<std::intptr_t>(rel));
                                if (tgt < 0x140000000ull
                                    || tgt >= 0x140000000ull
                                        + nt->OptionalHeader.SizeOfImage)
                                    detourTail = true;
                            }
                            if (detourTail)
                                continue;
                            if (unknown == 0 && !quiet)
                                NoteVerifyFail(
                                    "image sweep: unknown develop-field "
                                    "reference at %p (disp 0x%X) - an "
                                    "unpatched code copy exists",
                                    reinterpret_cast<const void*>(va), v);
                            if (unknown < kSweepReportCap)
                            {
                                g_SweepUnknown[unknown] = SweepFind{ va, v };
                                g_SweepUnknownCount = unknown + 1;
                            }
                            ++unknown;
                        }
                    }
                    p = rEnd < end ? rEnd : end;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                NoteVerifyFail("image sweep: exception while scanning the "
                               "game image");
                return false;
            }
            return unknown == 0;
        }

        bool VerifyByteFix(const ByteFix& f)
        {
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(f.addr));
            if (!addr)
                return false;
            __try
            {
                if (std::memcmp(addr, f.oldBytes, f.len) == 0
                    || std::memcmp(addr, f.newBytes, f.len) == 0)
                    return true;
                NoteVerifyFail("byte fix %s @%p: bytes match neither the "
                               "original nor the fixed form", f.what, addr);
                return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                NoteVerifyFail("byte fix %s: exception reading the site",
                               f.what);
                return false;
            }
        }

        bool WriteByteFix(const ByteFix& f, const std::uint8_t* bytes)
        {
            std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(
                const_cast<void*>(ResolveGameAddress(f.addr)));
            if (!addr)
                return false;
            DWORD oldProt = 0;
            if (!VirtualProtect(addr, f.len, PAGE_EXECUTE_READWRITE, &oldProt))
            {
                ReportProtectFailure(f.what, static_cast<void*>(addr), GetLastError());
                return false;
            }
            std::memcpy(addr, bytes, f.len);
            DWORD tmp = 0;
            VirtualProtect(addr, f.len, oldProt, &tmp);
            FlushInstructionCache(GetCurrentProcess(), addr, f.len);
            return true;
        }

        std::uint16_t __fastcall hkSiblingCount(void* self, std::uint16_t row,
                                                char flaggedOnly)
        {
            std::uint16_t n =
                g_OrigSiblingCount ? g_OrigSiblingCount(self, row, flaggedOnly)
                                   : 0;
            if (n > kSiblingListCap)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogWarn("DevelopArrayGrow: develop row %u has %u same-group rows, "
                            "clamped to 1024", static_cast<unsigned>(row),
                            static_cast<unsigned>(n));
                }
                n = kSiblingListCap;
            }
            return n;
        }

        bool CustomBluePrintShowsRow(const void* self, std::uint16_t row)
        {
            if (row >= kNewRows)
                return false;
            const std::uint16_t developId = g_CustomBluePrintDevelopIds[row];
            if (developId == 0 || MissionCodeGuard::ShouldBypassHooks())
                return false;
            __try
            {
                const std::uint8_t* rec = static_cast<const std::uint8_t*>(self)
                    + static_cast<std::size_t>(row) * kRecStride;
                if (*reinterpret_cast<const std::uint16_t*>(rec + kRecDevelopIdOff) != developId)
                    return false;
                const std::uint8_t displayInfo = rec[kRecDisplayInfoOff] & kDisplayInfoMask;
                return displayInfo == kDisplayInfoSkillOrBluePrint
                    || displayInfo == kDisplayInfoBluePrintOnly;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool CustomWeaponRowHasNoGunBasic(const void* self, std::uint16_t row)
        {
            if (row < FirstCustomRow() || row >= kNewRows)
                return false;
            std::uint16_t equipId = 0;
            __try
            {
                const std::uint8_t* rec = static_cast<const std::uint8_t*>(self)
                    + static_cast<std::size_t>(row) * kRecStride;
                if (*reinterpret_cast<const std::uint16_t*>(rec + kRecDevelopIdOff) == 0)
                    return false;
                equipId = equip::DevelopRecordEquipId(rec);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
            return GunBasic_WeaponHasNoRow(equipId);
        }

        bool __fastcall hkIsVisible(void* self, std::uint16_t row)
        {
            if (outfit::IsDevelopHidden(row))
                return false;
            if (CustomWeaponRowHasNoGunBasic(self, row))
                return false;
            if (CustomBluePrintShowsRow(self, row))
                return true;
            if (row >= kNewRows || g_VisThread != GetCurrentThreadId())
                return g_OrigIsVisible ? g_OrigIsVisible(self, row) : false;

            const std::uint64_t now = GetTickCount64();
            if (g_VisValid && !g_VisArmed && now - g_VisStamp > kVisTtlMs)
            {
                g_VisValid = false;
                g_VisSelf  = nullptr;
            }
            if (!g_VisValid)
            {
                std::memset(g_VisCache, 0, sizeof(g_VisCache));
                g_VisSelf  = self;
                g_VisStamp = now;
                g_VisValid = true;
            }
            else if (g_VisSelf != self)
            {
                return g_OrigIsVisible ? g_OrigIsVisible(self, row) : false;
            }

            if (const std::uint8_t c = g_VisCache[row])
                return c == 2;
            const bool v = g_OrigIsVisible ? g_OrigIsVisible(self, row) : false;
            g_VisCache[row] = v ? std::uint8_t{ 2 } : std::uint8_t{ 1 };
            return v;
        }

        bool VerifyBoundSite(const BoundImmSite& s)
        {
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(s.addr));
            if (!addr)
                return false;
            __try
            {
                std::uint32_t v = 0;
                std::memcpy(&v, addr + s.immOff, 4);
                if (v != 0x400)
                {
                    NoteVerifyFail("bound site %p+%u: imm 0x%X is not 0x400",
                                   addr, s.immOff, v);
                    return false;
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                NoteVerifyFail("bound site %p: unreadable", addr);
                return false;
            }
        }

        void RevertAllPatches()
        {
            g_Active = false;
            g_ArmedBase20.store(0, std::memory_order_relaxed);
            for (const ImmPatch& p : kPatches)
                if (p.addr)
                    WriteImmAt(p, p.oldImm);
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
                if (g_CtlDispOff[i])
                    WriteU32At(kCtlDispSites[i].addr, g_CtlDispOff[i],
                               kCtlDispSites[i].oldDisp);
            for (std::size_t i = 0; i < kBoundSiteCount; ++i)
                WriteU32At(kBoundImmSites[i].addr, kBoundImmSites[i].immOff, 0x400);
            for (const ByteFix& f : kByteFixes)
                WriteByteFix(f, f.oldBytes);
        }

        void DeactivateGrow(const char* why)
        {
            g_Active = false;
            g_ArmedBase20.store(0, std::memory_order_relaxed);
            LogError("DevelopArrayGrow: %s, rows >= 1024 disabled; restart before "
                     "developing", why);
        }

        bool ProbeGrownRowsClean(std::uintptr_t base20)
        {
            for (std::uint32_t i = kOldRows; i < kNewRows; ++i)
            {
                const std::uint16_t w =
                    *reinterpret_cast<const std::uint16_t*>(
                        base20 + 8
                        + static_cast<std::size_t>(i) * kRecStride);
                if (i == kFlowSentinel)
                {
                    if (w != 0 && w != 0xFFFF)
                        return false;
                }
                else if (w != 0)
                    return false;
            }
            return true;
        }

        void ClaimReservedRows(std::uintptr_t block)
        {
            std::uint32_t mask = 0;
            for (std::uint32_t row = kGunsmithFlowFirst;
                 row <= kFlowSentinel; ++row)
            {
                std::uint8_t* rec = reinterpret_cast<std::uint8_t*>(
                    block + 0x28
                    + static_cast<std::size_t>(row) * kRecStride);
                const std::uint16_t word0 =
                    *reinterpret_cast<std::uint16_t*>(rec);
                if (word0 != 0 && word0 != 0xFFFF)
                {
                    static std::atomic<int> s_occupied{ 0 };
                    if (s_occupied.fetch_add(1, std::memory_order_relaxed) < 4)
                        LogWarn("DevelopArrayGrow: reserved develop row %u already "
                                "holds developId %u, left to the game",
                                row, static_cast<unsigned>(word0));
                    continue;
                }
                if (word0 != 0xFFFF)
                {
                    std::memset(rec, 0, kRecStride);
                    *reinterpret_cast<std::uint16_t*>(rec) = 0xFFFF;
                    rec[0x36] = 0xFF;
                }
                if (row < kNewRows)
                {
                    g_FlagsShadow[row] |= kReqAnnouncedBit;
                    MirrorFlagByte(row);
                }
                if (row != kFlowSentinel)
                    mask |= 1u << (row - kGunsmithFlowFirst);
            }
            g_GunsmithClaimMask.store(mask, std::memory_order_relaxed);
        }

        void ArmFromBase20(std::uintptr_t base20)
        {
            __try
            {
                const std::uintptr_t block = base20 - 0x20;
                std::uint8_t** slot = reinterpret_cast<std::uint8_t**>(
                    block + kBlock_FlagsPtrOff);

                if (!ProbeGrownRowsClean(base20))
                {
                    DeactivateGrow("develop block predates the grow patches");
                    return;
                }

                ClaimReservedRows(block);

                std::uint8_t* svarsFlags = *slot;
                if (!svarsFlags)
                    return;
                if (svarsFlags != g_FlagsShadow)
                {
                    if (g_BlockSeen
                        && g_BlockSeen != reinterpret_cast<void*>(block))
                        LogWarn("DevelopArrayGrow: second develop block %p (first %p), "
                                "flags shadow re-bound",
                                reinterpret_cast<void*>(block), g_BlockSeen);
                    g_BlockSeen  = reinterpret_cast<void*>(block);
                    g_SvarsFlags = svarsFlags;
                    std::memcpy(g_FlagsShadow, svarsFlags, kOldRows);
                    std::memcpy(g_SyncMirror, svarsFlags, kOldRows);
                    std::memset(g_FlagsShadow + kOldRows, kReqAnnouncedBit,
                                kFlagsShadowSize - kOldRows);
                    MarkReservedRowsAnnounced();
                    MarkSilencedRowsAnnounced();
                    RestoreGrownAnnouncedBits();
                    *slot = g_FlagsShadow;
                    InvalidateDevelopVisibilityCache();
                    InvalidateDevelopLookupIndex();
                    EquipDevelop_RequestDevelopRestore();
                }
                else if (!g_BlockSeen)
                {
                    g_BlockSeen = reinterpret_cast<void*>(block);
                }
                g_ArmedBase20.store(base20, std::memory_order_relaxed);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogWarn("DevelopArrayGrow: fault probing develop block, grown "
                            "rows stay unarmed");
                }
            }
        }

        bool GrownRowFlagsBound(std::uintptr_t base20)
        {
            __try
            {
                return *reinterpret_cast<std::uint8_t* const*>(
                           base20 + kBase20_FlagsPtrOff) == g_FlagsShadow;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void __fastcall hkBlockReset(std::uintptr_t block)
        {
            g_OrigBlockReset(block);
            __try
            {
                ClaimReservedRows(block);

                std::uint8_t** slot = reinterpret_cast<std::uint8_t**>(
                    block + kBlock_FlagsPtrOff);
                std::uint8_t* svarsFlags = *slot;
                if (!svarsFlags)
                    return;
                if (g_BlockSeen && g_BlockSeen != reinterpret_cast<void*>(block))
                    LogWarn("DevelopArrayGrow: second develop block %p (first %p), "
                            "flags shadow re-bound",
                            reinterpret_cast<void*>(block), g_BlockSeen);
                g_BlockSeen  = reinterpret_cast<void*>(block);
                g_SvarsFlags = svarsFlags;
                std::memcpy(g_FlagsShadow, svarsFlags, kOldRows);
                std::memcpy(g_SyncMirror, svarsFlags, kOldRows);
                std::memset(g_FlagsShadow + kOldRows, kReqAnnouncedBit,
                            kFlagsShadowSize - kOldRows);
                MarkReservedRowsAnnounced();
                MarkSilencedRowsAnnounced();
                RestoreGrownAnnouncedBits();
                *slot = g_FlagsShadow;
                g_ArmedBase20.store(block + 0x20, std::memory_order_relaxed);
                InvalidateDevelopVisibilityCache();
                InvalidateDevelopLookupIndex();
                EquipDevelop_RequestDevelopRestore();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogWarn("DevelopArrayGrow: fault re-arming the flags shadow on "
                        "develop block reset");
            }
        }

        bool RecordFieldAt(std::uintptr_t base20, std::uint16_t row,
                           bool equipField, std::uint16_t& out);

        void RearmNewBadgeOnDevelop(std::uintptr_t base20, std::uint16_t idx)
        {
            if (idx < FirstCustomRow())
                return;
            std::uint16_t developId = 0;
            if (!RecordFieldAt(base20, idx, false, developId) || developId == 0)
                return;
            const std::int32_t id = static_cast<std::int32_t>(developId);
            if (!V_FrameWorkState::IsManagedDevelopId(id))
                return;
            V_FrameWorkState::SetNewByDevelopId(id, true);
        }

        void __fastcall hkSetDeveloped(std::uintptr_t base20, std::uint16_t idx)
        {
            if (idx == kFlowSentinel)
                return;
            if (idx >= equip::NativeFlowBound())
            {
                static std::atomic<int> s_overBound{ 0 };
                if (s_overBound.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("DevelopArrayGrow: SetEquipDeveloped index %u over bound %u, "
                            "refused", static_cast<unsigned>(idx), equip::NativeFlowBound());
                return;
            }
            if (idx >= kOldRows && !GrownRowFlagsBound(base20))
            {
                static std::atomic<int> s_unbound{ 0 };
                if (s_unbound.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("DevelopArrayGrow: SetEquipDeveloped row %u refused, flags "
                            "shadow unbound", static_cast<unsigned>(idx));
                return;
            }
            void* ctl = reinterpret_cast<void*>(base20);
            const bool timerProof =
                idx >= FirstCustomRow() && EquipDevelop_WasDevelopTimerActive(idx);
            if (EquipDevelop_ShouldSuppressNativeDevelop(ctl, idx))
                return;
            RearmNewBadgeOnDevelop(base20, idx);
            g_OrigSetDeveloped(base20, idx);
            MirrorFlagByte(idx);
            EquipDevelop_NotifyNativeDevelopChanged(ctl, idx, true, timerProof);
        }

        void __fastcall hkSetUndeveloped(std::uintptr_t base20,
                                         std::uint16_t idx, char notify)
        {
            if (idx == kFlowSentinel)
                return;
            if (idx >= equip::NativeFlowBound())
            {
                static std::atomic<int> s_overBound{ 0 };
                if (s_overBound.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("DevelopArrayGrow: SetEquipUndeveloped index %u over bound %u, "
                            "refused", static_cast<unsigned>(idx), equip::NativeFlowBound());
                return;
            }
            if (idx >= kOldRows && !GrownRowFlagsBound(base20))
            {
                static std::atomic<int> s_unbound{ 0 };
                if (s_unbound.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("DevelopArrayGrow: SetEquipUndeveloped row %u refused, flags "
                            "shadow unbound", static_cast<unsigned>(idx));
                return;
            }
            void* ctl = reinterpret_cast<void*>(base20);
            g_OrigSetUndeveloped(base20, idx, notify);
            MirrorFlagByte(idx);
            EquipDevelop_NotifyNativeDevelopChanged(ctl, idx, false);
        }

        constexpr std::uint32_t kDevIdSlots   = 0x10000;
        constexpr std::uint32_t kEquipIdSlots = 0x10000;
        constexpr std::uint64_t kLookupTtlMs  = 16;

        std::uint16_t  g_DevIdRow[kDevIdSlots]          = {};
        std::uint16_t  g_EquipIdRow[kEquipIdSlots]      = {};
        std::uint16_t  g_EquipIdRowManaged[kEquipIdSlots] = {};
        bool           g_LookupValid  = false;
        std::uintptr_t g_LookupBase   = 0;
        std::uint32_t  g_LookupBound  = 0;
        std::uint64_t  g_LookupStamp  = 0;

        std::uint64_t g_VanIdentityBits[kEquipIdSlots / 64] = {};
        bool          g_VanIdentityDirty = false;

        void BuildLookupIndex(std::uintptr_t base20, std::uint32_t bound)
        {
            std::memset(g_DevIdRow, 0xFF, sizeof(g_DevIdRow));
            std::memset(g_EquipIdRow, 0xFF, sizeof(g_EquipIdRow));
            std::memset(g_EquipIdRowManaged, 0xFF, sizeof(g_EquipIdRowManaged));
            std::uint64_t vanBits[kEquipIdSlots / 64] = {};
            const std::uint32_t first = FirstCustomRow();
            const std::uint8_t* base =
                reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = 0; i < bound; ++i)
            {
                if (IsReservedFlowRow(i))
                    continue;
                const std::uint8_t* rec =
                    base + static_cast<std::size_t>(i) * kRecStride;
                const std::uint16_t dev = *reinterpret_cast<const std::uint16_t*>(
                    rec + kBase20_RecFieldOff);
                if (g_DevIdRow[dev] == 0xFFFF)
                    g_DevIdRow[dev] = static_cast<std::uint16_t>(i);
                const std::uint16_t eqp = equip::DevelopRecordEquipId(rec);
                if (eqp < kEquipIdSlots && g_EquipIdRow[eqp] == 0xFFFF)
                    g_EquipIdRow[eqp] = static_cast<std::uint16_t>(i);
                if (eqp != 0 && eqp < kEquipIdSlots)
                {
                    if (i < first)
                        vanBits[eqp >> 6] |= 1ull << (eqp & 63);
                    else if (g_EquipIdRowManaged[eqp] == 0xFFFF)
                        g_EquipIdRowManaged[eqp] = static_cast<std::uint16_t>(i);
                }
            }
            if (std::memcmp(vanBits, g_VanIdentityBits, sizeof(vanBits)) != 0)
            {
                std::memcpy(g_VanIdentityBits, vanBits, sizeof(vanBits));
                g_VanIdentityDirty = true;
            }
            g_LookupBase  = base20;
            g_LookupBound = bound;
            g_LookupStamp = GetTickCount64();
            g_LookupValid = true;
        }

        void PushVanillaIdentityIds()
        {
            if (!g_VanIdentityDirty)
                return;
            g_VanIdentityDirty = false;
            std::vector<std::int32_t> ids;
            ids.reserve(512);
            for (std::uint32_t w = 0; w < kEquipIdSlots / 64; ++w)
            {
                if (g_VanIdentityBits[w] == 0)
                    continue;
                for (std::uint32_t b = 0; b < 64; ++b)
                    if (g_VanIdentityBits[w] & (1ull << b))
                        ids.push_back(static_cast<std::int32_t>(w * 64 + b));
            }
            if (ids.size() < 64)
                return;
            V_FrameWorkState::SetVanillaIdentityEquipIds(ids.data(), ids.size());
        }

        std::uint32_t g_LookupProbe = 0;

        bool RecordFieldAt(std::uintptr_t base20, std::uint16_t row,
                           bool equipField, std::uint16_t& out)
        {
            if (row >= equip::NativeFlowBound() || IsReservedFlowRow(row))
                return false;
            __try
            {
                const std::uint8_t* rec =
                    reinterpret_cast<const std::uint8_t*>(base20)
                    + static_cast<std::size_t>(row) * kRecStride;
                out = equipField
                    ? equip::DevelopRecordEquipId(rec)
                    : *reinterpret_cast<const std::uint16_t*>(rec + kBase20_RecFieldOff);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool EnsureLookupIndex(std::uintptr_t base20)
        {
            const std::uint32_t bound = equip::NativeFlowBound();
            if (g_LookupValid
                && g_LookupBase == base20
                && g_LookupBound == bound)
            {
                if ((++g_LookupProbe & 0xFF) != 0)
                    return true;
                if (GetTickCount64() - g_LookupStamp <= kLookupTtlMs)
                    return true;
            }
            __try
            {
                BuildLookupIndex(base20, bound);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_LookupValid = false;
                return false;
            }
            PushVanillaIdentityIds();
            return true;
        }

        bool ServeFromIndex(std::uintptr_t base20, std::uint16_t key,
                            bool equipField, std::uint16_t& out)
        {
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                if (!EnsureLookupIndex(base20))
                    return false;
                const std::uint16_t row =
                    equipField ? g_EquipIdRow[key] : g_DevIdRow[key];
                if (row == 0xFFFF)
                {
                    out = kFlowSentinel;
                    return true;
                }
                std::uint16_t live = 0;
                if (RecordFieldAt(base20, row, equipField, live) && live == key)
                {
                    out = row;
                    return true;
                }
                InvalidateDevelopLookupIndex();
            }
            return false;
        }

        std::uint16_t __fastcall hkFindByDevId(std::uintptr_t base20,
                                               std::uint16_t developId)
        {
            EquipDevelop_RefreshDevelopTimerSnapshot();
            std::uint16_t served = 0;
            if (ServeFromIndex(base20, developId, false, served))
                return served;
            const std::uint8_t* base =
                reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = 0; i < equip::NativeFlowBound(); ++i)
            {
                if (IsReservedFlowRow(i))
                    continue;
                if (*reinterpret_cast<const std::uint16_t*>(
                        base + static_cast<std::size_t>(i) * kRecStride
                        + kBase20_RecFieldOff) == developId)
                    return static_cast<std::uint16_t>(i);
            }
            return kFlowSentinel;
        }

        std::uint16_t __fastcall hkFindByEquipId(std::uintptr_t base20,
                                                 std::uint16_t engineEquipId)
        {
            const auto equipId = static_cast<std::uint16_t>(equip::FromEngine(engineEquipId));
            if (equipId == 0)
                return kFlowSentinel;
            EquipDevelop_RefreshDevelopTimerSnapshot();
            if (equipId < kEquipIdSlots
                && !MissionCodeGuard::ShouldBypassHooks()
                && V_FrameWorkState::IsClaimedEquipId(
                       static_cast<std::int32_t>(equipId))
                && EnsureLookupIndex(base20))
            {
                const std::uint16_t mrow = g_EquipIdRowManaged[equipId];
                std::uint16_t live = 0;
                if (mrow != 0xFFFF
                    && RecordFieldAt(base20, mrow, true, live)
                    && live == equipId)
                    return mrow;
            }
            std::uint16_t served = 0;
            if (equipId < kEquipIdSlots
                && ServeFromIndex(base20, equipId, true, served))
                return served;
            const std::uint8_t* base =
                reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = 0; i < equip::NativeFlowBound(); ++i)
            {
                if (IsReservedFlowRow(i))
                    continue;
                if (equip::DevelopRecordEquipId(base + static_cast<std::size_t>(i) * kRecStride) == equipId)
                    return static_cast<std::uint16_t>(i);
            }
            return kFlowSentinel;
        }

        bool IsGrownBase20(std::uintptr_t base20)
        {
            return g_BlockSeen
                && base20 == reinterpret_cast<std::uintptr_t>(g_BlockSeen)
                             + 0x20;
        }

        bool RowQualifiesForList(std::uintptr_t base20, std::uint32_t idx)
        {
            const std::uintptr_t block = base20 - 0x20;
            const std::uintptr_t vtbl =
                *reinterpret_cast<const std::uintptr_t*>(block);
            const RowListGate_t gate =
                *reinterpret_cast<const RowListGate_t*>(vtbl + 0x30);
            if (!gate || gate(block, static_cast<std::uint16_t>(idx)) == 0)
                return false;
            if (*reinterpret_cast<const std::uint16_t*>(
                    base20 + 8
                    + static_cast<std::size_t>(idx) * kRecStride) == 0)
                return false;
            const std::uint8_t* flags =
                *reinterpret_cast<std::uint8_t* const*>(
                    base20 + kBase20_FlagsPtrOff);
            return flags && (flags[idx] & 1) != 0;
        }

        thread_local bool         t_SortiePrepListArmed  = false;
        thread_local std::uint8_t t_SortiePrepArmedTab   = 0;
        thread_local bool         t_SortiePrepRowsFirst  = false;
        thread_local std::uint8_t t_SortiePrepFillTab    = 0;

        std::uint8_t SortiePrepListsShowingRow(std::uintptr_t base20, std::uint32_t idx)
        {
            const auto* rec = reinterpret_cast<const std::uint8_t*>(
                base20 + static_cast<std::size_t>(idx) * kRecStride);
            std::uint8_t lists = 0;
            switch (rec[kBase20_RecDevelopTypeOff])
            {
            case 0x13: lists |= kSortiePrepType13Rows; break;
            case 0x14: lists |= kSortiePrepType14Rows; break;
            case 0x17: lists |= kSortiePrepType17Rows; break;
            case 0x18: lists |= kSortiePrepType18Rows; break;
            case 0x19: lists |= kSortiePrepType19Rows; break;
            default: break;
            }
            if (rec[kBase20_RecDevelopTypeOff + 1] == '[')
                lists |= kSortiePrepHeadOptionRows;
            const std::uint16_t equipId = *reinterpret_cast<const std::uint16_t*>(rec + 0xC) >> 4;
            if (equipId >= 0x1F2 && equipId <= 0x1FB) // the item list picks these by equipId, any type
                lists |= kSortiePrepItemRows;
            return lists;
        }

        int SortiePrepFillTier(std::uintptr_t base20, std::uint32_t idx, std::uint8_t openTab)
        {
            const std::uint8_t lists = SortiePrepListsShowingRow(base20, idx);
            if (lists & openTab)
                return 0;
            return lists != 0 ? 1 : 2;
        }

        constexpr std::size_t   kBase20_RecDevelopGmpCostOff = 0x24;
        constexpr std::uint16_t kCboxDsrDevelopId            = 12000;

        std::uint32_t DevelopZeroCostEquips(std::uintptr_t base20)
        {
            const std::uintptr_t block = base20 - 0x20;
            const std::uintptr_t vtbl =
                *reinterpret_cast<const std::uintptr_t*>(block);
            const RowListGate_t gate =
                *reinterpret_cast<const RowListGate_t*>(vtbl + 0x30);
            const SetItemDevelopedLv_t setItemDevelopedLv =
                *reinterpret_cast<const SetItemDevelopedLv_t*>(vtbl + 0x68);
            std::uint8_t* const flags =
                *reinterpret_cast<std::uint8_t* const*>(
                    base20 + kBase20_FlagsPtrOff);
            if (!gate || !setItemDevelopedLv || !flags)
                return 0;

            const std::uint32_t bound =
                GrownRowFlagsBound(base20) ? kNewRows : kOldRows;
            std::uint32_t developed = 0;
            for (std::uint32_t i = 0; i < bound; ++i)
            {
                if (IsReservedFlowRow(i))
                    continue;
                const std::uint16_t row = static_cast<std::uint16_t>(i);
                if (gate(block, row) == 0)
                    continue;
                const std::uintptr_t rec =
                    base20 + static_cast<std::size_t>(i) * kRecStride;
                const std::uint16_t developId =
                    *reinterpret_cast<const std::uint16_t*>(
                        rec + kBase20_RecFieldOff);
                const std::uint32_t developGmpCost =
                    *reinterpret_cast<const std::uint32_t*>(
                        rec + kBase20_RecDevelopGmpCostOff);
                if (developId == 0 || developId == kCboxDsrDevelopId
                    || developGmpCost != 0)
                    continue;
                flags[i] |= 1;
                setItemDevelopedLv(block, row);
                ++developed;
            }
            if (developed != 0)
                InvalidateDevelopVisibilityCache();
            return developed;
        }

        constexpr std::uint8_t  kDevelopTypeEquip        = 0x12;   // EQP_DEV_TYPE_Equip: items
        constexpr std::uint32_t kMaxSupersededItemGrades = 256;

        thread_local bool          t_HideSupersededItemGrades = false;
        thread_local std::uint16_t t_SupersededItemDevelopIds[kMaxSupersededItemGrades];
        thread_local std::uint32_t t_SupersededItemCount = 0;

        // Sortie Prep lists one grade of an item, the latest, like the game's own items: a custom item row
        // whose next grade (a custom row naming it as its base) is developed stays out of that list.
        // Developed, not listed: the game's own row gate can hide a middle grade, which must still count.
        void CollectSupersededItemGradesSEH(std::uintptr_t base20)
        {
            t_SupersededItemCount = 0;
            __try
            {
                const std::uint8_t* developed = *reinterpret_cast<std::uint8_t* const*>(base20 + kBase20_FlagsPtrOff);
                if (!developed)
                    return;
                const std::uint32_t bound = NativeFlowBound();
                for (std::uint32_t i = FirstCustomFlowIndex(); i < bound; ++i)
                {
                    if (IsReservedFlowRow(i))
                        continue;
                    const auto* rec = reinterpret_cast<const std::uint8_t*>(
                        base20 + static_cast<std::size_t>(i) * kRecStride);
                    const std::uint16_t baseDevelopId = *reinterpret_cast<const std::uint16_t*>(rec + 0xA);
                    if (rec[kBase20_RecDevelopTypeOff] != kDevelopTypeEquip || baseDevelopId == 0
                        || (developed[i] & 1) == 0)
                        continue;
                    if (t_SupersededItemCount < kMaxSupersededItemGrades)
                        t_SupersededItemDevelopIds[t_SupersededItemCount++] = baseDevelopId;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                t_SupersededItemCount = 0;
            }
        }

        bool IsSupersededItemGrade(std::uintptr_t base20, std::uint32_t idx)
        {
            if (!t_HideSupersededItemGrades || t_SupersededItemCount == 0 || idx < FirstCustomFlowIndex())
                return false;
            const auto* rec = reinterpret_cast<const std::uint8_t*>(
                base20 + static_cast<std::size_t>(idx) * kRecStride);
            if (rec[kBase20_RecDevelopTypeOff] != kDevelopTypeEquip)
                return false;
            const std::uint16_t developId = *reinterpret_cast<const std::uint16_t*>(rec + kBase20_RecFieldOff);
            for (std::uint32_t k = 0; k < t_SupersededItemCount; ++k)
                if (t_SupersededItemDevelopIds[k] == developId)
                    return true;
            return false;
        }

        std::uint16_t __fastcall hkListDevCount(std::uintptr_t base20)
        {
            const bool sortiePrep = t_SortiePrepListArmed;
            t_SortiePrepListArmed = false;
            t_SortiePrepRowsFirst = false;
            t_SortiePrepFillTab   = sortiePrep ? t_SortiePrepArmedTab : 0;
            t_HideSupersededItemGrades = false;
            if (!IsGrownBase20(base20))
                return g_OrigListDevCount(base20);
            SyncFlagsWithSvars(true);
            if (sortiePrep)
            {
                CollectSupersededItemGradesSEH(base20);
                t_HideSupersededItemGrades = true;
            }
            __try
            {
                std::uint32_t n = 0;
                for (std::uint32_t i = 0; i < kNewRows; ++i)
                {
                    if (IsReservedFlowRow(i))
                        continue;
                    if (RowQualifiesForList(base20, i) && !IsSupersededItemGrade(base20, i))
                        ++n;
                }
                if (n == 0)
                    n = DevelopZeroCostEquips(base20);
                if (n > kOldRows)
                {
                    static bool logged = false;
                    static bool loggedSortiePrep = false;
                    if (sortiePrep)
                    {
                        t_SortiePrepRowsFirst = true;
                        if (!loggedSortiePrep)
                        {
                            loggedSortiePrep = true;
                            LogWarn("DevelopArrayGrow: %u develop menu entries over the 1024 cap, "
                                    "Sortie Prep lists the open tab's rows first, then the other kinds it shows", n);
                        }
                    }
                    else if (!logged)
                    {
                        logged = true;
                        LogWarn("DevelopArrayGrow: %u develop menu entries over the "
                                "1024 cap, extras not listed", n);
                    }
                    n = kOldRows;
                }
                return static_cast<std::uint16_t>(n);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogWarn("DevelopArrayGrow: fault counting develop rows, equip list "
                            "empty this pass");
                }
                return 0;
            }
        }

        void __fastcall hkListDevFill(std::uintptr_t base20,
                                      std::uint16_t maxCount,
                                      std::uint16_t* outBuf)
        {
            const bool tiered = t_SortiePrepRowsFirst;
            const std::uint8_t openTab = t_SortiePrepFillTab;
            t_SortiePrepRowsFirst = false;
            if (!IsGrownBase20(base20))
            {
                t_HideSupersededItemGrades = false;
                g_OrigListDevFill(base20, maxCount, outBuf);
                return;
            }
            SyncFlagsWithSvars(true);
            __try
            {
                if (!outBuf || maxCount == 0)
                    return;
                std::uint16_t w = 0;
                for (int tier = 0; tier < (tiered ? 3 : 1); ++tier)
                    for (std::uint32_t i = 0; i < kNewRows && w < maxCount; ++i)
                    {
                        if (IsReservedFlowRow(i))
                            continue;
                        if (tiered && SortiePrepFillTier(base20, i, openTab) != tier)
                            continue;
                        if (RowQualifiesForList(base20, i) && !IsSupersededItemGrade(base20, i))
                            outBuf[w++] = static_cast<std::uint16_t>(i);
                    }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                static bool s_warned = false;
                if (!s_warned)
                {
                    s_warned = true;
                    LogWarn("DevelopArrayGrow: fault filling develop rows, equip list "
                            "truncated this pass");
                }
            }
            t_HideSupersededItemGrades = false;
        }

        std::uint16_t FindLastRowForEquipId(std::uintptr_t base20,
                                            std::uint16_t engineEquipId)
        {
            const auto equipId = static_cast<std::uint16_t>(equip::FromEngine(engineEquipId));
            std::uint16_t found = kFlowSentinel;
            const std::uint32_t bound = equip::NativeFlowBound();
            for (std::uint32_t i = 0; i < bound; ++i)
            {
                const auto* rec = reinterpret_cast<const std::uint8_t*>(
                    base20 + static_cast<std::size_t>(i) * kRecStride);
                if (equip::DevelopRecordEquipId(rec) == equipId)
                    found = static_cast<std::uint16_t>(i);
            }
            return found;
        }

        long long __fastcall hkEquipIdCount(std::uintptr_t base20,
                                            std::uint16_t equipId)
        {
            __try
            {
                const std::uint16_t found =
                    FindLastRowForEquipId(base20, equipId);
                const std::uintptr_t block = base20 - 0x20;
                const std::uintptr_t vt =
                    *reinterpret_cast<const std::uintptr_t*>(block);
                const RowQueryTail_t tail =
                    *reinterpret_cast<const RowQueryTail_t*>(vt + 0x90);
                const long long a = tail(block, equipId, found);

                const GetQuark_t getQuark = reinterpret_cast<GetQuark_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable));
                if (!getQuark)
                    return a;
                const std::uint8_t* quark = getQuark();
                if (!quark)
                    return a;
                const std::uintptr_t ext =
                    *reinterpret_cast<const std::uintptr_t*>(
                        *reinterpret_cast<const std::uintptr_t*>(quark + 0x98)
                        + 0x180);
                const std::uintptr_t extVt =
                    *reinterpret_cast<const std::uintptr_t*>(ext);
                const ExtCount_t cnt =
                    *reinterpret_cast<const ExtCount_t*>(extVt + 0xA8);
                return a + cnt(ext);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return g_OrigEquipIdCount(base20, equipId);
            }
        }

        long long __fastcall hkEquipIdRow(std::uintptr_t base20,
                                          std::uint16_t equipId)
        {
            __try
            {
                if (equipId == 0)
                    return 0;
                const std::uint16_t found =
                    FindLastRowForEquipId(base20, equipId);
                const std::uintptr_t block = base20 - 0x20;
                const std::uintptr_t vt =
                    *reinterpret_cast<const std::uintptr_t*>(block);
                const RowQueryTail_t tail =
                    *reinterpret_cast<const RowQueryTail_t*>(vt + 0x90);
                return tail(block, equipId, found);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return g_OrigEquipIdRow(base20, equipId);
            }
        }

        std::uint16_t __fastcall hkGetBaseId(std::uintptr_t base20,
                                             std::uint16_t row)
        {
            if (row >= static_cast<std::uint16_t>(kNewRows)
                || IsReservedFlowRow(row))
                return kFlowSentinel;
            const std::uint16_t parent = g_OrigGetBaseId(base20, row);
            if (parent >= static_cast<std::uint16_t>(kNewRows)
                || IsReservedFlowRow(parent))
                return kFlowSentinel;
            if (parent == row)
            {
                static std::atomic<int> s_selfParent{ 0 };
                if (s_selfParent.fetch_add(1, std::memory_order_relaxed) < 4)
                    LogWarn("DevelopArrayGrow: develop row %u is its own parent, "
                            "treated as no parent", static_cast<unsigned>(row));
                return kFlowSentinel;
            }
            return parent;
        }

        std::uint16_t __fastcall hkGetDerivationDevelopIndex(std::uintptr_t block,
                                                             std::uint16_t row,
                                                             std::uint8_t nth)
        {
            if (row >= static_cast<std::uint16_t>(kNewRows)
                || IsReservedFlowRow(row))
                return kFlowSentinel;
            const std::uint16_t found =
                g_OrigGetDerivationDevelopIndex(block, row, nth);
            if (found >= static_cast<std::uint16_t>(kNewRows)
                || IsReservedFlowRow(found))
                return kFlowSentinel;
            return found;
        }

        bool VerifyPatchSite(const ImmPatch& p)
        {
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(p.addr));
            if (!addr)
                return false;
            __try
            {
                if (std::memcmp(addr, p.op, p.opLen) != 0)
                {
                    char have[16] = {};
                    char want[16] = {};
                    for (std::uint8_t i = 0; i < p.opLen && i < 5; ++i)
                    {
                        std::snprintf(have + i * 3, 4, "%02X ", addr[i]);
                        std::snprintf(want + i * 3, 4, "%02X ", p.op[i]);
                    }
                    NoteVerifyFail("site %s @%p: opcode mismatch (have %s- "
                                   "expected %s)", p.what, addr, have, want);
                    return false;
                }
                std::uint32_t imm;
                std::memcpy(&imm, addr + p.opLen, 4);
                if (imm != p.oldImm && imm != p.newImm)
                {
                    NoteVerifyFail("site %s @%p: imm 0x%X is neither orig "
                                   "0x%X nor patched 0x%X", p.what, addr, imm,
                                   p.oldImm, p.newImm);
                    return false;
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                NoteVerifyFail("site %s @%p: unreadable", p.what, addr);
                return false;
            }
        }

        bool ApplyPatchSite(const ImmPatch& p)
        {
            std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(
                const_cast<void*>(ResolveGameAddress(p.addr)));
            if (!addr)
                return false;
            DWORD oldProt = 0;
            if (!VirtualProtect(addr, p.opLen + 4, PAGE_EXECUTE_READWRITE,
                                &oldProt))
            {
                ReportProtectFailure(p.what, static_cast<void*>(addr), GetLastError());
                return false;
            }
            std::memcpy(addr + p.opLen, &p.newImm, 4);
            DWORD tmp = 0;
            VirtualProtect(addr, p.opLen + 4, oldProt, &tmp);
            FlushInstructionCache(GetCurrentProcess(), addr, p.opLen + 4);
            return true;
        }

        std::size_t ListedPatchCount()
        {
            std::size_t listed = 0;
            for (std::size_t i = 0; i < kPatchCount; ++i)
                if (kPatches[i].addr)
                    ++listed;
            return listed;
        }

        int TotalPatchCount()
        {
            return static_cast<int>(ListedPatchCount() + kCtlSiteCount
                                    + kBoundSiteCount + kByteFixCount);
        }

        int ApplyAllPatches()
        {
            int applied = 0;
            for (const ImmPatch& p : kPatches)
                if (p.addr && ApplyPatchSite(p))
                    ++applied;
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
                if (WriteU32At(kCtlDispSites[i].addr, g_CtlDispOff[i],
                               CtlNewDisp(kCtlDispSites[i].addr + g_CtlDispOff[i],
                                          kCtlDispSites[i].oldDisp)))
                    ++applied;
            for (std::size_t i = 0; i < kBoundSiteCount; ++i)
                if (WriteU32At(kBoundImmSites[i].addr,
                               kBoundImmSites[i].immOff, kNewRows))
                    ++applied;
            for (const ByteFix& f : kByteFixes)
                if (WriteByteFix(f, f.newBytes))
                    ++applied;
            return applied;
        }

        constexpr int kMaxSuspendedThreads = 512;

        int SuspendOtherThreads(HANDLE* handles)
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap == INVALID_HANDLE_VALUE)
                return 0;
            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            int n = 0;
            const DWORD pid = GetCurrentProcessId();
            const DWORD tid = GetCurrentThreadId();
            if (Thread32First(snap, &te))
            {
                do
                {
                    if (te.th32OwnerProcessID == pid
                        && te.th32ThreadID != tid
                        && n < kMaxSuspendedThreads)
                    {
                        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE,
                                              te.th32ThreadID);
                        if (h)
                        {
                            if (SuspendThread(h) != static_cast<DWORD>(-1))
                                handles[n++] = h;
                            else
                                CloseHandle(h);
                        }
                    }
                } while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
            g_OtherThreadsSuspended = true;
            return n;
        }

        void ResumeOtherThreads(HANDLE* handles, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                ResumeThread(handles[i]);
                CloseHandle(handles[i]);
            }
            g_OtherThreadsSuspended = false;
            if (g_DeferredProtectFails != 0)
            {
                LogWarn("DevelopArrayGrow: %d patch write(s) failed, first %s @%p: "
                        "VirtualProtect error %lu", g_DeferredProtectFails,
                        g_DeferredProtectWhat ? g_DeferredProtectWhat : "?",
                        g_DeferredProtectAddr, g_DeferredProtectErr);
                g_DeferredProtectFails = 0;
            }
        }

        using QuarkAlloc_t = void* (__fastcall*)(std::uint64_t size,
                                                 std::uint32_t align,
                                                 std::uint32_t flags);

        QuarkAlloc_t g_OrigQuarkAlloc      = nullptr;
        void*        g_HookQuarkAlloc      = nullptr;
        bool         g_AllocGuardInstalled = false;

        void* __fastcall hkQuarkBlockAlloc(std::uint64_t size,
                                           std::uint32_t align,
                                           std::uint32_t flags)
        {
            std::uint64_t realSize = size;
            if (size == kOldAllocSize && align == 0x10 && flags == 0x3002C)
            {
                realSize = kNewAllocSize;
                LogWarn("DevelopArrayGrow: develop block alloc asked old size 0x%X, "
                        "grown to 0x%X", kOldAllocSize, kNewAllocSize);
            }
            void* p = g_OrigQuarkAlloc(realSize, align, flags);
            if (p && realSize == kNewAllocSize && align == 0x10
                && flags == 0x3002C)
                std::memset(p, 0, kNewAllocSize);
            return p;
        }

        bool TryReadU32(std::uintptr_t va, std::uint32_t off,
                        std::uint32_t& out)
        {
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(va));
            if (!addr)
                return false;
            __try
            {
                std::memcpy(&out, addr + off, 4);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool TryReadBytes(std::uintptr_t va, void* out, std::size_t n)
        {
            const std::uint8_t* addr = reinterpret_cast<const std::uint8_t*>(
                ResolveGameAddress(va));
            if (!addr)
                return false;
            __try
            {
                std::memcpy(out, addr, n);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        struct HealedSite
        {
            std::uint8_t  cls;
            std::uint16_t idx;
        };
        constexpr std::size_t kMaxHealed = 600;

        constexpr std::size_t kDetourWindow = 5;

        bool SiteInsideOurDetour(std::uintptr_t va)
        {
            void* const site = ResolveGameAddress(va);
            if (!site)
                return false;

            void* const hooks[] = {
                g_HookBlockReset,    g_HookSetDeveloped,  g_HookSetUndeveloped,
                g_HookFindByDevId,   g_HookFindByEquipId, g_HookListDevCount,
                g_HookListDevFill,   g_HookEquipIdCount,  g_HookEquipIdRow,
                g_HookGetBaseId,     g_HookGetDerivationDevelopIndex,
                g_HookEdcDtor,
            };

            const std::uintptr_t s = reinterpret_cast<std::uintptr_t>(site);
            for (void* h : hooks)
            {
                if (!h)
                    continue;
                const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(h);
                if (s >= b && s < b + kDetourWindow)
                    return true;
            }
            return false;
        }

        void RepairHealedPatches()
        {
            static HealedSite healed[kMaxHealed];
            std::size_t nHealed        = 0;
            int         nForeign       = 0;
            std::uintptr_t foreignAddr = 0;
            std::uint32_t  foreignVal  = 0;
            const char*    foreignWhat = nullptr;

            for (std::size_t i = 0; i < kPatchCount && nHealed < kMaxHealed;
                 ++i)
            {
                const ImmPatch& p = kPatches[i];
                if (!p.addr)
                    continue;
                std::uint32_t v = 0;
                if (!TryReadU32(p.addr, p.opLen, v))
                    continue;
                if (v == p.oldImm)
                    healed[nHealed++] =
                        { 0, static_cast<std::uint16_t>(i) };
                else if (v != p.newImm)
                {
                    if (nForeign++ == 0)
                    {
                        foreignAddr = p.addr;
                        foreignVal  = v;
                        foreignWhat = p.what;
                    }
                }
            }
            for (std::size_t i = 0; i < kCtlSiteCount && nHealed < kMaxHealed;
                 ++i)
            {
                if (SiteInsideOurDetour(kCtlDispSites[i].addr))
                    continue;
                std::uint32_t v = 0;
                if (!TryReadU32(kCtlDispSites[i].addr, g_CtlDispOff[i], v))
                    continue;
                if (v == kCtlDispSites[i].oldDisp)
                    healed[nHealed++] =
                        { 1, static_cast<std::uint16_t>(i) };
                else if (v != CtlNewDisp(kCtlDispSites[i].addr + g_CtlDispOff[i],
                                         kCtlDispSites[i].oldDisp))
                {
                    if (nForeign++ == 0)
                    {
                        foreignAddr = kCtlDispSites[i].addr;
                        foreignVal  = v;
                        foreignWhat = "ctl disp";
                    }
                }
            }
            for (std::size_t i = 0;
                 i < kBoundSiteCount && nHealed < kMaxHealed; ++i)
            {
                if (SiteInsideOurDetour(kBoundImmSites[i].addr))
                    continue;
                std::uint32_t v = 0;
                if (!TryReadU32(kBoundImmSites[i].addr,
                                kBoundImmSites[i].immOff, v))
                    continue;
                if (v == 0x400)
                    healed[nHealed++] =
                        { 2, static_cast<std::uint16_t>(i) };
                else if (v != kNewRows)
                {
                    if (nForeign++ == 0)
                    {
                        foreignAddr = kBoundImmSites[i].addr;
                        foreignVal  = v;
                        foreignWhat = "scan bound";
                    }
                }
            }
            for (std::size_t i = 0;
                 i < kByteFixCount && nHealed < kMaxHealed; ++i)
            {
                const ByteFix& f = kByteFixes[i];
                std::uint8_t cur[4] = {};
                if (!f.addr || f.len > 4
                    || !TryReadBytes(f.addr, cur, f.len))
                    continue;
                if (std::memcmp(cur, f.oldBytes, f.len) == 0)
                    healed[nHealed++] =
                        { 3, static_cast<std::uint16_t>(i) };
            }

            static int foreignLogged = 0;
            if (nForeign != 0 && foreignLogged < 3)
            {
                ++foreignLogged;
                LogWarn("DevelopArrayGrow: %d patched site(s) hold foreign values "
                        "(first %s @0x%llX = 0x%X), left untouched",
                        nForeign,
                        foreignWhat ? foreignWhat : "?",
                        static_cast<unsigned long long>(foreignAddr),
                        foreignVal);
            }
            if (nHealed == 0)
                return;

            static HANDLE suspended[kMaxSuspendedThreads];
            const int nSuspended = SuspendOtherThreads(suspended);
            std::size_t repaired = 0;
            for (std::size_t i = 0; i < nHealed; ++i)
            {
                bool ok = false;
                switch (healed[i].cls)
                {
                case 0:
                {
                    const ImmPatch& p = kPatches[healed[i].idx];
                    ok = WriteImmAt(p, p.newImm);
                    break;
                }
                case 1:
                {
                    const std::size_t k = healed[i].idx;
                    ok = WriteU32At(kCtlDispSites[k].addr, g_CtlDispOff[k],
                                    CtlNewDisp(kCtlDispSites[k].addr
                                                   + g_CtlDispOff[k],
                                               kCtlDispSites[k].oldDisp));
                    break;
                }
                case 2:
                {
                    const std::size_t k = healed[i].idx;
                    ok = WriteU32At(kBoundImmSites[k].addr,
                                    kBoundImmSites[k].immOff, kNewRows);
                    break;
                }
                case 3:
                {
                    const ByteFix& f = kByteFixes[healed[i].idx];
                    ok = WriteByteFix(f, f.newBytes);
                    break;
                }
                }
                if (ok)
                    ++repaired;
            }
            ResumeOtherThreads(suspended, nSuspended);

            LogWarn("DevelopArrayGrow: protector restored %zu patched site(s), "
                    "%zu re-applied", nHealed, repaired);
            InvalidateDevelopVisibilityCache();
            InvalidateDevelopLookupIndex();
        }

        std::uintptr_t g_LateCloneLogged[16] = {};
        std::size_t    g_LateCloneLoggedCount = 0;

        bool LateCloneAlreadyLogged(std::uintptr_t va)
        {
            for (std::size_t i = 0; i < g_LateCloneLoggedCount; ++i)
                if (g_LateCloneLogged[i] == va)
                    return true;
            if (g_LateCloneLoggedCount
                < sizeof(g_LateCloneLogged) / sizeof(g_LateCloneLogged[0]))
                g_LateCloneLogged[g_LateCloneLoggedCount++] = va;
            return false;
        }

        void RecheckForLateClones()
        {
            if (SweepImageForUnknownSites(true))
                return;
            const int nFind = g_SweepUnknownCount;
            if (nFind == 0)
                return;

            static std::uint16_t order[kMaxCtlSites];
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
                order[i] = static_cast<std::uint16_t>(i);
            for (std::size_t a = 1; a < kCtlSiteCount; ++a)
            {
                const std::uint16_t key = order[a];
                const std::uintptr_t kv =
                    kCtlDispSites[key].addr + g_CtlDispOff[key];
                std::size_t b = a;
                while (b > 0
                       && kCtlDispSites[order[b - 1]].addr
                              + g_CtlDispOff[order[b - 1]] > kv)
                {
                    order[b] = order[b - 1];
                    --b;
                }
                order[b] = key;
            }

            int g0 = 0;
            while (g0 < nFind)
            {
                int g1 = g0 + 1;
                while (g1 < nFind
                       && g_SweepUnknown[g1].va - g_SweepUnknown[g1 - 1].va
                              <= 0x1000)
                    ++g1;
                const int n = g1 - g0;

                int matches = 0;
                if (n >= 2 && static_cast<std::size_t>(n) <= kCtlSiteCount)
                {
                    for (std::size_t j = 0;
                         j + static_cast<std::size_t>(n) <= kCtlSiteCount;
                         ++j)
                    {
                        bool fit = true;
                        for (int i = 0; i < n && fit; ++i)
                        {
                            const CtlDispSite& s =
                                kCtlDispSites[order[j + i]];
                            if (s.oldDisp != g_SweepUnknown[g0 + i].disp)
                                fit = false;
                        }
                        for (int i = 0; i + 1 < n && fit; ++i)
                        {
                            const std::intptr_t fg = static_cast<std::intptr_t>(
                                g_SweepUnknown[g0 + i + 1].va
                                - g_SweepUnknown[g0 + i].va);
                            const std::intptr_t kg = static_cast<std::intptr_t>(
                                (kCtlDispSites[order[j + i + 1]].addr
                                 + g_CtlDispOff[order[j + i + 1]])
                                - (kCtlDispSites[order[j + i]].addr
                                   + g_CtlDispOff[order[j + i]]));
                            if (fg - kg > 0x80 || kg - fg > 0x80)
                                fit = false;
                        }
                        if (fit)
                            ++matches;
                    }
                }

                if (matches == 1)
                {
                    static HANDLE suspended[kMaxSuspendedThreads];
                    const int nSuspended = SuspendOtherThreads(suspended);
                    int patched = 0;
                    for (int i = 0; i < n; ++i)
                        if (WriteU32At(g_SweepUnknown[g0 + i].va, 0,
                                       CtlNewDisp(g_SweepUnknown[g0 + i].va,
                                                  g_SweepUnknown[g0 + i].disp)))
                            ++patched;
                    ResumeOtherThreads(suspended, nSuspended);
                    LogWarn("DevelopArrayGrow: late clone with %d develop-field "
                            "ref(s) at 0x%llX, %d patched", n,
                            static_cast<unsigned long long>(g_SweepUnknown[g0].va),
                            patched);
                }
                else if (!LateCloneAlreadyLogged(g_SweepUnknown[g0].va))
                {
                    LogWarn("DevelopArrayGrow: %d develop-field ref(s) at 0x%llX "
                            "match %d known runs, not patched; grown rows at risk",
                            n, static_cast<unsigned long long>(g_SweepUnknown[g0].va),
                            matches);
                }
                g0 = g1;
            }
        }

        std::atomic<bool> g_IntegrityThreadStarted{ false };
        std::atomic<bool> g_RetryThreadRunning{ false };

        DWORD WINAPI GrowIntegrityThread(LPVOID)
        {
            SetThreadPriority(GetCurrentThread(),
                              THREAD_PRIORITY_BELOW_NORMAL);
            for (int pass = 0; ; ++pass)
            {
                Sleep(pass < 36 ? 5000 : 30000);
                if (!g_Active)
                    continue;
                RepairHealedPatches();
                if ((pass & 3) == 3)
                    RecheckForLateClones();
            }
        }

        bool MigrateExistingBlock()
        {
            const GetQuark_t getQuark = reinterpret_cast<GetQuark_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            const QuarkAlloc_t quarkAlloc = reinterpret_cast<QuarkAlloc_t>(
                ResolveGameAddress(kAddr_QuarkBlockHeapAlloc));
            if (!getQuark || !quarkAlloc)
            {
                NoteVerifyFail("migrate: quark resolver addresses missing");
                return false;
            }

            static HANDLE suspended[kMaxSuspendedThreads];
            volatile int nSuspended = -1;

            __try
            {
                std::uint8_t* quark = getQuark();
                if (!quark)
                {
                    NoteVerifyFail("migrate: quark table is null");
                    return false;
                }
                const std::uintptr_t app =
                    *reinterpret_cast<const std::uintptr_t*>(quark + 0x98);
                if (!app)
                {
                    NoteVerifyFail("migrate: app object (quark+0x98) is null");
                    return false;
                }
                const std::uintptr_t f110 =
                    *reinterpret_cast<const std::uintptr_t*>(app + 0x110);
                if (!f110)
                {
                    NoteVerifyFail("migrate: holder (app+0x110) is null");
                    return false;
                }
                std::uintptr_t* slot =
                    reinterpret_cast<std::uintptr_t*>(f110 + 0xAC8);
                const std::uintptr_t base20 = *slot;
                if (!base20)
                {
                    NoteVerifyFail("migrate: develop slot (+0xAC8) is null");
                    return false;
                }
                const std::uintptr_t block = base20 - 0x20;

                const std::uintptr_t vt0 =
                    *reinterpret_cast<const std::uintptr_t*>(block);
                const std::uintptr_t vt20 =
                    *reinterpret_cast<const std::uintptr_t*>(block + 0x20);
                const std::uintptr_t vtblA = reinterpret_cast<std::uintptr_t>(
                    ResolveGameAddress(kAddr_BlockVtblA));
                const std::uintptr_t vtblB = reinterpret_cast<std::uintptr_t>(
                    ResolveGameAddress(kAddr_BlockVtblB));
                const std::uintptr_t vt20A = reinterpret_cast<std::uintptr_t>(
                    ResolveGameAddress(kAddr_Base20VtblA));
                const std::uintptr_t vt20B = reinterpret_cast<std::uintptr_t>(
                    ResolveGameAddress(kAddr_Base20VtblB));
                if ((vt0 != vtblA && vt0 != vtblB)
                    || (vt20 != vt20A && vt20 != vt20B))
                {
                    NoteVerifyFail("migrate: block %p vtables %p/%p do not "
                                   "match the develop block class",
                                   reinterpret_cast<void*>(block),
                                   reinterpret_cast<void*>(vt0),
                                   reinterpret_cast<void*>(vt20));
                    return false;
                }

                std::uint8_t* nb = static_cast<std::uint8_t*>(
                    quarkAlloc(kNewAllocSize, 0x10, 0x3002C));
                if (!nb)
                {
                    NoteVerifyFail("migrate: QuarkBlockHeapAlloc(0x%X) "
                                   "returned null", kNewAllocSize);
                    return false;
                }
                std::memset(nb, 0, kNewAllocSize);

                nSuspended = SuspendOtherThreads(suspended);
                if (*slot != base20)
                {
                    ResumeOtherThreads(suspended, nSuspended);
                    nSuspended = -1;
                    NoteVerifyFail("migrate: develop slot changed while "
                                   "threads were being suspended");
                    return false;
                }
                std::memcpy(nb, reinterpret_cast<const void*>(block), 0x28);
                std::memcpy(nb + 0x28,
                            reinterpret_cast<const void*>(block + 0x28),
                            static_cast<std::size_t>(kOldRows) * kRecStride);
                std::memcpy(nb + kNewCtlOff,
                            reinterpret_cast<const void*>(block + kOldCtlOff),
                            kCtlSize);
                const std::uint32_t* oldEquiped =
                    *reinterpret_cast<const std::uint32_t* const*>(
                        block + kWorkBufDisp);
                if (oldEquiped)
                    std::memcpy(g_EquipEquipedBits, oldEquiped,
                                kOldEquipEquipedWords * sizeof(std::uint32_t));
                *reinterpret_cast<std::uint32_t**>(
                    nb + kNewCtlOff + (kWorkBufDisp - kOldCtlOff)) =
                    g_EquipEquipedBits;
                std::memcpy(nb + kNewParallelOff,
                            reinterpret_cast<const void*>(block + 0x1A028),
                            static_cast<std::size_t>(kOldRows) * kParStride);

                const int applied = ApplyAllPatches();
                if (applied != TotalPatchCount())
                {
                    RevertAllPatches();
                    ResumeOtherThreads(suspended, nSuspended);
                    nSuspended = -1;
                    NoteVerifyFail("migrate: only %d of %d patches applied - "
                                   "reverted", applied, TotalPatchCount());
                    return false;
                }
                *slot = reinterpret_cast<std::uintptr_t>(nb + 0x20);
                g_GrownBlock         = reinterpret_cast<std::uintptr_t>(nb);
                g_MigratedSlot       = slot;
                g_MigratedOldBase20  = base20;
                ResumeOtherThreads(suspended, nSuspended);
                nSuspended = -1;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                if (nSuspended >= 0)
                    ResumeOtherThreads(suspended, nSuspended);
                NoteVerifyFail("migrate: exception during block copy or "
                               "pointer swap");
                return false;
            }
        }

        void RevertPatchesAfterHookFailure()
        {
            static HANDLE suspended[kMaxSuspendedThreads];
            if (g_GrownBlock && g_MigratedSlot
                && *g_MigratedSlot == g_GrownBlock + 0x20)
            {
                // the engine already runs on the migrated block, so it goes back to the old one together with the code
                const int nSuspended = SuspendOtherThreads(suspended);
                RevertAllPatches();
                *g_MigratedSlot = g_MigratedOldBase20;
                g_GrownBlock    = 0;
                ResumeOtherThreads(suspended, nSuspended);
                return;
            }
            if (g_GrownBlock || EquipDevelop_ResolveDevelopController())
            {
                g_Active = false;
                g_ArmedBase20.store(0, std::memory_order_relaxed);
                LogError("DevelopArrayGrow: develop block already built on the grown layout, "
                         "patches kept so it stays valid");
                return;
            }
            RevertAllPatches();
        }
    }

    bool DevelopArrayGrowActive()
    {
        return g_Active;
    }

    bool DevelopArrayGrowPending()
    {
        return g_RetryThreadRunning.load();
    }


    void SilenceDevelopRowAnnounce(std::int32_t developId)
    {
        if (developId <= 0)
            return;
        {
            std::lock_guard<std::mutex> lock(g_SilencedMutex);
            for (std::int32_t known : g_SilencedDevelopIds)
            {
                if (known == developId)
                    return;
            }
            g_SilencedDevelopIds.push_back(developId);
        }
        if (!g_Active)
            return;
        const std::uint32_t row = AnnouncedRowOf(developId, -1);
        if (row == 0)
            return;
        g_FlagsShadow[row] |= kReqAnnouncedBit;
        MirrorFlagByte(row);
    }

    void AssertDevelopRowAnnounced(std::int32_t developId, std::int32_t flowIndex)
    {
        if (!g_Active || developId <= 0)
            return;

        const std::uint32_t row = AnnouncedRowOf(developId, flowIndex);
        if (row == 0)
            return;

        const bool persisted =
            V_FrameWorkState::GetDevReqAnnouncedByDevelopId(developId);
        const bool initial = EquipDevelop_IsDevelopInitiallyAvailable(
            static_cast<std::uint32_t>(developId));

        if (!persisted && !initial)
        {
            g_FlagsShadow[row] &= static_cast<std::uint8_t>(~kReqAnnouncedBit);
            MirrorFlagByte(row);
            return;
        }

        g_FlagsShadow[row] |= kReqAnnouncedBit;
        MirrorFlagByte(row);
        if (!persisted)
            V_FrameWorkState::SetDevReqAnnouncedByDevelopId(developId, true);
    }

    bool OwnsDevelopVisibilityHook(const void* target)
    {
        return target && g_A && g_IsVisibleInstalled
            && ResolveGameAddress(kAddr_IsVisible) == target;
    }

    void InvalidateDevelopVisibilityCache()
    {
        g_VisValid = false;
        g_VisSelf  = nullptr;
    }

    void ArmSortiePrepDevelopList(std::uint8_t openTabRows)
    {
        t_SortiePrepListArmed = true;
        t_SortiePrepArmedTab  = openTabRows;
    }

    void DisarmSortiePrepDevelopList()
    {
        t_SortiePrepListArmed = false;
        t_SortiePrepArmedTab  = 0;
        t_SortiePrepRowsFirst = false;
        t_SortiePrepFillTab   = 0;
    }

    void SetCustomBluePrintRow(std::uint32_t row, std::uint16_t developId)
    {
        if (row >= kNewRows)
            return;
        g_CustomBluePrintDevelopIds[row] = developId;
        InvalidateDevelopVisibilityCache();
    }

    void InvalidateDevelopLookupIndex()
    {
        g_LookupValid = false;
    }

    void BeginDevelopVisibilityCache()
    {
        if (!g_IsVisibleInstalled)
            return;
        g_VisValid  = false;
        g_VisSelf   = nullptr;
        g_VisThread = GetCurrentThreadId();
        g_VisArmed  = true;
    }

    void EndDevelopVisibilityCache()
    {
        if (!g_VisArmed)
            return;
        g_VisArmed = false;
    }

    DevelopVisibilityScope::DevelopVisibilityScope()
        : owned(false)
    {
        if (!g_IsVisibleInstalled || g_VisArmed)
            return;
        g_VisValid  = false;
        g_VisSelf   = nullptr;
        g_VisThread = GetCurrentThreadId();
        g_VisArmed  = true;
        owned       = true;
    }

    DevelopVisibilityScope::~DevelopVisibilityScope()
    {
        if (owned)
            g_VisArmed = false;
    }

    std::uint32_t NativeFlowBound()
    {
        return g_Active ? kNewRows : kOldRows;
    }

    bool GunsmithFlowRowClaimed(std::uint32_t row)
    {
        if (row < kGunsmithFlowFirst || row > kGunsmithFlowLast)
            return false;
        return ((g_GunsmithClaimMask.load(std::memory_order_relaxed)
                 >> (row - kGunsmithFlowFirst)) & 1u) != 0;
    }

    std::size_t DevFlagsPtrOffsetBase20()
    {
        return (g_Active ? kNewCtlOff : kOldCtlOff) - 0x20;
    }

    void DevFlagsWriteByte(void* controller, std::uint32_t index,
                           std::uint8_t value)
    {
        if (!controller || index >= NativeFlowBound())
            return;
        __try
        {
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller)
                + DevFlagsPtrOffsetBase20());
            if (!bits)
                return;
            if (index >= kOldRows && bits != g_FlagsShadow)
                return;
            bits[index] = value;
            InvalidateDevelopVisibilityCache();
            if (bits == g_FlagsShadow)
                MirrorFlagByte(index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint32_t FirstCustomFlowIndex()
    {
        return FirstCustomRow();
    }

    std::uint32_t TrueVanillaRowCount()
    {
        return kTrueVanillaRows;
    }

    bool IsDevelopLayoutSettled()
    {
        return g_LayoutSettled.load(std::memory_order_acquire);
    }

    void SettleDevelopLayout(std::uint32_t firstCustomRow, std::uint32_t clearLoadedRowsBegin,
                             std::uint32_t clearLoadedRowsEnd)
    {
        if (firstCustomRow < kTrueVanillaRows)
            firstCustomRow = kTrueVanillaRows;
        if (clearLoadedRowsBegin < kTrueVanillaRows)
            clearLoadedRowsBegin = kTrueVanillaRows;
        if (clearLoadedRowsEnd > firstCustomRow)
            clearLoadedRowsEnd = firstCustomRow;
        if (clearLoadedRowsEnd > kOldRows)
            clearLoadedRowsEnd = kOldRows;
        if (clearLoadedRowsBegin >= clearLoadedRowsEnd)
            clearLoadedRowsBegin = clearLoadedRowsEnd = 0;
        if (clearLoadedRowsEnd > clearLoadedRowsBegin && g_Active && g_SvarsFlags)
        {
            __try
            {
                for (std::uint32_t i = clearLoadedRowsBegin; i < clearLoadedRowsEnd; ++i)
                {
                    g_FlagsShadow[i] = 0;
                    g_SvarsFlags[i]  = 0;
                    g_SyncMirror[i]  = 0;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        g_ClearLoadedRowsBegin.store(clearLoadedRowsBegin, std::memory_order_relaxed);
        g_ClearLoadedRowsEnd.store(clearLoadedRowsEnd, std::memory_order_relaxed);
        g_FirstCustomRow.store(firstCustomRow, std::memory_order_release);
        g_LayoutSettled.store(true, std::memory_order_release);
        InvalidateDevelopLookupIndex();
        InvalidateDevelopVisibilityCache();
    }

    void UseSaveSlotHooksForDevelopLayout()
    {
        g_RejectLoadedRows.store(false, std::memory_order_relaxed);
        g_LayoutPersistPending.store(false, std::memory_order_relaxed);
    }

    void ClearDevelopLayoutRowsNow()
    {
        const std::uint32_t begin = g_ClearLoadedRowsBegin.load(std::memory_order_relaxed);
        const std::uint32_t end = g_ClearLoadedRowsEnd.load(std::memory_order_relaxed);
        if (end <= begin || !g_Active || !g_SvarsFlags)
            return;
        __try
        {
            for (std::uint32_t i = begin; i < end; ++i)
            {
                g_FlagsShadow[i] = 0;
                g_SvarsFlags[i]  = 0;
                g_SyncMirror[i]  = 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        InvalidateDevelopVisibilityCache();
    }

    void PersistDevelopLayoutAfterSaveLoad(std::int32_t firstCustomRow, std::int32_t nativeRowsHash)
    {
        g_PendingFirstCustomRow.store(firstCustomRow, std::memory_order_relaxed);
        g_PendingNativeRowsHash.store(nativeRowsHash, std::memory_order_relaxed);
        g_LayoutPersistPending.store(true, std::memory_order_release);
    }

    bool TryReadRowDevelopId(std::uint32_t index, std::uint32_t& out)
    {
        if (index >= NativeFlowBound())
            return false;
        const std::uintptr_t base20 =
            g_ArmedBase20.load(std::memory_order_relaxed);
        if (!base20)
            return false;
        std::uint16_t dev = 0;
        if (!RecordFieldAt(base20, static_cast<std::uint16_t>(index), false, dev))
            return false;
        out = dev;
        return true;
    }

    bool TryReadDevelopRow(std::uint32_t index, DevelopRowInfo& out)
    {
        if (index >= NativeFlowBound() || IsReservedFlowRow(index))
            return false;
        const std::uintptr_t base20 =
            g_ArmedBase20.load(std::memory_order_relaxed);
        if (!base20)
            return false;
        __try
        {
            const auto* rec = reinterpret_cast<const std::uint8_t*>(
                base20 + static_cast<std::size_t>(index) * kRecStride);
            const std::uint16_t equipWord =
                *reinterpret_cast<const std::uint16_t*>(rec + kRecEquipIdOff);
            out.developId = *reinterpret_cast<const std::uint16_t*>(rec + kRecDevelopIdOff);
            out.parentDevelopId = *reinterpret_cast<const std::uint16_t*>(rec + 0xA);
            out.equipId = static_cast<std::uint16_t>(equipWord >> 4);
            out.grade = static_cast<std::uint8_t>(equipWord & 0xF);
            out.developType = rec[kBase20_RecDevelopTypeOff];
            out.listed = RowQualifiesForList(base20, index);
            return out.developId != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool DevFlagsTryReadByte(void* controller, std::uint32_t index,
                             std::uint8_t& out)
    {
        if (!controller || index >= NativeFlowBound())
            return false;
        __try
        {
            std::uint8_t* bits = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(controller)
                + DevFlagsPtrOffsetBase20());
            if (!bits)
                return false;
            if (index >= kOldRows && bits != g_FlagsShadow)
                return false;
            out = bits[index];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void EnsureDevelopBlockArmed(void* base20)
    {
        if (!g_Active || !base20)
            return;
        if (g_ArmedBase20.load(std::memory_order_relaxed)
            != reinterpret_cast<std::uintptr_t>(base20))
            ArmFromBase20(reinterpret_cast<std::uintptr_t>(base20));
        SyncFlagsWithSvars(false);
    }

    void SyncDevelopFlagsWithSave()
    {
        SyncFlagsWithSvars(false);
    }

    namespace
    {
        std::uintptr_t LiveDevelopBase20()
        {
            if (!g_A)
                return 0;
            const GetQuark_t getQuark = reinterpret_cast<GetQuark_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            const std::uintptr_t vt20A = reinterpret_cast<std::uintptr_t>(
                ResolveGameAddress(kAddr_Base20VtblA));
            const std::uintptr_t vt20B = reinterpret_cast<std::uintptr_t>(
                ResolveGameAddress(kAddr_Base20VtblB));
            if (!getQuark || (!vt20A && !vt20B))
                return 0;
            __try
            {
                const std::uint8_t* quark = getQuark();
                if (!quark)
                    return 0;
                const std::uintptr_t app =
                    *reinterpret_cast<const std::uintptr_t*>(quark + 0x98);
                if (!app)
                    return 0;
                const std::uintptr_t f110 =
                    *reinterpret_cast<const std::uintptr_t*>(app + 0x110);
                if (!f110)
                    return 0;
                const std::uintptr_t base20 =
                    *reinterpret_cast<const std::uintptr_t*>(f110 + 0xAC8);
                if (!base20)
                    return 0;
                const std::uintptr_t vt20 =
                    *reinterpret_cast<const std::uintptr_t*>(base20);
                return vt20 == vt20A || vt20 == vt20B ? base20 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        std::uint32_t LiveScanBound(std::uintptr_t base20)
        {
            const std::uint32_t bound = NativeFlowBound();
            if (bound > kOldRows && base20 != g_ArmedBase20.load(std::memory_order_relaxed))
                return kOldRows;
            return bound;
        }
    }

    bool ScanCustomOnlyDevelopIds(std::uint64_t* bits, std::size_t words)
    {
        if (!bits || words < kDevIdSlots / 64)
            return false;
        std::memset(bits, 0, kDevIdSlots / 8);
        const std::uintptr_t base20 = LiveDevelopBase20();
        if (!base20)
            return false;
        const std::uint32_t bound = LiveScanBound(base20);
        __try
        {
            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = kTrueVanillaRows; i < bound; ++i)
            {
                if (i == kFlowSentinel)
                    continue;
                const std::uint16_t dev = *reinterpret_cast<const std::uint16_t*>(
                    base + static_cast<std::size_t>(i) * kRecStride + kBase20_RecFieldOff);
                if (dev != 0)
                    bits[dev >> 6] |= 1ull << (dev & 63);
            }
            for (std::uint32_t i = 0; i < kTrueVanillaRows; ++i)
            {
                const std::uint16_t dev = *reinterpret_cast<const std::uint16_t*>(
                    base + static_cast<std::size_t>(i) * kRecStride + kBase20_RecFieldOff);
                bits[dev >> 6] &= ~(1ull << (dev & 63));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::memset(bits, 0, kDevIdSlots / 8);
            return false;
        }
    }

    bool CountDevelopedRowsByType(std::uint32_t* vanillaCounts, std::uint32_t* customCounts,
                                  std::size_t slots)
    {
        if (!vanillaCounts || !customCounts || slots < kDevelopTypeSlots)
            return false;
        const std::size_t countBytes = kDevelopTypeSlots * sizeof(std::uint32_t);
        std::memset(vanillaCounts, 0, countBytes);
        std::memset(customCounts, 0, countBytes);
        const std::uintptr_t base20 = LiveDevelopBase20();
        if (!base20)
            return false;
        const IsVisible_t isVisible = reinterpret_cast<IsVisible_t>(
            ResolveGameAddress(kAddr_IsVisible));
        if (!isVisible)
            return false;
        const std::uint32_t bound = LiveScanBound(base20);
        void* self = reinterpret_cast<void*>(base20);
        __try
        {
            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = 0; i < bound; ++i)
            {
                if (i == kFlowSentinel)
                    continue;
                const std::uint8_t* rec = base + static_cast<std::size_t>(i) * kRecStride;
                if (*reinterpret_cast<const std::uint16_t*>(rec + kBase20_RecFieldOff) == 0)
                    continue;
                std::uint8_t flags = 0;
                if (!DevFlagsTryReadByte(self, i, flags) || (flags & 1u) == 0)
                    continue;
                if (!isVisible(self, static_cast<std::uint16_t>(i)))
                    continue;
                std::uint32_t* counts = i < kTrueVanillaRows ? vanillaCounts : customCounts;
                ++counts[rec[kBase20_RecDevelopTypeOff]];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            std::memset(vanillaCounts, 0, countBytes);
            std::memset(customCounts, 0, countBytes);
            return false;
        }
    }

    std::size_t ScanVanillaDevelopEquipIds(std::uint64_t* bits, std::size_t words)
    {
        if (!bits || words < kEquipIdSlots / 64 || !IsDevelopLayoutSettled())
            return 0;
        const std::uintptr_t base20 = LiveDevelopBase20();
        if (!base20)
            return 0;
        const std::uint32_t first = FirstCustomRow();

        std::uint64_t local[kEquipIdSlots / 64] = {};
        std::size_t found = 0;
        __try
        {
            const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(base20);
            for (std::uint32_t i = 0; i < first; ++i)
            {
                const std::uint8_t* rec =
                    base + static_cast<std::size_t>(i) * kRecStride;
                const std::uint16_t dev = *reinterpret_cast<const std::uint16_t*>(
                    rec + kBase20_RecFieldOff);
                if (dev == 0)
                    return 0;
                const std::uint16_t eqp = static_cast<std::uint16_t>(
                    *reinterpret_cast<const std::uint16_t*>(rec + 0xC) >> 4);
                if (eqp == 0)
                    continue;
                const std::uint64_t bit = 1ull << (eqp & 63);
                if ((local[eqp >> 6] & bit) == 0)
                {
                    local[eqp >> 6] |= bit;
                    ++found;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        if (found < 64)
            return 0;
        std::memcpy(bits, local, sizeof(local));
        return found;
    }

    void PreApplyDevelopArrayGrowPatches()
    {
        if (g_PrePatch != PrePatchState::NotAttempted)
            return;
        if (!SelectBuildTables())
        {
            g_PrePatch = PrePatchState::WrongBuild;
            return;
        }
        if (GrowDisabledByMarker())
        {
            g_PrePatch = PrePatchState::DisabledByMarker;
            return;
        }
        for (std::size_t i = 0; i < kPatchCount; ++i)
            if (kPatches[i].addr && !VerifyPatchSite(kPatches[i]))
            {
                g_PrePatch       = PrePatchState::VerifyFailed;
                g_PrePatchDetail = static_cast<int>(i);
                return;
            }
        {
            std::size_t ctlFailed[16];
            std::size_t nCtlFailed   = 0;
            std::size_t ctlFirstFail = 0;
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
                if (!VerifyCtlSiteAt(i, kCtlDispSites[i].addr, true))
                {
                    if (nCtlFailed == 0)
                        ctlFirstFail = i;
                    if (nCtlFailed < 16)
                        ctlFailed[nCtlFailed] = i;
                    ++nCtlFailed;
                }
            if (nCtlFailed > 16)
            {
                VerifyCtlSite(ctlFirstFail);
                g_PrePatch       = PrePatchState::RetryPending;
                g_PrePatchDetail = 1000 + static_cast<int>(ctlFirstFail);
                return;
            }
            if (nCtlFailed > 0
                && !RelocateCloneCtlSites(ctlFailed, nCtlFailed))
            {
                g_PrePatch       = PrePatchState::RetryPending;
                g_PrePatchDetail = 1000 + static_cast<int>(ctlFirstFail);
                return;
            }
        }
        for (std::size_t i = 0; i < kBoundSiteCount; ++i)
            if (!VerifyBoundSite(kBoundImmSites[i]))
            {
                g_PrePatch       = PrePatchState::VerifyFailed;
                g_PrePatchDetail = 2000 + static_cast<int>(i);
                return;
            }
        for (std::size_t i = 0; i < kByteFixCount; ++i)
            if (!VerifyByteFix(kByteFixes[i]))
            {
                g_PrePatch       = PrePatchState::VerifyFailed;
                g_PrePatchDetail = 4000 + static_cast<int>(i);
                return;
            }

        if (!SweepImageForUnknownSites())
        {
            g_PrePatch       = PrePatchState::VerifyFailed;
            g_PrePatchDetail = 3000;
            return;
        }

        if (EquipDevelop_ResolveDevelopController())
        {
            if (!MigrateExistingBlock())
            {
                g_PrePatch = PrePatchState::BlockExists;
                return;
            }
            g_PrePatch = PrePatchState::Applied;
            return;
        }

        const int applied = ApplyAllPatches();
        if (applied != TotalPatchCount())
        {
            RevertAllPatches();
            g_PrePatch       = PrePatchState::ApplyFailed;
            g_PrePatchDetail = applied;
            return;
        }

        if (EquipDevelop_ResolveDevelopController())
        {
            RevertAllPatches();
            g_PrePatch = PrePatchState::RacedMidPatch;
            return;
        }

        g_PrePatch = PrePatchState::Applied;
    }

    namespace
    {
        bool LogGrowInstallGate()
        {
        switch (g_PrePatch)
        {
        case PrePatchState::Applied:
            return true;
        case PrePatchState::WrongBuild:
            LogError("DevelopArrayGrow: no tables for this game build, develop rows "
                     "stay 1024");
            return false;
        case PrePatchState::DisabledByMarker:
            LogWarn("DevelopArrayGrow: disabled by V_FrameWork_no_develop_grow.txt, "
                    "develop rows stay 1024");
            return false;
        case PrePatchState::BlockExists:
            LogError("DevelopArrayGrow: develop block could not be migrated (%s), "
                     "develop rows stay 1024",
                     g_PreVerifyDetail[0] ? g_PreVerifyDetail : "no detail");
            return false;
        case PrePatchState::VerifyFailed:
            LogError("DevelopArrayGrow: patch site %d failed verification (%s), "
                     "develop rows stay 1024", g_PrePatchDetail,
                     g_PreVerifyDetail[0] ? g_PreVerifyDetail : "no detail");
            return false;
        case PrePatchState::ApplyFailed:
            LogError("DevelopArrayGrow: only %d patch sites applied, reverted; "
                     "develop rows stay 1024", g_PrePatchDetail);
            return false;
        case PrePatchState::RacedMidPatch:
            LogError("DevelopArrayGrow: develop block built mid-patch, reverted; "
                     "develop rows stay 1024");
            return false;
        default:
            return false;
        }
        }

        bool InstallGrowHooksAndFinalize()
        {
        const bool hooks =
            CreateAndEnableHook(ResolveGameAddress(kAddr_BlockReset),
                                reinterpret_cast<void*>(&hkBlockReset),
                                reinterpret_cast<void**>(&g_OrigBlockReset))
            && (g_HookBlockReset = ResolveGameAddress(kAddr_BlockReset)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_SetDeveloped),
                                   reinterpret_cast<void*>(&hkSetDeveloped),
                                   reinterpret_cast<void**>(&g_OrigSetDeveloped))
            && (g_HookSetDeveloped = ResolveGameAddress(kAddr_SetDeveloped)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_SetUndeveloped),
                                   reinterpret_cast<void*>(&hkSetUndeveloped),
                                   reinterpret_cast<void**>(&g_OrigSetUndeveloped))
            && (g_HookSetUndeveloped = ResolveGameAddress(kAddr_SetUndeveloped)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_FindByDevId),
                                   reinterpret_cast<void*>(&hkFindByDevId),
                                   reinterpret_cast<void**>(&g_OrigFindByDevId))
            && (g_HookFindByDevId = ResolveGameAddress(kAddr_FindByDevId)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_FindByEquipId),
                                   reinterpret_cast<void*>(&hkFindByEquipId),
                                   reinterpret_cast<void**>(&g_OrigFindByEquipId))
            && (g_HookFindByEquipId = ResolveGameAddress(kAddr_FindByEquipId)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_ListDevCount),
                                   reinterpret_cast<void*>(&hkListDevCount),
                                   reinterpret_cast<void**>(&g_OrigListDevCount))
            && (g_HookListDevCount = ResolveGameAddress(kAddr_ListDevCount)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_ListDevFill),
                                   reinterpret_cast<void*>(&hkListDevFill),
                                   reinterpret_cast<void**>(&g_OrigListDevFill))
            && (g_HookListDevFill = ResolveGameAddress(kAddr_ListDevFill)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_EquipIdCount),
                                   reinterpret_cast<void*>(&hkEquipIdCount),
                                   reinterpret_cast<void**>(&g_OrigEquipIdCount))
            && (g_HookEquipIdCount = ResolveGameAddress(kAddr_EquipIdCount)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_EquipIdRow),
                                   reinterpret_cast<void*>(&hkEquipIdRow),
                                   reinterpret_cast<void**>(&g_OrigEquipIdRow))
            && (g_HookEquipIdRow = ResolveGameAddress(kAddr_EquipIdRow)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_GetDerivationDevelopIndex),
                                   reinterpret_cast<void*>(&hkGetDerivationDevelopIndex),
                                   reinterpret_cast<void**>(&g_OrigGetDerivationDevelopIndex))
            && (g_HookGetDerivationDevelopIndex =
                    ResolveGameAddress(kAddr_GetDerivationDevelopIndex)) != nullptr
            && CreateAndEnableHook(ResolveGameAddress(kAddr_GetBaseId),
                                   reinterpret_cast<void*>(&hkGetBaseId),
                                   reinterpret_cast<void**>(&g_OrigGetBaseId))
            && (g_HookGetBaseId = ResolveGameAddress(kAddr_GetBaseId)) != nullptr;
        if (!hooks)
        {
            LogError("DevelopArrayGrow: hook install failed, develop rows stay 1024");
            Uninstall_DevelopArrayGrow();
            RevertPatchesAfterHookFailure();
            return false;
        }

        g_ClampInstalled = CreateAndEnableHook(
            ResolveGameAddress(kAddr_SiblingCount),
            reinterpret_cast<void*>(&hkSiblingCount),
            reinterpret_cast<void**>(&g_OrigSiblingCount));
        if (!g_ClampInstalled)
            LogError("DevelopArrayGrow: sibling-count clamp hook failed, a large "
                     "develop group can overrun the menu stack");

        g_IsVisibleInstalled = CreateAndEnableHook(
            ResolveGameAddress(kAddr_IsVisible),
            reinterpret_cast<void*>(&hkIsVisible),
            reinterpret_cast<void**>(&g_OrigIsVisible));
        if (!g_IsVisibleInstalled)
            LogError("DevelopArrayGrow: row-visibility cache hook failed, weapon "
                     "lists build slowly");

        g_AllocGuardInstalled = CreateAndEnableHook(
            ResolveGameAddress(kAddr_QuarkBlockHeapAlloc),
            reinterpret_cast<void*>(&hkQuarkBlockAlloc),
            reinterpret_cast<void**>(&g_OrigQuarkAlloc));
        if (g_AllocGuardInstalled)
            g_HookQuarkAlloc = ResolveGameAddress(kAddr_QuarkBlockHeapAlloc);
        else
            LogError("DevelopArrayGrow: block-alloc guard hook failed, a fresh "
                     "develop block keeps a garbage tail");

        if (kAddr_EdcDtor != 0)
        {
            g_DtorGuardInstalled = CreateAndEnableHook(
                ResolveGameAddress(kAddr_EdcDtor),
                reinterpret_cast<void*>(&hkEdcDtorGuard),
                reinterpret_cast<void**>(&g_OrigEdcDtor));
            if (g_DtorGuardInstalled)
                g_HookEdcDtor = ResolveGameAddress(kAddr_EdcDtor);
            else
                LogError("DevelopArrayGrow: dtor guard hook failed, a teardown fault "
                         "crashes the game at quit");
        }

        g_Active = true;

        if (!g_IntegrityThreadStarted.exchange(true))
        {
            HANDLE h = CreateThread(nullptr, 0, &GrowIntegrityThread,
                                    nullptr, 0, nullptr);
            if (h)
                CloseHandle(h);
            else
            {
                g_IntegrityThreadStarted.store(false);
                LogError("DevelopArrayGrow: integrity watchdog failed to start, "
                         "protector restores go undetected");
            }
        }

        {
            int workBufSites = 0;
            int sunk = 0;
            for (std::size_t i = 0; i < kCtlSiteCount; ++i)
            {
                if (kCtlDispSites[i].oldDisp != kWorkBufDisp)
                    continue;
                ++workBufSites;
                if (IsWorkBufFreeLoad(kCtlDispSites[i].addr + g_CtlDispOff[i],
                                      kCtlDispSites[i].oldDisp))
                    ++sunk;
            }
            if (workBufSites != 0 && sunk == 0)
                LogWarn("DevelopArrayGrow: no work-buffer free-load site matched "
                        "(%d sites), heap may corrupt at exit", workBufSites);
        }
        return true;
        }

        DWORD WINAPI GrowRetryThread(LPVOID)
        {
            for (int i = 0; i < 60; ++i)
            {
                Sleep(250);
                g_PrePatch = PrePatchState::NotAttempted;
                PreApplyDevelopArrayGrowPatches();
                if (g_PrePatch != PrePatchState::RetryPending)
                    break;
            }
            if (g_PrePatch == PrePatchState::RetryPending)
            {
                g_PrePatch = PrePatchState::VerifyFailed;
                LogError("DevelopArrayGrow: patch site %d unverified after 15s (%s), "
                         "develop rows stay 1024", g_PrePatchDetail,
                         g_PreVerifyDetail[0] ? g_PreVerifyDetail : "no detail");
                g_RetryThreadRunning.store(false);
                EquipDevelop_InstallDevelopSyncHooks();
                return 0;
            }
            if (LogGrowInstallGate())
                InstallGrowHooksAndFinalize();
            g_RetryThreadRunning.store(false);
            EquipDevelop_InstallDevelopSyncHooks();   // the module install skipped it while the retry was pending
            return 0;
        }
    }

    bool Install_DevelopArrayGrow()
    {
        if (g_Active)
            return true;

        if (g_PrePatch == PrePatchState::NotAttempted)
            PreApplyDevelopArrayGrowPatches();

        if (g_PrePatch == PrePatchState::RetryPending)
        {
            g_RetryThreadRunning.store(true);
            HANDLE h = CreateThread(nullptr, 0, &GrowRetryThread, nullptr,
                                    0, nullptr);
            if (h)
            {
                CloseHandle(h);
                return true;
            }
            g_RetryThreadRunning.store(false);
            g_PrePatch = PrePatchState::VerifyFailed;
            LogError("DevelopArrayGrow: retry thread failed to start, develop rows "
                     "stay 1024");
            return false;
        }

        if (!LogGrowInstallGate())
            return false;
        return InstallGrowHooksAndFinalize();
    }

    void Uninstall_DevelopArrayGrow()
    {
        if (g_AllocGuardInstalled)
        {
            DisableAndRemoveHook(g_HookQuarkAlloc
                                     ? g_HookQuarkAlloc
                                     : ResolveGameAddress(
                                           kAddr_QuarkBlockHeapAlloc));
            g_AllocGuardInstalled = false;
            g_OrigQuarkAlloc     = nullptr;
            g_HookQuarkAlloc     = nullptr;
        }
        if (g_IsVisibleInstalled)
        {
            DisableAndRemoveHook(ResolveGameAddress(kAddr_IsVisible));
            g_IsVisibleInstalled = false;
            g_VisArmed           = false;
            g_OrigIsVisible      = nullptr;
        }
        if (g_ClampInstalled)
        {
            DisableAndRemoveHook(ResolveGameAddress(kAddr_SiblingCount));
            g_ClampInstalled   = false;
            g_OrigSiblingCount = nullptr;
        }
        if (g_HookEdcDtor)
        {
            DisableAndRemoveHook(g_HookEdcDtor);
            g_HookEdcDtor        = nullptr;
            g_OrigEdcDtor        = nullptr;
            g_DtorGuardInstalled = false;
        }
        if (g_HookGetDerivationDevelopIndex)
            DisableAndRemoveHook(g_HookGetDerivationDevelopIndex);
        if (g_HookGetBaseId)      DisableAndRemoveHook(g_HookGetBaseId);
        if (g_HookEquipIdRow)     DisableAndRemoveHook(g_HookEquipIdRow);
        if (g_HookEquipIdCount)   DisableAndRemoveHook(g_HookEquipIdCount);
        if (g_HookListDevFill)    DisableAndRemoveHook(g_HookListDevFill);
        if (g_HookListDevCount)   DisableAndRemoveHook(g_HookListDevCount);
        if (g_HookFindByEquipId)  DisableAndRemoveHook(g_HookFindByEquipId);
        if (g_HookFindByDevId)    DisableAndRemoveHook(g_HookFindByDevId);
        if (g_HookSetUndeveloped) DisableAndRemoveHook(g_HookSetUndeveloped);
        if (g_HookSetDeveloped)   DisableAndRemoveHook(g_HookSetDeveloped);
        if (g_HookBlockReset)     DisableAndRemoveHook(g_HookBlockReset);
        g_HookGetBaseId = g_HookGetDerivationDevelopIndex = nullptr;
        g_HookEquipIdRow = g_HookEquipIdCount = nullptr;
        g_HookListDevFill = g_HookListDevCount = nullptr;
        g_HookFindByEquipId = g_HookFindByDevId = nullptr;
        g_HookSetUndeveloped = g_HookSetDeveloped = g_HookBlockReset = nullptr;
    }
}
