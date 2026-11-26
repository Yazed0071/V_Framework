#include <pch.h>
#include "MotherBaseDeviceTopMenuTableImpl_CustomItems.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "UixUtilityControl.h"
#include "UixUtilityDev.h"
#include "log.h"

namespace mbTopMenuCustomItems
{
    namespace
    {
        using TableFactory_t   = void*(__fastcall*)();
        using BoolFn_t         = bool(__fastcall*)();
        using GetInstance_t    = void*(__fastcall*)();
        using GetCommonData_t  = void*(__fastcall*)(void* manager);
        using ByteGetter_t     = std::uint8_t(__fastcall*)(void* self, std::uint8_t id);
        using FlagSetter_t     = void(__fastcall*)(void* self, std::uint8_t id, bool flag);
        using ByteSetter_t     = void(__fastcall*)(void* self, std::uint8_t id, std::uint8_t value);
        using CountSetter_t    = void(__fastcall*)(void* self, std::uint8_t id, std::uint32_t count);
        using IdSetter_t       = void(__fastcall*)(void* self, std::uint8_t id);
        using DwordGetter_t    = std::uint32_t(__fastcall*)(void* self, std::uint8_t id);
        using QwordGetter_t    = std::uint64_t(__fastcall*)(void* self, std::uint8_t id);
        using LangIdGetter_t   = std::uint64_t*(__fastcall*)(void* self, std::uint64_t* out, std::uint8_t id);   // returns out: callers read the id through rax
        using TextGetter_t     = const char*(__fastcall*)(void* self, std::uint8_t id);
        using Enumerator_t     = std::uint32_t(__fastcall*)(void* self, std::uint8_t parent, std::uint8_t* out, std::uint32_t cap);
        using LabelToIndex_t   = std::uint8_t(__fastcall*)(void* self, const char* label);
        using LangText_t       = const char*(__fastcall*)(void** utility, std::uint64_t langId);
        using ItemIcon_t       = void(__fastcall*)(std::uint8_t id, float* u, float* v, std::uint64_t* texture);

        constexpr int Slot(std::size_t byteOffset) { return static_cast<int>(byteOffset / sizeof(void*)); }

        constexpr int kSlotCount           = 49;
        constexpr int kSlotIsVisible       = Slot(0x08);
        constexpr int kSlotIsEnabled       = Slot(0x10);
        constexpr int kSlotClosesIDroid    = Slot(0x18);
        constexpr int kSlotStaysInSelector = Slot(0x20);
        constexpr int kSlotGetParent       = Slot(0x28);
        constexpr int kSlotGetRemembered   = Slot(0x30);
        constexpr int kSlotGetNoticeBits   = Slot(0x38);
        constexpr int kSlotGetNoticeCount  = Slot(0x40);
        constexpr int kSlotGetPressCount   = Slot(0x50);
        constexpr int kSlotGetActId        = Slot(0x58);
        constexpr int kSlotGetTitleLangId  = Slot(0x60);
        constexpr int kSlotGetTitleText    = Slot(0x68);
        constexpr int kSlotGetHelpLangId   = Slot(0x70);
        constexpr int kSlotGetHelpText     = Slot(0x78);
        constexpr int kSlotGetTrigger      = Slot(0x80);
        constexpr int kSlotSetVisible      = Slot(0x90);
        constexpr int kSlotSetActive       = Slot(0x98);
        constexpr int kSlotSetLocked       = Slot(0xA0);
        constexpr int kSlotIncPressCount   = Slot(0xA8);
        constexpr int kSlotSetRemembered   = Slot(0xB0);
        constexpr int kSlotOrNoticeBits    = Slot(0xB8);
        constexpr int kSlotClearNoticeBits = Slot(0xC0);
        constexpr int kSlotSetNoticeCount  = Slot(0xE8);
        constexpr int kSlotFindVisible     = Slot(0xF8);
        constexpr int kSlotGetRootTab      = Slot(0x108);
        constexpr int kSlotLabelToIndex    = Slot(0x138);
        constexpr int kSlotMarkDirty       = Slot(0x128);   // asks the selector to rebuild its lists

        constexpr std::uint8_t  kNone               = 0x4F;       // the engine's "no entry" index
        constexpr std::uint8_t  kPageRootMotherBase = 0x01;
        constexpr std::uint8_t  kPageRootMission    = 0x21;
        constexpr std::uint8_t  kDefaultIcon        = 0x03;       // MBM_CUSTOM: a first-level item with no icon of its own
        constexpr int           kItemCount         = kLastItemId - kFirstItemId + 1;
        constexpr std::uint64_t kId48Mask           = 0xFFFFFFFFFFFFull;
        constexpr std::uint64_t kNoTrigger          = 0xB8A0BF169F98ull;   // the empty StringId: no trigger, no text
        constexpr std::uint64_t kBackTrigger        = 0x44CDE4609589ull;
        constexpr std::uint64_t kShowMenuTrigger    = 0xBCE1EF6ACF15ull;
        constexpr std::uint32_t kRootListCap        = 11;         // drawn slots at the first depth
        constexpr std::uint32_t kSubListCap         = 10;         // drawn slots at depth 2 and 3
        constexpr int           kRetiredTexts       = 64;

