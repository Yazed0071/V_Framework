#include <pch.h>
#include "DataBaseCallbackImpl_RefreshPrefabList.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "AddressSet.h"
#include "HookArena.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"
#include "V_FrameWorkState.h"
#include "../equip/CustomBluePrint.h"
#include "../equip/DataBaseControllerImpl_AddDataBase.h"
#include "../buddy/BuddyCodeCave.h"

namespace
{
    constexpr std::uint8_t kTabDefault    = 2;
    constexpr std::size_t  kRowsOffset    = 0x80;
    constexpr std::size_t  kCountOffset   = 0x284;
    constexpr std::size_t  kRowsCapacity  = 108;      // rows the list object holds itself
    constexpr std::size_t  kMostRows      = 0x7FFF;   // the list reads its row count as a signed 16-bit number

    std::uint16_t  g_OverflowRows[kMostRows - kRowsCapacity] = {};
    std::uintptr_t g_OverflowOwner  = 0;                // the list object rows past 108 belong to (read by the caves)
    std::uint16_t* g_OverflowRowsAt = g_OverflowRows;   // read by the caves
    bool           g_RowCavesLive   = false;

    constexpr std::size_t kRec_ImplOffset = 0x30;
    constexpr std::size_t kRec_NodeOffset = 0x60;
    constexpr std::size_t kRec_IconNodeA  = 0x50;
    constexpr std::size_t kRec_IconNodeB  = 0x58;
    constexpr std::size_t kRec_InfoUi     = 0x28;
    constexpr std::size_t kImpl_UiOffset  = 0x08;
    constexpr std::size_t kImpl_ArgOffset = 0x38;
    constexpr std::size_t kUiVtbl_SetText = 0x708;
    constexpr std::size_t kUiVtbl_SetIcon = 0x518;
    constexpr std::size_t kUiVtbl_LangStr = 0x750;
    constexpr std::size_t kUiVtbl_InfoTxt = 0x738;

    constexpr std::size_t kInfo_Ui       = 0x28;
    constexpr std::size_t kInfo_Arg      = 0x60;
    constexpr std::size_t kInfo_TextNode = 0x6c8;
    constexpr std::size_t kInfo_Cursor   = 0x290;
    constexpr std::size_t kInfo_Scroll   = 0x28c;
    constexpr int         kInfoTextMode  = 8;

    constexpr std::size_t kImageRec_IconA = 0x28;
    constexpr std::size_t kImageRec_IconB = 0x30;

    constexpr std::uint64_t kIconSlotHash    = 0xcafb3bbf9889ull;
    constexpr std::uint64_t kIconSlotHashAlt = 0xec3f8d982b8eull;

    using RefreshPrefabList_t  = void(__fastcall*)(void* self, std::uint8_t tab);
    using RefreshNameText_t    = void(__fastcall*)(void* self, std::uint16_t id, std::uint8_t cat);
    using RefreshIcon_t        = void(__fastcall*)(void* self, std::uint16_t id, std::uint8_t cat);
    using GetInfoText_t        = const char*(__fastcall*)(void* self, std::uint16_t id);
    using RefreshInfoLayout_t  = void(__fastcall*)(void* self);
    using CollectionRate_t     = void(__fastcall*)(void* self, bool show, std::uint32_t got,
                                                  std::uint32_t total);
    using RefreshImageRecord_t = void(__fastcall*)(void* self, void* rec, std::uint16_t row,
                                                  std::uint16_t mode);
    using SetLangText_t        = void(__fastcall*)(void* ui, void* node, void* arg,
                                                  const char* text);
    using SetInfoText_t        = void(__fastcall*)(void* ui, void* node, void* arg, int mode,
                                                  const char* text, bool wrap, bool fit);
    using SetIconTex_t         = void(__fastcall*)(void* ui, void* node,
                                                  std::uint64_t iconHash, std::uint64_t slotHash);
    using LangToString_t       = const char*(__fastcall*)(void* ui, std::uint64_t langHash);

    RefreshPrefabList_t g_Orig = nullptr;
    RefreshNameText_t g_OrigName = nullptr;
    RefreshIcon_t g_OrigIcon = nullptr;
    GetInfoText_t g_OrigInfo = nullptr;
    RefreshInfoLayout_t g_OrigInfoLayout = nullptr;
    RefreshImageRecord_t g_OrigImageRecord = nullptr;
    CollectionRate_t g_OrigCollectionRate = nullptr;