        const std::uint8_t kFactoryEntry[] = { 0x48, 0x83, 0xEC, 0x38, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF };
        const std::uint8_t kItemIconEntry[] = { 0x0F, 0xB6, 0xC1, 0x83, 0xC0, 0xFE, 0x83, 0xF8, 0x4B, 0x0F, 0x87, 0x05, 0x02, 0x00, 0x00, 0x4C, 0x8D, 0x15 };

        struct Item
        {
            bool          used = false;
            bool          group = false;
            bool          visible = true;
            bool          active = true;
            bool          locked = false;
            std::uint8_t  parent = 0;
            std::uint8_t  icon = 0;
            std::uint64_t iconTexture = 0;
            float         iconU = 0.0f;
            float         iconV = 0.0f;
            std::uint8_t  remembered = kNone;
            std::uint8_t  noticeBits = 0;
            std::uint8_t  noticeCount = 0;
            std::uint32_t pressCount = 0;
            std::uint32_t actId = 0;
            std::uint64_t identity = 0;
            std::uint64_t labelLang = 0;
            std::uint64_t descriptionLang = 0;
            std::shared_ptr<const std::string> label;
            std::shared_ptr<const std::string> description;
            std::string   name;
        };

        std::mutex                         g_ItemsLock;
        Item                               g_Items[kItemCount];
        std::shared_ptr<const std::string> g_Retired[kRetiredTexts];   // the engine may still hold a text pointer of a removed item for a frame
        int                                g_RetiredNext = 0;
        std::atomic<int>                   g_ItemsUsed{ 0 };

        std::mutex                         g_SwapLock;
        alignas(16) void*                  g_Clone[kSlotCount + 1] = {};   // [0] is vtable[-1], the vptr points at [1]
        void*                              g_Orig[kSlotCount] = {};
        void**                             g_SourceVtable = nullptr;
        void**                             g_ExeVtable = nullptr;
        bool                               g_CloneBuilt = false;
        std::atomic<void*>                 g_Table{ nullptr };
        std::atomic<bool>                  g_Ready{ false };

        TableFactory_t   g_OrigFactory = nullptr;
        void*            g_FactoryTarget = nullptr;
        ItemIcon_t       g_OrigGetItemIcon = nullptr;
        void*            g_ItemIconTarget = nullptr;
        BoolFn_t         g_IsFobMode = nullptr;
        GetInstance_t    g_GetCommonDataManager = nullptr;
        GetCommonData_t  g_GetCommonData = nullptr;

        bool IsVirtual(std::uint8_t id)
        {
            return id >= kFirstItemId && id <= kLastItemId;
        }

        Item& ItemOf(std::uint8_t id)
        {
            return g_Items[id - kFirstItemId];
        }

        template <typename Fn>
        Fn Original(int slot)
        {
            return reinterpret_cast<Fn>(g_Orig[slot]);
        }

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

        const char* LangTextSEH(std::uint64_t langId)
        {
            __try
            {
                void** utility = UixUtilityControl::Utility();
                void* fn = UixUtilityControl::Method(UixUtilityControl::dev::GetLangText);
                return utility && fn ? reinterpret_cast<LangText_t>(fn)(utility, langId) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        // what a child of this parent may be: -1 not a parent, else how deep the parent sits (page root = 0, so its children are at depth 1)
        int ParentLevelLocked(std::uint8_t parent, int hops = 0)
        {
            if (IsVirtual(parent))
            {
                const Item& item = ItemOf(parent);
                if (!item.used || !item.group || hops > 3)
                    return -1;
                const int above = ParentLevelLocked(item.parent, hops + 1);
                return above < 0 ? -1 : above + 1;
            }
            switch (parent)
            {
                case 0x01: case 0x21:
                    return 0;
                case 0x03: case 0x11: case 0x1A: case 0x1D: case 0x24: case 0x29: case 0x37: case 0x40:
                    return 1;
                case 0x07: case 0x3C:
                    return 2;
                default:
                    return -1;
            }
        }

        bool VisibleLocked(std::uint8_t id, int guard)
        {
            const Item& item = ItemOf(id);
            if (!item.used || !item.visible)
                return false;
            if (IsVirtual(item.parent))
            {
                const Item& up = ItemOf(item.parent);
                if (!up.used || !up.visible)
                    return false;
            }
            if (!item.group)
                return true;
            if (guard > 3)
                return false;
            for (int i = 0; i < kItemCount; ++i)
                if (g_Items[i].used && g_Items[i].parent == id && VisibleLocked(static_cast<std::uint8_t>(kFirstItemId + i), guard + 1))
                    return true;
            return false;
        }

        void RetireTextLocked(std::shared_ptr<const std::string>& text)
        {
            if (!text)
                return;
            g_Retired[g_RetiredNext] = std::move(text);
            g_RetiredNext = (g_RetiredNext + 1) % kRetiredTexts;
        }

        void ClearItemLocked(std::uint8_t id)
        {
            Item& item = ItemOf(id);
            RetireTextLocked(item.label);
            RetireTextLocked(item.description);
            item = Item();
            g_ItemsUsed.fetch_sub(1, std::memory_order_relaxed);
        }

        void RemoveLocked(std::uint8_t id)
        {
            for (int i = 0; i < kItemCount; ++i)
                if (g_Items[i].used && g_Items[i].parent == id)
                    RemoveLocked(static_cast<std::uint8_t>(kFirstItemId + i));
            ClearItemLocked(id);
        }

        // ---- the cloned vtable: readers ----

        std::uint32_t __fastcall hkIsVisible(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotIsVisible)(self, id);
            if (!ItemsAllowed())
                return 0;
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return VisibleLocked(id, 0);
        }

        std::uint32_t __fastcall hkIsEnabled(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotIsEnabled)(self, id);
            if (!ItemsAllowed())
                return 0;
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            const Item& item = ItemOf(id);
            return item.used && item.active && !item.locked;
        }