    bool g_Installed = false;
    bool g_NameInstalled = false;
    bool g_IconInstalled = false;
    bool g_InfoInstalled = false;
    bool g_InfoLayoutInstalled = false;
    bool g_ImageRecordInstalled = false;
    bool g_CollectionRateInstalled = false;

    std::uint32_t g_TabCustomRows = 0;
    std::uint32_t g_TabCustomOwned = 0;

    bool g_CapacityLogged = false;
    bool g_NameSetFailedLogged = false;
    bool g_NameMissingLogged = false;
    bool g_IconSetFailedLogged = false;
    bool g_InfoLayoutFailedLogged = false;
    bool g_ImageIconFailedLogged = false;

    std::vector<std::uint16_t>* g_CollectTarget = nullptr;
    std::uint8_t g_CollectTab = kTabDefault;

    std::uint8_t TabForBluePrint(std::int32_t publicId)
    {
        bluePrintDb::Documentation doc;
        return bluePrintDb::TryGetDocumentation(publicId, doc) ? doc.tab : kTabDefault;
    }

    void CollectBluePrint(const char* key, std::int32_t slot, bool owned)
    {
        UNREFERENCED_PARAMETER(key);
        UNREFERENCED_PARAMETER(owned);

        if (!g_CollectTarget || slot <= 0)
            return;

        const std::int32_t publicId = bluePrint::PublicId(slot);
        if (publicId <= 0 || publicId > 0xFFFE)
            return;

        if (TabForBluePrint(publicId) != g_CollectTab)
            return;

        g_CollectTarget->push_back(static_cast<std::uint16_t>(publicId));
    }

    std::size_t AppendRowsSeh(void* self, const std::vector<std::uint16_t>& ids, std::size_t room)
    {
        __try
        {
            auto* rows  = reinterpret_cast<std::uint16_t*>(
                static_cast<std::uint8_t*>(self) + kRowsOffset);
            auto* count = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(self) + kCountOffset);

            std::uint32_t used = *count;
            if (used >= room)
                return 0;

            std::size_t added = 0;
            for (const std::uint16_t id : ids)
            {
                if (used >= room)
                    break;
                if (used < kRowsCapacity)
                    rows[used] = id;
                else
                    g_OverflowRows[used - kRowsCapacity] = id;
                ++used;
                ++added;
            }

            *count = used;
            return added;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool SetRowNameSeh(void* self, std::uint64_t langHash)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            void* impl = *reinterpret_cast<void**>(base + kRec_ImplOffset);
            void* node = *reinterpret_cast<void**>(base + kRec_NodeOffset);
            if (!impl)
                return false;

            auto* implBytes = static_cast<std::uint8_t*>(impl);
            void* ui  = *reinterpret_cast<void**>(implBytes + kImpl_UiOffset);
            void* arg = *reinterpret_cast<void**>(implBytes + kImpl_ArgOffset);
            if (!ui)
                return false;

            void** vtbl = *reinterpret_cast<void***>(ui);
            if (!vtbl)
                return false;

            auto setText = reinterpret_cast<SetLangText_t>(
                vtbl[kUiVtbl_SetText / sizeof(void*)]);
            auto toString = reinterpret_cast<LangToString_t>(
                vtbl[kUiVtbl_LangStr / sizeof(void*)]);
            if (!setText || !toString)
                return false;

            const char* text = toString(ui, langHash);
            if (!text)
                return false;

            setText(ui, node, arg, text);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    using GetUixUtility_t = void**(*)();
    GetUixUtility_t g_GetUixUtility = nullptr;

    void PrefetchIconTexture(std::uint64_t textureHash)
    {
        if (textureHash == 0 || gAddr.GetUixUtilityToFeedQuarkEnvironment == 0)
            return;
        if (!g_GetUixUtility)
            g_GetUixUtility = reinterpret_cast<GetUixUtility_t>(
                ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
        if (!g_GetUixUtility)
            return;

        __try
        {
            void** util = g_GetUixUtility();
            if (!util)
                return;
            void** vtbl = *reinterpret_cast<void***>(util);
            if (!vtbl)
                return;
            auto fn = reinterpret_cast<void(__fastcall*)(void*, std::uint64_t, int)>(
                vtbl[0x548 / sizeof(void*)]);
            if (fn)
                fn(util, textureHash, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool SetRowIconSeh(void* self, std::uint64_t iconHash)
    {
        PrefetchIconTexture(iconHash);

        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            void* impl = *reinterpret_cast<void**>(base + kRec_ImplOffset);
            if (!impl)
                return false;

            void* ui = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(impl) + kImpl_UiOffset);
            if (!ui)
                return false;

            void** vtbl = *reinterpret_cast<void***>(ui);
            if (!vtbl)
                return false;

            auto setIcon = reinterpret_cast<SetIconTex_t>(
                vtbl[kUiVtbl_SetIcon / sizeof(void*)]);
            if (!setIcon)
                return false;

            void* nodeA = *reinterpret_cast<void**>(base + kRec_IconNodeA);
            void* nodeB = *reinterpret_cast<void**>(base + kRec_IconNodeB);

            setIcon(ui, nodeA, iconHash, kIconSlotHash);
            setIcon(ui, nodeB, iconHash, kIconSlotHash);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* ResolveLangTextSeh(void* self, std::uint64_t langHash)
    {
        __try
        {
            void* ui = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(self) + kRec_InfoUi);
            if (!ui)
                return nullptr;

            void** vtbl = *reinterpret_cast<void***>(ui);
            if (!vtbl)
                return nullptr;

            auto toString = reinterpret_cast<LangToString_t>(
                vtbl[kUiVtbl_LangStr / sizeof(void*)]);
            if (!toString)
                return nullptr;

            return toString(ui, langHash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool CustomDocumentation(std::uint16_t id, bluePrintDb::Documentation& doc)
    {
        const std::int32_t publicId = static_cast<std::int32_t>(id);
        const std::int32_t slot = bluePrint::SlotFromPublicId(publicId);
        if (slot <= 0 || !bluePrint::Has(slot))
            return false;
        return bluePrintDb::TryGetDocumentation(publicId, doc);
    }

    void __fastcall hkRefreshDataBaseNameText(void* self, std::uint16_t id, std::uint8_t cat)
    {
        bluePrintDb::Documentation doc;

        if (self && CustomDocumentation(id, doc))
        {
            if (doc.nameHash == 0)
            {
                if (!g_NameMissingLogged)
                {
                    g_NameMissingLogged = true;
                    LogWarn("BluePrint: dataBaseId %u has no langDocName (SetDataBaseDisplay), "
                        "DATABASE row shows '???'", static_cast<unsigned>(id));
                }
            }
            else if (SetRowNameSeh(self, doc.nameHash))
            {
                return;
            }
            else if (!g_NameSetFailedLogged)
            {
                g_NameSetFailedLogged = true;
                LogWarn("BluePrint: DATABASE name node unreachable for dataBaseId %u, row "
                    "shows '???'", static_cast<unsigned>(id));
            }
        }

        if (g_OrigName)
            g_OrigName(self, id, cat);
    }

    void __fastcall hkRefreshDataBaseIcon(void* self, std::uint16_t id, std::uint8_t cat)
    {
        bluePrintDb::Documentation doc;
        const bool custom = CustomDocumentation(id, doc);

        if (self && custom && doc.iconHash != 0)
        {
            if (!SetRowIconSeh(self, doc.iconHash) && !g_IconSetFailedLogged)
            {
                g_IconSetFailedLogged = true;
                LogWarn("BluePrint: DATABASE icon node unreachable for dataBaseId %u, row "
                    "keeps '?' icon", static_cast<unsigned>(id));
            }
            return;
        }

        if (g_OrigIcon)
            g_OrigIcon(self, id, cat);
    }

    const char* __fastcall hkGetDataBaseInfoText(void* self, std::uint16_t id)
    {
        bluePrintDb::Documentation doc;

        if (self && CustomDocumentation(id, doc) && doc.infoHash != 0)
        {
            if (const char* text = ResolveLangTextSeh(self, doc.infoHash))
                return text;
        }

        return g_OrigInfo ? g_OrigInfo(self, id) : nullptr;
    }

    std::uint16_t RowIdSeh(void* self, std::uint32_t row)
    {
        __try
        {
            if (row >= kRowsCapacity)
            {
                if (!g_RowCavesLive || row >= kMostRows
                    || reinterpret_cast<std::uintptr_t>(self) != g_OverflowOwner)
                    return 0xFFFF;
                return g_OverflowRows[row - kRowsCapacity];
            }
            return *reinterpret_cast<std::uint16_t*>(
                static_cast<std::uint8_t*>(self) + kRowsOffset + row * sizeof(std::uint16_t));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    std::uint16_t SelectedRowIdSeh(void* self)
    {
        std::uint32_t row = 0;

        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(base + kCountOffset);
            if (count == 0)
                return 0xFFFF;

            const std::uint32_t cursor = *reinterpret_cast<std::uint32_t*>(base + kInfo_Cursor);
            const std::uint32_t scroll = *reinterpret_cast<std::uint32_t*>(base + kInfo_Scroll);
            row = (cursor + scroll) % count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }

        return RowIdSeh(self, row);
    }

    bool SetInfoTextSeh(void* self, std::uint64_t langHash)
    {
        __try
        {
            auto* base = static_cast<std::uint8_t*>(self);
            void* ui = *reinterpret_cast<void**>(base + kInfo_Ui);
            if (!ui)
                return false;

            void** vtbl = *reinterpret_cast<void***>(ui);
            if (!vtbl)
                return false;

            auto toString = reinterpret_cast<LangToString_t>(
                vtbl[kUiVtbl_LangStr / sizeof(void*)]);
            auto setInfo = reinterpret_cast<SetInfoText_t>(
                vtbl[kUiVtbl_InfoTxt / sizeof(void*)]);
            if (!toString || !setInfo)
                return false;

            const char* text = toString(ui, langHash);
            if (!text)
                return false;

            setInfo(ui, *reinterpret_cast<void**>(base + kInfo_TextNode),
                    *reinterpret_cast<void**>(base + kInfo_Arg), kInfoTextMode, text, true, true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetImageIconSeh(void* self, void* rec, std::uint64_t iconHash)
    {
        PrefetchIconTexture(iconHash);

        __try
        {
            void* ui = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(self) + kInfo_Ui);
            if (!ui)
                return false;

            void** vtbl = *reinterpret_cast<void***>(ui);
            if (!vtbl)
                return false;

            auto setIcon = reinterpret_cast<SetIconTex_t>(
                vtbl[kUiVtbl_SetIcon / sizeof(void*)]);
            if (!setIcon)
                return false;

            auto* recBytes = static_cast<std::uint8_t*>(rec);
            void* nodeA = *reinterpret_cast<void**>(recBytes + kImageRec_IconA);
            void* nodeB = *reinterpret_cast<void**>(recBytes + kImageRec_IconB);

            setIcon(ui, nodeA, iconHash, kIconSlotHash);
            setIcon(ui, nodeA, iconHash, kIconSlotHashAlt);
            setIcon(ui, nodeB, iconHash, kIconSlotHash);
            setIcon(ui, nodeB, iconHash, kIconSlotHashAlt);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkRefreshInfoLayout(void* self)
    {
        if (g_OrigInfoLayout)
            g_OrigInfoLayout(self);

        if (!self)
            return;

        bluePrintDb::Documentation doc;
        if (!CustomDocumentation(SelectedRowIdSeh(self), doc) || doc.infoHash == 0)
            return;

        if (!SetInfoTextSeh(self, doc.infoHash) && !g_InfoLayoutFailedLogged)
        {
            g_InfoLayoutFailedLogged = true;
            LogWarn("BluePrint: DATABASE description node unreachable, custom detail text "
                "shows '???'");
        }
    }

    void __fastcall hkRefreshImageRecord(void* self, void* rec, std::uint16_t row,
                                         std::uint16_t mode)
    {
        if (g_OrigImageRecord)
            g_OrigImageRecord(self, rec, row, mode);

        if (!self || !rec)
            return;

        bluePrintDb::Documentation doc;
        if (!CustomDocumentation(RowIdSeh(self, row), doc))
            return;

        const std::uint64_t imageHash = doc.imageHash ? doc.imageHash : doc.iconHash;
        if (imageHash == 0)
            return;

        if (!SetImageIconSeh(self, rec, imageHash) && !g_ImageIconFailedLogged)
        {
            g_ImageIconFailedLogged = true;
            LogWarn("BluePrint: DATABASE detail image node unreachable, custom detail "
                "image shows '?'");
        }
    }

    void __fastcall hkRefreshCollectionRate(void* self, bool show, std::uint32_t got,
                                            std::uint32_t total)
    {
        if (g_OrigCollectionRate)
            g_OrigCollectionRate(self, show, got + g_TabCustomOwned, total + g_TabCustomRows);
    }

    void __fastcall hkRefreshPrefabList(void* self, std::uint8_t tab)
    {
        if (g_Orig)
            g_Orig(self, tab);

        if (!self)
            return;

        g_TabCustomRows = 0;
        g_TabCustomOwned = 0;

        std::vector<std::uint16_t> ids;
        g_CollectTarget = &ids;
        g_CollectTab = tab;
        V_FrameWorkState::ForEachBluePrint(&CollectBluePrint);
        g_CollectTarget = nullptr;

        if (ids.empty())
            return;

        const std::size_t room = g_RowCavesLive ? kMostRows : kRowsCapacity;
        if (g_RowCavesLive)
            g_OverflowOwner = reinterpret_cast<std::uintptr_t>(self);
        const std::size_t added = AppendRowsSeh(self, ids, room);

        g_TabCustomRows = static_cast<std::uint32_t>(added);
        for (std::size_t i = 0; i < added; ++i)
        {
            const std::int32_t slot = bluePrint::SlotFromPublicId(
                static_cast<std::int32_t>(ids[i]));
            if (slot > 0 && bluePrint::Has(slot))
                ++g_TabCustomOwned;
        }

        if (added < ids.size() && !g_CapacityLogged)
        {
            g_CapacityLogged = true;
            LogWarn("BluePrint: only %zu of %zu custom rows fit DATABASE tab %u (%zu max), "
                "rest hidden", added, ids.size(), static_cast<unsigned>(tab), room);
        }
    }

    bool InstallOne(std::uintptr_t address, void* detour, void** orig, const char* what)
    {
        if (!address)
            return false;

        void* target = ResolveGameAddress(address);
        if (CreateAndEnableHook(target, detour, orig))
            return true;

        *orig = nullptr;
        LogError("BluePrint: DATABASE %s hook failed, custom blueprint rows keep the "
            "vanilla default there", what);
        return false;
    }

    void RemoveOne(std::uintptr_t address)
    {
        void* target = ResolveGameAddress(address);
        if (!target)
            return;
        DisableAndRemoveHook(target);
    }

    struct RowRead   // a game read of the list's row array; rows past its own 108 come from g_OverflowRows
    {
        const char*  name;
        int          function;
        std::size_t  offset;
        bool         throughPointer;      // reads [ptr + 0x80] with ptr = list + row * 2
        std::uint8_t length;
        std::uint8_t original[9];
        std::uint8_t indexTest[4];        // cmp row, 108; through a pointer: mov r11, ptr
        std::uint8_t ownerTest[3];        // cmp r11, list; through a pointer: cmp dst, rdi
        std::uint8_t loadAddress[2];      // through a pointer: mov dst, imm64
        std::uint8_t loadThrough[3];      // through a pointer: mov dst, [dst]
        std::uint8_t overflowRead[9];     // movzx dst, word [overflow + row * 2 - 216]
        std::uint8_t firstRowLength;
        std::uint8_t firstRow[8];         // movzx dst, word [list + 0x80], for a list the overflow is not for
    };

    const RowRead kRowReads[] = {
        { "image record", 0, 0x1B, false, 8, { 0x0F, 0xB7, 0xB4, 0x41, 0x80, 0x00, 0x00, 0x00 },
          { 0x48, 0x83, 0xF8, 0x6C }, { 0x49, 0x39, 0xCB }, {}, {},
          { 0x41, 0x0F, 0xB7, 0xB4, 0x43, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0xB1, 0x80, 0x00, 0x00, 0x00 } },
        { "detail text", 1, 0x70, false, 8, { 0x0F, 0xB7, 0x9C, 0x57, 0x80, 0x00, 0x00, 0x00 },
          { 0x48, 0x83, 0xFA, 0x6C }, { 0x49, 0x39, 0xFB }, {}, {},
          { 0x41, 0x0F, 0xB7, 0x9C, 0x53, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0x9F, 0x80, 0x00, 0x00, 0x00 } },
        { "map layout", 2, 0x23, false, 9, { 0x41, 0x0F, 0xB7, 0x8C, 0x52, 0x80, 0x00, 0x00, 0x00 },
          { 0x48, 0x83, 0xFA, 0x6C }, { 0x4D, 0x39, 0xD3 }, {}, {},
          { 0x41, 0x0F, 0xB7, 0x8C, 0x53, 0x28, 0xFF, 0xFF, 0xFF }, 8, { 0x41, 0x0F, 0xB7, 0x8A, 0x80, 0x00, 0x00, 0x00 } },
        { "view map", 3, 0xA9, false, 8, { 0x0F, 0xB7, 0x94, 0x47, 0x80, 0x00, 0x00, 0x00 },
          { 0x48, 0x83, 0xF8, 0x6C }, { 0x49, 0x39, 0xFB }, {}, {},
          { 0x41, 0x0F, 0xB7, 0x94, 0x43, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0x97, 0x80, 0x00, 0x00, 0x00 } },
        { "view map document", 3, 0xC9, true, 7, { 0x0F, 0xB7, 0x93, 0x80, 0x00, 0x00, 0x00 },
          { 0x49, 0x89, 0xDB }, { 0x48, 0x39, 0xFA }, { 0x48, 0xBA }, { 0x48, 0x8B, 0x12 },
          { 0x42, 0x0F, 0xB7, 0x94, 0x1A, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0x97, 0x80, 0x00, 0x00, 0x00 } },
        { "view map row", 3, 0x224, false, 8, { 0x0F, 0xB7, 0x84, 0x47, 0x80, 0x00, 0x00, 0x00 },
          { 0x48, 0x83, 0xF8, 0x6C }, { 0x49, 0x39, 0xFB }, {}, {},
          { 0x41, 0x0F, 0xB7, 0x84, 0x43, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0x87, 0x80, 0x00, 0x00, 0x00 } },
        { "view map row document", 3, 0x23A, true, 7, { 0x0F, 0xB7, 0xB1, 0x80, 0x00, 0x00, 0x00 },
          { 0x49, 0x89, 0xCB }, { 0x48, 0x39, 0xFE }, { 0x48, 0xBE }, { 0x48, 0x8B, 0x36 },
          { 0x42, 0x0F, 0xB7, 0xB4, 0x1E, 0x28, 0xFF, 0xFF, 0xFF }, 7, { 0x0F, 0xB7, 0xB7, 0x80, 0x00, 0x00, 0x00 } },
    };

    struct LiveRowCave
    {
        std::uint8_t*   site = nullptr;
        NativeCodePatch patches[2]{};
    };

    LiveRowCave g_RowCaves[std::size(kRowReads)];

    void EmitBytes(CaveCode& code, const std::uint8_t* bytes, std::size_t size)
    {
        code.bytes.insert(code.bytes.end(), bytes, bytes + size);
    }

    CaveCode BuildRowCave(const RowRead& read, std::uintptr_t back)
    {
        const auto owner = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&g_OverflowOwner));
        const auto rows  = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&g_OverflowRowsAt));
        CaveCode code;
        if (!read.throughPointer)
        {
            EmitBytes(code, read.indexTest, 4);
            const std::size_t pastOwnRows = code.Jcc8(0x73);
            EmitBytes(code, read.original, read.length);
            code.JmpAbs(back);
            code.Bind8(pastOwnRows);
            code.Emit({ 0x41, 0x53 });                     // push r11
            code.Emit({ 0x49, 0xBB }); code.Imm64(owner);  // mov r11, &owner
            code.Emit({ 0x4D, 0x8B, 0x1B });               // mov r11, [r11]
            EmitBytes(code, read.ownerTest, 3);
            const std::size_t otherList = code.Jcc8(0x75);
            code.Emit({ 0x49, 0xBB }); code.Imm64(rows);
            code.Emit({ 0x4D, 0x8B, 0x1B });
            EmitBytes(code, read.overflowRead, 9);
            code.Emit({ 0x41, 0x5B });                     // pop r11
            code.JmpAbs(back);
            code.Bind8(otherList);
            code.Emit({ 0x41, 0x5B });
            EmitBytes(code, read.firstRow, read.firstRowLength);
            code.JmpAbs(back);
            return code;
        }

        code.Emit({ 0x41, 0x53 });                         // push r11
        EmitBytes(code, read.indexTest, 3);
        code.Emit({ 0x49, 0x29, 0xFB });                   // sub r11, rdi: the row times 2
        code.Emit({ 0x49, 0x81, 0xFB, 0xD8, 0x00, 0x00, 0x00 });   // cmp r11, 216
        const std::size_t pastOwnRows = code.Jcc8(0x73);
        code.Emit({ 0x41, 0x5B });
        EmitBytes(code, read.original, read.length);
        code.JmpAbs(back);
        code.Bind8(pastOwnRows);
        EmitBytes(code, read.loadAddress, 2); code.Imm64(owner);
        EmitBytes(code, read.loadThrough, 3);
        EmitBytes(code, read.ownerTest, 3);
        const std::size_t otherList = code.Jcc8(0x75);
        EmitBytes(code, read.loadAddress, 2); code.Imm64(rows);
        EmitBytes(code, read.loadThrough, 3);
        EmitBytes(code, read.overflowRead, 9);
        code.Emit({ 0x41, 0x5B });
        code.JmpAbs(back);
        code.Bind8(otherList);
        code.Emit({ 0x41, 0x5B });
        EmitBytes(code, read.firstRow, read.firstRowLength);
        code.JmpAbs(back);
        return code;
    }

    std::uintptr_t RowReadFunction(int function)
    {
        switch (function)
        {
        case 0:  return gAddr.UiMbmDataBase_RefreshImageRecord;
        case 1:  return gAddr.UiMbmDataBase_RefreshInfoLayout;
        case 2:  return gAddr.UiMbmDataBase_RefreshMapLayout;
        default: return gAddr.UiMbmDataBase_RefreshViewMap;
        }
    }

    bool SiteMatchesSeh(const std::uint8_t* site, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return std::memcmp(site, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveRowCaves()
    {
        g_RowCavesLive = false;
        for (std::size_t i = 0; i < std::size(kRowReads); ++i)
        {
            LiveRowCave& cave = g_RowCaves[i];
            if (cave.site)
                RemoveNativeCodePatches(cave.patches, 2, nullptr, cave.site, cave.site + kRowReads[i].length);
            cave.site = nullptr;
        }
    }

    bool InstallRowCaves()
    {
        for (std::size_t i = 0; i < std::size(kRowReads); ++i)
        {
            const RowRead& read = kRowReads[i];
            auto* function = static_cast<std::uint8_t*>(ResolveGameAddress(RowReadFunction(read.function)));
            std::uint8_t* site = function ? function + read.offset : nullptr;
            if (!site || !SiteMatchesSeh(site, read.original, read.length))
            {
                LogError("BluePrint: the DATABASE %s row read is not the known code on %s, tabs keep 108 rows",
                         read.name, GetGameBuildName(gGameBuild));
                RemoveRowCaves();
                return false;
            }

            const CaveCode code = BuildRowCave(read, reinterpret_cast<std::uintptr_t>(site) + read.length);
            auto* thunk = static_cast<std::uint8_t*>(
                HookArena::AllocateNear(reinterpret_cast<std::uintptr_t>(site), code.bytes.size()));
            const std::int64_t rel = thunk
                ? static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(thunk))
                  - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site) + 5)
                : 0;
            if (!thunk || rel < INT32_MIN || rel > INT32_MAX)
            {
                LogError("BluePrint: no code page near the DATABASE %s row read, tabs keep 108 rows", read.name);
                RemoveRowCaves();
                return false;
            }
            std::memcpy(thunk, code.bytes.data(), code.bytes.size());
            FlushInstructionCache(GetCurrentProcess(), thunk, code.bytes.size());

            LiveRowCave& cave = g_RowCaves[i];
            cave.patches[0] = NativeCodePatch{};
            cave.patches[0].target = site;
            cave.patches[0].size = 5;
            std::memcpy(cave.patches[0].expected, read.original, 5);
            cave.patches[0].replacement[0] = 0xE9;
            const std::int32_t rel32 = static_cast<std::int32_t>(rel);
            std::memcpy(cave.patches[0].replacement + 1, &rel32, sizeof(rel32));
            cave.patches[1] = NativeCodePatch{};
            cave.patches[1].target = site + 5;
            cave.patches[1].size = static_cast<unsigned char>(read.length - 5);
            std::memcpy(cave.patches[1].expected, read.original + 5, read.length - 5u);
            std::memset(cave.patches[1].replacement, 0x90, read.length - 5u);

            bool done = false;
            DWORD error = ERROR_SUCCESS;
            for (int attempt = 0; attempt < 50 && !done; ++attempt)
            {
                done = InstallNativeCodePatches(cave.patches, 2, nullptr, site, site + read.length);
                error = GetLastError();
                if (!done && !NativePatchErrorIsTransient(error))
                    break;
                if (!done)
                    Sleep(4);
            }
            if (!done)
            {
                LogError("BluePrint: DATABASE %s row read patch refused (%s), tabs keep 108 rows",
                         read.name, NativePatchErrorName(error));
                RemoveRowCaves();
                return false;
            }
            cave.site = site;
        }
        g_RowCavesLive = true;
        return true;
    }
}

bool Install_DataBaseBluePrintList_Hook()
{
    if (g_Installed)
        return true;

    if (!gAddr.UiMbmDataBase_RefreshPrefabList)
        return true;

    void* target = ResolveGameAddress(gAddr.UiMbmDataBase_RefreshPrefabList);
    if (!CreateAndEnableHook(target, &hkRefreshPrefabList,
                             reinterpret_cast<void**>(&g_Orig)))
    {
        g_Orig = nullptr;
        LogError("BluePrint: DATABASE blueprint-list hook failed, custom blueprints not "
            "listed");
        return false;
    }

    g_Installed = true;

    g_NameInstalled = InstallOne(gAddr.UiMbmDataBase_RefreshDataBaseNameText,
                                 &hkRefreshDataBaseNameText,
                                 reinterpret_cast<void**>(&g_OrigName), "name");

    g_IconInstalled = InstallOne(gAddr.UiMbmDataBase_RefreshDataBaseIcon,
                                 &hkRefreshDataBaseIcon,
                                 reinterpret_cast<void**>(&g_OrigIcon), "icon");

    g_InfoInstalled = InstallOne(gAddr.UiMbmDataBase_GetDataBaseInfoText,
                                 &hkGetDataBaseInfoText,
                                 reinterpret_cast<void**>(&g_OrigInfo), "description");

    g_InfoLayoutInstalled = InstallOne(gAddr.UiMbmDataBase_RefreshInfoLayout,
                                       &hkRefreshInfoLayout,
                                       reinterpret_cast<void**>(&g_OrigInfoLayout),
                                       "detail description");

    g_ImageRecordInstalled = InstallOne(gAddr.UiMbmDataBase_RefreshImageRecord,
                                        &hkRefreshImageRecord,
                                        reinterpret_cast<void**>(&g_OrigImageRecord),
                                        "detail image");

    g_CollectionRateInstalled = InstallOne(gAddr.UiMbmDataBase_RefreshCollectionRate,
                                          &hkRefreshCollectionRate,
                                          reinterpret_cast<void**>(&g_OrigCollectionRate),
                                          "collection rate");

    InstallRowCaves();
    return true;
}

void Uninstall_DataBaseBluePrintList_Hook()
{
    if (!g_Installed)
        return;

    RemoveRowCaves();
    RemoveOne(gAddr.UiMbmDataBase_RefreshPrefabList);

    if (g_NameInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_RefreshDataBaseNameText);
        g_OrigName = nullptr;
        g_NameInstalled = false;
    }

    if (g_IconInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_RefreshDataBaseIcon);
        g_OrigIcon = nullptr;
        g_IconInstalled = false;
    }

    if (g_InfoInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_GetDataBaseInfoText);
        g_OrigInfo = nullptr;
        g_InfoInstalled = false;
    }

    if (g_InfoLayoutInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_RefreshInfoLayout);
        g_OrigInfoLayout = nullptr;
        g_InfoLayoutInstalled = false;
    }

    if (g_ImageRecordInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_RefreshImageRecord);
        g_OrigImageRecord = nullptr;
        g_ImageRecordInstalled = false;
    }

    if (g_CollectionRateInstalled)
    {
        RemoveOne(gAddr.UiMbmDataBase_RefreshCollectionRate);
        g_OrigCollectionRate = nullptr;
        g_CollectionRateInstalled = false;
    }

    g_Orig = nullptr;
    g_Installed = false;
}