        std::uint32_t __fastcall hkClosesIDroid(void* self, std::uint8_t id)
        {
            return IsVirtual(id) ? 0 : Original<ByteGetter_t>(kSlotClosesIDroid)(self, id);
        }

        std::uint32_t __fastcall hkStaysInSelector(void* self, std::uint8_t id)
        {
            return IsVirtual(id) ? 0 : Original<ByteGetter_t>(kSlotStaysInSelector)(self, id);   // 0 makes the selector hide itself after our trigger
        }

        std::uint32_t __fastcall hkGetParent(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetParent)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            const Item& item = ItemOf(id);
            return item.used ? item.parent : kNone;
        }

        std::uint32_t __fastcall hkGetRemembered(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetRemembered)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return ItemOf(id).remembered;
        }

        std::uint32_t __fastcall hkGetNoticeBits(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetNoticeBits)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return ItemOf(id).noticeBits;
        }

        std::uint32_t __fastcall hkGetNoticeCount(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetNoticeCount)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return ItemOf(id).noticeCount;
        }

        std::uint32_t __fastcall hkGetPressCount(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetPressCount)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return ItemOf(id).pressCount & 0xFF;
        }

        std::uint32_t __fastcall hkGetActId(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<DwordGetter_t>(kSlotGetActId)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            return ItemOf(id).actId;
        }

        std::uint64_t* __fastcall hkGetTitleLangId(void* self, std::uint64_t* out, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<LangIdGetter_t>(kSlotGetTitleLangId)(self, out, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            const Item& item = ItemOf(id);
            *out = item.used && item.labelLang ? item.labelLang : kNoTrigger;
            return out;
        }

        std::uint64_t* __fastcall hkGetHelpLangId(void* self, std::uint64_t* out, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<LangIdGetter_t>(kSlotGetHelpLangId)(self, out, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            const Item& item = ItemOf(id);
            *out = item.used && item.descriptionLang ? item.descriptionLang : kNoTrigger;
            return out;
        }

        const char* TextOfItem(std::uint8_t id, bool title)
        {
            std::shared_ptr<const std::string> text;
            std::uint64_t langId = 0;
            {
                std::lock_guard<std::mutex> lock(g_ItemsLock);
                const Item& item = ItemOf(id);
                if (!item.used)
                    return "";
                text = title ? item.label : item.description;
                langId = title ? item.labelLang : item.descriptionLang;
            }
            if (langId)
            {
                const char* fromGame = LangTextSEH(langId);
                if (fromGame && *fromGame)
                    return fromGame;
            }
            return text ? text->c_str() : "";   // the item keeps its own copy, a removed item's copy lives on in the retired ring
        }

        const char* __fastcall hkGetTitleText(void* self, std::uint8_t id)
        {
            return IsVirtual(id) ? TextOfItem(id, true) : Original<TextGetter_t>(kSlotGetTitleText)(self, id);
        }

        const char* __fastcall hkGetHelpText(void* self, std::uint8_t id)
        {
            return IsVirtual(id) ? TextOfItem(id, false) : Original<TextGetter_t>(kSlotGetHelpText)(self, id);
        }

        std::uint64_t __fastcall hkGetTrigger(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<QwordGetter_t>(kSlotGetTrigger)(self, id);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            const Item& item = ItemOf(id);
            return item.used && !item.group ? item.identity : kNoTrigger;
        }

        std::uint32_t __fastcall hkGetRootTab(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
                return Original<ByteGetter_t>(kSlotGetRootTab)(self, id);
            std::uint8_t current = id;
            {
                std::lock_guard<std::mutex> lock(g_ItemsLock);
                for (int hop = 0; hop < 4 && IsVirtual(current); ++hop)
                {
                    const Item& item = ItemOf(current);
                    if (!item.used || item.icon || item.iconTexture)
                        return current;
                    if (item.parent == kPageRootMotherBase || item.parent == kPageRootMission)
                        return current;   // a virtual first-level entry is its own tab, like the vanilla ones
                    current = item.parent;
                }
            }
            return IsVirtual(current) ? id : Original<ByteGetter_t>(kSlotGetRootTab)(self, current);
        }

        // the first-level list hands the row id straight to this function, and its switch ends at 0x4D: an id above that leaves texture 0 and the row draws noise
        void __fastcall hkGetItemIcon(std::uint8_t id, float* u, float* v, std::uint64_t* texture)
        {
            if (IsVirtual(id))
            {
                std::lock_guard<std::mutex> lock(g_ItemsLock);
                const Item& item = ItemOf(id);
                if (item.iconTexture)   // the row sets this texture by path and shifts its cell by u/v (MbTopMenuSelectorEv_SetItemIcon)
                {
                    *u = item.iconU;
                    *v = item.iconV;
                    *texture = item.iconTexture;
                    return;
                }
                id = item.icon ? item.icon : kDefaultIcon;
            }
            g_OrigGetItemIcon(id, u, v, texture);
        }

        // ---- writers: never reach the vanilla code for a virtual id, that would write past the 0xC50 bytes of the object ----

        void __fastcall hkSetVisible(void* self, std::uint8_t id, bool flag)
        {
            if (!IsVirtual(id))
            {
                Original<FlagSetter_t>(kSlotSetVisible)(self, id, flag);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).visible = flag;
        }

        void __fastcall hkSetActive(void* self, std::uint8_t id, bool flag)
        {
            if (!IsVirtual(id))
            {
                Original<FlagSetter_t>(kSlotSetActive)(self, id, flag);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).active = flag;
        }

        void __fastcall hkSetLocked(void* self, std::uint8_t id, bool flag)
        {
            if (!IsVirtual(id))
            {
                Original<FlagSetter_t>(kSlotSetLocked)(self, id, flag);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).locked = flag;
        }

        void __fastcall hkIncPressCount(void* self, std::uint8_t id)
        {
            if (!IsVirtual(id))
            {
                Original<IdSetter_t>(kSlotIncPressCount)(self, id);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ++ItemOf(id).pressCount;
        }

        void __fastcall hkSetRemembered(void* self, std::uint8_t id, std::uint8_t child)
        {
            if (!IsVirtual(id))
            {
                Original<ByteSetter_t>(kSlotSetRemembered)(self, id, child);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).remembered = child;
        }

        void __fastcall hkOrNoticeBits(void* self, std::uint8_t id, std::uint8_t bits)
        {
            if (!IsVirtual(id))
            {
                Original<ByteSetter_t>(kSlotOrNoticeBits)(self, id, bits);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).noticeBits |= bits;
        }

        void __fastcall hkClearNoticeBits(void* self, std::uint8_t id, std::uint8_t bits)
        {
            if (!IsVirtual(id))
            {
                Original<ByteSetter_t>(kSlotClearNoticeBits)(self, id, bits);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).noticeBits &= static_cast<std::uint8_t>(~bits);
        }

        void __fastcall hkSetNoticeCount(void* self, std::uint8_t id, std::uint32_t count)
        {
            if (!IsVirtual(id))
            {
                Original<CountSetter_t>(kSlotSetNoticeCount)(self, id, count);
                return;
            }
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            ItemOf(id).noticeCount = static_cast<std::uint8_t>(std::min<std::uint32_t>(count, 99));   // the vanilla cap
        }

        // ---- the list the selector draws ----

#ifdef _DEBUG
        std::uint8_t ItemFlagsSEH(const void* self, std::uint8_t id)
        {
            __try
            {
                return static_cast<const std::uint8_t*>(self)[8 + static_cast<std::size_t>(id) * 0x28];
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0xFF;
            }
        }

        void ProbeBuddyList(void* self, std::uint8_t parent, const std::uint8_t* out, std::uint32_t count)   // TEMP probe: Customize > BUDDY rows
        {
            constexpr std::uint8_t kCustomizeBuddy = 7;
            if (parent != kCustomizeBuddy)
                return;
            char rows[64] = {};
            int at = 0;
            for (std::uint32_t i = 0; i < count && at < 56; ++i)
                at += std::snprintf(rows + at, sizeof(rows) - at, "%u ", out[i]);
            char states[256] = {};
            int st = 0;
            for (std::uint8_t id = 8; id <= 12 && st < 220; ++id)
                st += std::snprintf(states + st, sizeof(states) - st, "%u: visible %u active %u flags %02X | ", id,
                                    Original<ByteGetter_t>(kSlotIsVisible)(self, id), Original<ByteGetter_t>(kSlotIsEnabled)(self, id),
                                    ItemFlagsSEH(self, id));
            static char last[352] = {};
            char line[352] = {};
            std::snprintf(line, sizeof(line), "%s| %s", rows, states);
            if (std::strcmp(line, last) == 0)
                return;
            std::memcpy(last, line, sizeof(last));
            LogDebug("iDroid menu: Customize > BUDDY rows %s", line);
        }
#endif

        std::uint32_t __fastcall hkFindVisibleChildItemIds(void* self, std::uint8_t parent, std::uint8_t* out, std::uint32_t cap)
        {
            std::uint32_t count = Original<Enumerator_t>(kSlotFindVisible)(self, parent, out, cap);
#ifdef _DEBUG
            ProbeBuddyList(self, parent, out, count);
#endif
            if (g_ItemsUsed.load(std::memory_order_relaxed) == 0 || !ItemsAllowed())
                return count;
            const std::uint32_t limit = (parent == kPageRootMotherBase || parent == kPageRootMission) ? (std::min)(cap, kRootListCap)
                                                                                                    : (std::min)(cap, kSubListCap);
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            for (int i = 0; i < kItemCount && count < limit; ++i)
                if (g_Items[i].used && g_Items[i].parent == parent && VisibleLocked(static_cast<std::uint8_t>(kFirstItemId + i), 0))
                    out[count++] = static_cast<std::uint8_t>(kFirstItemId + i);
            return count;
        }

        // TppUiCommand.SetMbTopMenuItemVisible / Active / TutorialNotice find their entry here by the label text
        std::uint32_t __fastcall hkLabelToIndex(void* self, const char* label)
        {
            const std::uint32_t vanilla = Original<LabelToIndex_t>(kSlotLabelToIndex)(self, label);
            if (vanilla != kNone || !label || !*label || g_ItemsUsed.load(std::memory_order_relaxed) == 0)
                return vanilla;
            const std::uint32_t actId = static_cast<std::uint32_t>(FoxHashes::StrCode64(label));   // the engine compares the low dword of this hash
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            for (int i = 0; i < kItemCount; ++i)
                if (g_Items[i].used && g_Items[i].actId == actId)
                    return static_cast<std::uint32_t>(kFirstItemId + i);
            return vanilla;
        }

        // ---- putting the clone on the table object ----

        bool ReadSlotsSEH(void** vtable, void** out, int count)
        {
            return ReadEntryBytesSEH(vtable, out, sizeof(void*) * count);
        }

        void* ExistingTableSEH()
        {
            __try
            {
                void* manager = g_GetCommonDataManager ? g_GetCommonDataManager() : nullptr;
                void* common = manager ? g_GetCommonData(manager) : nullptr;
                return common ? *reinterpret_cast<void**>(static_cast<std::uint8_t*>(common) + 0xCA0) : nullptr;   // MotherBaseMenuCommonData: the top-menu table
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        void PlaceHooksInClone()
        {
            g_Clone[1 + kSlotIsVisible]       = reinterpret_cast<void*>(&hkIsVisible);
            g_Clone[1 + kSlotIsEnabled]       = reinterpret_cast<void*>(&hkIsEnabled);
            g_Clone[1 + kSlotClosesIDroid]    = reinterpret_cast<void*>(&hkClosesIDroid);
            g_Clone[1 + kSlotStaysInSelector] = reinterpret_cast<void*>(&hkStaysInSelector);
            g_Clone[1 + kSlotGetParent]       = reinterpret_cast<void*>(&hkGetParent);
            g_Clone[1 + kSlotGetRemembered]   = reinterpret_cast<void*>(&hkGetRemembered);
            g_Clone[1 + kSlotGetNoticeBits]   = reinterpret_cast<void*>(&hkGetNoticeBits);
            g_Clone[1 + kSlotGetNoticeCount]  = reinterpret_cast<void*>(&hkGetNoticeCount);
            g_Clone[1 + kSlotGetPressCount]   = reinterpret_cast<void*>(&hkGetPressCount);
            g_Clone[1 + kSlotGetActId]        = reinterpret_cast<void*>(&hkGetActId);
            g_Clone[1 + kSlotGetTitleLangId]  = reinterpret_cast<void*>(&hkGetTitleLangId);
            g_Clone[1 + kSlotGetTitleText]    = reinterpret_cast<void*>(&hkGetTitleText);
            g_Clone[1 + kSlotGetHelpLangId]   = reinterpret_cast<void*>(&hkGetHelpLangId);
            g_Clone[1 + kSlotGetHelpText]     = reinterpret_cast<void*>(&hkGetHelpText);
            g_Clone[1 + kSlotGetTrigger]      = reinterpret_cast<void*>(&hkGetTrigger);
            g_Clone[1 + kSlotSetVisible]      = reinterpret_cast<void*>(&hkSetVisible);
            g_Clone[1 + kSlotSetActive]       = reinterpret_cast<void*>(&hkSetActive);
            g_Clone[1 + kSlotSetLocked]       = reinterpret_cast<void*>(&hkSetLocked);
            g_Clone[1 + kSlotIncPressCount]   = reinterpret_cast<void*>(&hkIncPressCount);
            g_Clone[1 + kSlotSetRemembered]   = reinterpret_cast<void*>(&hkSetRemembered);
            g_Clone[1 + kSlotOrNoticeBits]    = reinterpret_cast<void*>(&hkOrNoticeBits);
            g_Clone[1 + kSlotClearNoticeBits] = reinterpret_cast<void*>(&hkClearNoticeBits);
            g_Clone[1 + kSlotSetNoticeCount]  = reinterpret_cast<void*>(&hkSetNoticeCount);
            g_Clone[1 + kSlotFindVisible]     = reinterpret_cast<void*>(&hkFindVisibleChildItemIds);
            g_Clone[1 + kSlotGetRootTab]      = reinterpret_cast<void*>(&hkGetRootTab);
            g_Clone[1 + kSlotLabelToIndex]    = reinterpret_cast<void*>(&hkLabelToIndex);
        }

        bool BuildCloneFrom(void** source)
        {
            void* before = nullptr;
            void* slots[kSlotCount] = {};
            if (!ReadEntryBytesSEH(source - 1, &before, sizeof(before)) || !ReadSlotsSEH(source, slots, kSlotCount))
                return false;
            for (int i = 0; i < kSlotCount; ++i)
                if (!slots[i])
                    return false;
            g_Clone[0] = before;
            for (int i = 0; i < kSlotCount; ++i)
            {
                g_Clone[1 + i] = slots[i];
                g_Orig[i] = slots[i];
            }
            PlaceHooksInClone();
            g_SourceVtable = source;
            g_CloneBuilt = true;
            return true;
        }

        void SwapTable(void* table)
        {
            if (!table || !g_Ready.load(std::memory_order_acquire))
                return;
            void** const live = [&]() -> void** {
                void* vptr = nullptr;
                return ReadEntryBytesSEH(table, &vptr, sizeof(vptr)) ? static_cast<void**>(vptr) : nullptr;
            }();
            if (!live)
                return;
            std::lock_guard<std::mutex> lock(g_SwapLock);
            if (live == &g_Clone[1])
            {
                g_Table.store(table, std::memory_order_release);
                return;
            }
            if (!g_CloneBuilt)
            {
                if (live != g_ExeVtable)
                    LogWarn("iDroid menu: the table's vtable was replaced by another module, cloning that one");
                if (!BuildCloneFrom(live))
                {
                    LogError("iDroid menu: the table's vtable could not be read, custom items stay off");
                    g_Ready.store(false, std::memory_order_release);
                    return;
                }
            }
            else if (live != g_SourceVtable && live != g_ExeVtable)
            {
                LogWarn("iDroid menu: the table's vtable belongs to another module, custom items stay off for this table");
                return;
            }
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(table), &g_Clone[1]);
            g_Table.store(table, std::memory_order_release);
        }

        void MarkTableDirty()
        {
            void* table = g_Table.load(std::memory_order_acquire);
            if (table && g_Orig[kSlotMarkDirty] && ExistingTableSEH() == table)
                reinterpret_cast<void(__fastcall*)(void*)>(g_Orig[kSlotMarkDirty])(table);
        }

        std::uint8_t LabelToIndexSEH(void* table, const char* menuName)   // the game's own TppTerminal.MBDVCMENU name lookup
        {
            __try { return Original<LabelToIndex_t>(kSlotLabelToIndex)(table, menuName); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kNone; }
        }

        void* __fastcall hkTableFactory()
        {
            void* table = g_OrigFactory();
            SwapTable(table);
            return table;
        }
    }

    bool VanillaEntryByMenuName(const char* menuName, std::uint8_t& id)
    {
        void* table = g_Table.load(std::memory_order_acquire);
        if (!menuName || !*menuName || !table || !g_Orig[kSlotLabelToIndex])
            return false;
        const std::uint8_t index = LabelToIndexSEH(table, menuName);
        if (index == kNone)
            return false;
        id = index;
        return true;
    }

    bool IsReady()
    {
        return g_Ready.load(std::memory_order_acquire);
    }

    bool ItemsAllowed()
    {
        if (!g_IsFobMode)
            return false;
        return !MissionCodeGuard::ShouldBypassHooks() && !InFobSEH();
    }

    void NoteTable(void* table)
    {
        SwapTable(table);
    }

    namespace
    {
        std::uint8_t AddItemNow(const ItemSpec& spec);
    }

    std::uint8_t AddItem(const ItemSpec& spec)
    {
        const std::uint8_t id = AddItemNow(spec);
        if (id)
            MarkTableDirty();
        return id;
    }

    namespace
    {
    std::uint8_t AddItemNow(const ItemSpec& spec)
    {
        if (!IsReady())
        {
            LogWarnOnce("V_UixUtility.AddIDroidMenuItem: the iDroid menu is not available on this build");
            return 0;
        }
        if (spec.name.empty() || (spec.label.empty() && !spec.labelLang))
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: name and label are required");
            return 0;
        }
        const std::uint64_t identity = FoxHashes::StrCode64(spec.name) & kId48Mask;
        if (identity == kNoTrigger || identity == kBackTrigger || identity == kShowMenuTrigger || identity == 0)
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: the name '%s' collides with a reserved trigger, use another name", spec.name.c_str());
            return 0;
        }
        const std::uint32_t actId = static_cast<std::uint32_t>(FoxHashes::StrCode64(spec.name));   // TppUiCommand.SetMbTopMenuItemVisible/Active(name) find the item by it

        std::uint8_t icon = spec.iconTexture ? 0 : spec.icon;
        if (icon)
        {
            float u = 0.0f, v = 0.0f;
            std::uint64_t texture = 0;
            if (g_OrigGetItemIcon)
                g_OrigGetItemIcon(icon, &u, &v, &texture);
            if (!texture)
            {
                LogWarn("V_UixUtility.AddIDroidMenuItem: '%s' asks for the icon of a menu entry that has none, it keeps its group's icon", spec.name.c_str());
                icon = 0;
            }
        }

        std::lock_guard<std::mutex> lock(g_ItemsLock);

        int found = -1;
        int firstFree = -1;
        for (int i = 0; i < kItemCount; ++i)
        {
            if (g_Items[i].used && g_Items[i].identity == identity)
                found = i;
            if (!g_Items[i].used && firstFree < 0)
                firstFree = i;
        }

        if (found >= 0 && g_Items[found].group != spec.group)
        {
            RemoveLocked(static_cast<std::uint8_t>(kFirstItemId + found));   // a group turned into a leaf or back: start over
            found = -1;
            firstFree = -1;
            for (int i = 0; i < kItemCount && firstFree < 0; ++i)
                if (!g_Items[i].used)
                    firstFree = i;
        }

        const int level = ParentLevelLocked(spec.parent);
        if (level < 0)
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: '%s' has a parent that is not a vanilla group or a group item", spec.name.c_str());
            return 0;
        }
        if (found >= 0)
        {
            std::uint8_t up = spec.parent;
            for (int hop = 0; hop < 4 && IsVirtual(up); ++hop)
            {
                if (up == kFirstItemId + found)
                {
                    LogWarn("V_UixUtility.AddIDroidMenuItem: '%s' cannot sit inside itself", spec.name.c_str());
                    return 0;
                }
                up = ItemOf(up).parent;
            }
        }
        if (level + 1 > 3 || (spec.group && level + 1 > 2))
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: '%s' is too deep, the iDroid menu has 3 levels and a group needs one more below it", spec.name.c_str());
            return 0;
        }

        int index = found >= 0 ? found : firstFree;
        if (index < 0)
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: no free item ids left (%d are in use)", kItemCount);
            return 0;
        }

        Item& item = g_Items[index];
        if (found < 0)
        {
            item = Item();
            item.used = true;
            item.identity = identity;
            g_ItemsUsed.fetch_add(1, std::memory_order_relaxed);
        }
        item.group = spec.group;
        item.parent = spec.parent;
        item.icon = icon;
        item.iconTexture = spec.iconTexture;
        item.iconU = spec.iconU;
        item.iconV = spec.iconV;
        item.actId = actId;
        item.labelLang = spec.labelLang & kId48Mask;
        item.descriptionLang = spec.descriptionLang & kId48Mask;
        item.name = spec.name;
        RetireTextLocked(item.label);
        RetireTextLocked(item.description);
        item.label = std::make_shared<const std::string>(spec.label);
        item.description = std::make_shared<const std::string>(spec.description);
        return static_cast<std::uint8_t>(kFirstItemId + index);
    }
    }

    bool RemoveItem(std::uint8_t id)
    {
        if (!IsVirtual(id))
            return false;
        {
            std::lock_guard<std::mutex> lock(g_ItemsLock);
            if (!ItemOf(id).used)
                return false;
            RemoveLocked(id);
        }
        MarkTableDirty();
        return true;
    }

    void ClearAll()
    {
        std::lock_guard<std::mutex> lock(g_ItemsLock);
        for (int i = 0; i < kItemCount; ++i)
            if (g_Items[i].used)
                ClearItemLocked(static_cast<std::uint8_t>(kFirstItemId + i));
    }

    bool FindItemByTrigger(std::uint64_t trigger, std::uint8_t& id)
    {
        if (g_ItemsUsed.load(std::memory_order_relaxed) == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_ItemsLock);
        for (int i = 0; i < kItemCount; ++i)
            if (g_Items[i].used && !g_Items[i].group && g_Items[i].identity == trigger)
            {
                id = static_cast<std::uint8_t>(kFirstItemId + i);
                return true;
            }
        return false;
    }

    bool Install(HMODULE)
    {
        if (!gAddr.MotherBaseDeviceTopMenuTableImpl_Vtable
            || !gAddr.MotherBaseDeviceTopMenuTableImpl_Factory
            || !gAddr.MotherBaseDeviceTopMenuTableImpl_IsVisibleItem
            || !gAddr.MotherBaseDeviceTopMenuTableImpl_SetVisibleItem
            || !gAddr.Barrier_IsFobMode
            || !gAddr.Ui_UiCommonDataManagerGetInstance
            || !gAddr.UiCommonDataManager_GetMotherBaseMenuCommonData)
        {
            LogError("iDroid menu: no address for this build, custom iDroid items are off");
            return true;
        }

        g_ExeVtable = static_cast<void**>(ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_Vtable));
        void* slots[kSlotCount + 1] = {};
        if (!g_ExeVtable || !ReadSlotsSEH(g_ExeVtable, slots, kSlotCount + 1)
            || slots[kSlotIsVisible] != ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_IsVisibleItem)
            || slots[kSlotSetVisible] != ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_SetVisibleItem)
            || slots[kSlotCount] != nullptr)   // the vanilla vtable has exactly 49 slots, the next qword is 0
        {
            LogError("iDroid menu: the table's vtable is not the one this build expects, custom iDroid items are off");
            return true;
        }

        g_IsFobMode = reinterpret_cast<BoolFn_t>(ResolveGameAddress(gAddr.Barrier_IsFobMode));
        g_GetCommonDataManager = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.Ui_UiCommonDataManagerGetInstance));
        g_GetCommonData = reinterpret_cast<GetCommonData_t>(ResolveGameAddress(gAddr.UiCommonDataManager_GetMotherBaseMenuCommonData));
        g_Ready.store(true, std::memory_order_release);

        g_FactoryTarget = ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_Factory);
        const HookEntry entry = CheckHookEntry(g_FactoryTarget, kFactoryEntry, sizeof(kFactoryEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("iDroid menu: the table factory is not the expected code, a table made later is only found when the iDroid opens");
            g_FactoryTarget = nullptr;
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("iDroid menu table factory", g_FactoryTarget);
            if (!CreateAndEnableHook(g_FactoryTarget, &hkTableFactory, reinterpret_cast<void**>(&g_OrigFactory)))
                g_FactoryTarget = nullptr;
        }

        g_ItemIconTarget = ResolveGameAddress(gAddr.MbTopMenuSelectorEv_GetItemIcon);
        const HookEntry iconEntry = g_ItemIconTarget ? CheckHookEntry(g_ItemIconTarget, kItemIconEntry, sizeof(kItemIconEntry)) : HookEntry::Unknown;
        if (iconEntry == HookEntry::Unknown)
        {
            LogError("iDroid menu: the row icon function is not the expected code, custom items draw without a real icon");
            g_ItemIconTarget = nullptr;
        }
        else
        {
            if (iconEntry == HookEntry::ForeignHook)
                LogHookedOnTop("iDroid menu row icon", g_ItemIconTarget);
            if (!CreateAndEnableHook(g_ItemIconTarget, &hkGetItemIcon, reinterpret_cast<void**>(&g_OrigGetItemIcon)))
                g_ItemIconTarget = nullptr;
        }

        SwapTable(ExistingTableSEH());
        return true;
    }

    void Uninstall()
    {
        g_Ready.store(false, std::memory_order_release);
        if (g_FactoryTarget)
        {
            DisableAndRemoveHook(g_FactoryTarget);
            g_FactoryTarget = nullptr;
        }
        if (g_ItemIconTarget)
        {
            DisableAndRemoveHook(g_ItemIconTarget);
            g_ItemIconTarget = nullptr;
            g_OrigGetItemIcon = nullptr;
        }

        void* table = g_Table.exchange(nullptr, std::memory_order_acq_rel);
        if (!table || ExistingTableSEH() != table)   // the object is gone, nothing to give back
            return;
        void* vptr = nullptr;
        if (ReadEntryBytesSEH(table, &vptr, sizeof(vptr)) && vptr == &g_Clone[1] && g_SourceVtable)
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(table), g_SourceVtable);
    }
}
