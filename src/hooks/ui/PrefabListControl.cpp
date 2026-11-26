#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "PrefabListControl.h"
#include "UixUtilityControl.h"
#include "UixUtilityDev.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using PrefabListControl::RowText;
    using PrefabListControl::kMaxColumns;
    using PrefabListControl::kMaxTitleColumns;
    using PrefabListControl::kMaxVisibleRows;
    using PrefabListControl::kMaxRows;
    using UixUtilityControl::Kind;
    using UixUtilityControl::Object;

    constexpr std::uint64_t kId48 = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kListTrigger = 0xED02C2866CCDull;   // hands the list its parameter block; the list keeps the pointer
    constexpr std::uint32_t kFlagsClosed = 1;
    constexpr std::uint32_t kFlagsOpen = 0x413;
    constexpr std::uint32_t kUnsetIndex = 0xFFFFFFFFu;
    constexpr int kTextBytes = 256;
    constexpr ULONGLONG kFreeDelayMs = 5000;   // after its window died, so the engine is done with the list for sure
    constexpr unsigned kRecordLayoutType = 0;
    constexpr unsigned kTitleLayoutType = 1;

    // vtable offsets exactly as the Missions list page code calls them
    constexpr std::size_t kUiGetModel = 0x248, kUiGetTextNode = 0x280, kUiSetText = 0x708;
    constexpr std::size_t kWindowGetChild = 0xA8;
    constexpr std::size_t kPrefabGetLayout = 0xC8, kPrefabCreateCursorFunc = 0x100, kPrefabCreateScrollbarFunc = 0x108;
    constexpr std::size_t kQuarkTableHolder = 0x98, kHolderContextSource = 0x40, kContextUi = 0x20, kContextPrefab = 0x28;

    constexpr std::uint64_t kButtonHashes[6] = {
        0x73D6D044F980ull, 0xBE0A2539F04Dull, 0x6FEF43D6B862ull, 0x586EBB2A56C4ull, 0x31443165E927ull, 0x26B0735EEAE0ull,
    };

    using IsFobMode_t       = bool(__fastcall*)();
    using GetQuarkTable_t   = std::uint8_t*(__fastcall*)();
    using GetPrefabLayout_t = void*(__fastcall*)(void* prefab, unsigned type, void* window);
    using GetByName_t       = void*(__fastcall*)(void* ui, void* parent, std::uint64_t id);
    using SetText_t         = void(__fastcall*)(void* ui, void* node, void* units, const char* text, int languageFont);
    using CreateFunc_t      = void*(__fastcall*)(void* prefab);
    using Destroy_t         = void*(__fastcall*)(void* object, unsigned flags);
    using GetChild_t        = void*(__fastcall*)(void* window, std::uint64_t id);
    using GetButton_t       = std::uint64_t(__fastcall*)(void** util, std::uint64_t hash, int mode);
    using SendTrigger_t     = void(__fastcall*)(void** util, void* window, std::uint64_t trigger, std::uint64_t param);

    IsFobMode_t       g_IsFobMode = nullptr;
    std::atomic<bool> g_Installed{ false };

    struct List;

    struct RowConfig
    {
        std::uint32_t version = 0;
        std::uint64_t model = 0;
        int           nodeCount = 0;
        std::uint64_t nodes[kMaxColumns] = {};
    };

    struct TitleConfig
    {
        std::uint32_t version = 0;
        std::uint64_t model = 0;
        int           nodeCount = 0;
        std::uint64_t nodes[kMaxTitleColumns] = {};
    };

    // the list parameter block the list graph reads and writes through the pointer it was sent
    struct alignas(16) Param
    {
        std::uint8_t           kind;                 // +0x00 1 = list
        std::uint8_t           reserved0[0x1F];
        volatile std::uint32_t flags;                // +0x20
        std::uint32_t          buttons[6];           // +0x24
        volatile std::uint32_t count;                // +0x3C
        volatile std::uint32_t visible;              // +0x40
        volatile std::uint32_t top;                  // +0x44
        volatile std::uint32_t cursor;               // +0x48 relative to top
        volatile std::uint32_t capacityMinusOne;     // +0x4C
        std::uint8_t           reserved1[8];         // +0x50
        void* const*           rows;                 // +0x58
        void*                  title;                // +0x60
        void*                  cursorFunc;           // +0x68
        void*                  scrollFunc;           // +0x70
        std::uint8_t           reserved2[0x80];      // +0x78
    };
    static_assert(offsetof(Param, flags) == 0x20 && offsetof(Param, buttons) == 0x24 && offsetof(Param, count) == 0x3C, "list parameter block layout");
    static_assert(offsetof(Param, top) == 0x44 && offsetof(Param, cursor) == 0x48 && offsetof(Param, capacityMinusOne) == 0x4C, "list parameter block layout");
    static_assert(offsetof(Param, rows) == 0x58 && offsetof(Param, title) == 0x60 && offsetof(Param, scrollFunc) == 0x70, "list parameter block layout");
    static_assert(sizeof(Param) >= 0xF8, "list parameter block size");

    struct Row   // the engine sees the vtable and the index at +0x08; the rest is ours
    {
        const void* const*     vtable = nullptr;
        volatile std::uint32_t dataIndex = kUnsetIndex;
        std::uint32_t          reserved0 = 0;
        std::uint8_t           flag = 0;                     // +0x10
        std::uint8_t           reserved1[7] = {};
        List*                  list = nullptr;
        int                    slot = 0;
        std::uint32_t          configVersion = 0;
        void*                  window = nullptr;
        void*                  nodes[kMaxColumns] = {};
        char                   text[kMaxColumns][kTextBytes] = {};
    };
    static_assert(offsetof(Row, dataIndex) == 8, "row object layout");

    struct Title
    {
        const void* const* vtable = nullptr;
        List*              list = nullptr;
        std::uint32_t      configVersion = 0;
        void*              window = nullptr;
        void*              nodes[kMaxTitleColumns] = {};
        char               text[kMaxTitleColumns][kTextBytes] = {};
    };

    struct List
    {
        Param                 param{};
        void*                 rowArray[kMaxVisibleRows] = {};
        Row*                  rows[kMaxVisibleRows] = {};
        Title*                title = nullptr;
        void*                 ui = nullptr;
        void*                 prefab = nullptr;
        void*                 units = nullptr;
        std::uint32_t         unitsHandle = 0;
        std::uint32_t         id = 0;
        std::uint32_t         windowHandle = 0;
        std::uint64_t         name = 0;
        void*                 child = nullptr;
        int                   capacity = 0;
        std::uint32_t         openFlags = kFlagsOpen;
        bool                  closed = false;
        std::atomic<bool>     retired{ false };
        ULONGLONG             deadSince = 0;
        std::mutex            lock;
        RowConfig             rowConfig;
        TitleConfig           titleConfig;
        std::vector<RowText>  data;
        RowText               titleText;
    };

    std::mutex                         g_RegistryLock;
    std::vector<std::shared_ptr<List>> g_Lists;
    std::uint32_t                      g_NextId = 1;

    template <typename Fn>
    Fn VirtualOf(void* object, std::size_t offset)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[offset / sizeof(void*)]);
    }

    bool InFobSEH()
    {
        __try { return g_IsFobMode(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }

    void ResolveNodes(const List& list, void* window, unsigned layoutType, std::uint64_t modelId, const std::uint64_t* nodeIds, int nodeCount, void** nodes, int capacity)
    {
        for (int i = 0; i < capacity; ++i)
            nodes[i] = nullptr;
        void* layout = VirtualOf<GetPrefabLayout_t>(list.prefab, kPrefabGetLayout)(list.prefab, layoutType, window);
        if (!layout)
        {
            LogWarnOnce("PrefabList: the list window has no prefab layout %u, its rows stay empty", layoutType);
            return;
        }
        void* model = VirtualOf<GetByName_t>(list.ui, kUiGetModel)(list.ui, layout, modelId);
        if (!model)
        {
            LogWarnOnce("PrefabList: the model of the %s layout was not found, its rows stay empty", layoutType == kTitleLayoutType ? "title" : "row");
            return;
        }
        for (int i = 0; i < nodeCount && i < capacity; ++i)
        {
            nodes[i] = VirtualOf<GetByName_t>(list.ui, kUiGetTextNode)(list.ui, model, nodeIds[i]);
            if (!nodes[i])
                LogWarnOnce("PrefabList: text node %d of the %s layout was not found", i + 1, layoutType == kTitleLayoutType ? "title" : "row");
        }
    }

    void ShowTexts(const List& list, void* const* nodes, int nodeCount, const char* texts)
    {
        auto setText = VirtualOf<SetText_t>(list.ui, kUiSetText);
        for (int i = 0; i < nodeCount; ++i)
            if (nodes[i])
                setText(list.ui, nodes[i], list.units, texts + i * kTextBytes, 1);
    }

    const RowText kNoText;

    void CopyText(char (&buffer)[kTextBytes], const RowText& source, std::size_t column)
    {
        const std::string empty;
        const std::string& text = column < source.size() ? source[column] : empty;
        const std::size_t length = text.size() < kTextBytes - 1 ? text.size() : kTextBytes - 1;
        std::memcpy(buffer, text.data(), length);
        buffer[length] = '\0';
    }

    // Start runs on an engine worker: it only copies strings already stored and calls the UI object, never Lua
    void __fastcall RowStart(Row* row, void* window)
    {
        List* list = row->list;
        if (!list || list->retired.load(std::memory_order_acquire))
            return;

        RowConfig config;
        std::uint32_t index = 0;
        {
            std::lock_guard<std::mutex> lock(list->lock);
            config = list->rowConfig;
            const std::uint32_t count = static_cast<std::uint32_t>(list->data.size());
            const std::uint32_t engineIndex = row->dataIndex;
            index = engineIndex != kUnsetIndex && engineIndex < count ? engineIndex
                  : count ? (list->param.top + static_cast<std::uint32_t>(row->slot)) % count : 0;
            const RowText& source = index < count ? list->data[index] : kNoText;
            for (int column = 0; column < config.nodeCount; ++column)
                CopyText(row->text[column], source, static_cast<std::size_t>(column));
        }
        if (row->window != window || row->configVersion != config.version)
        {
            ResolveNodes(*list, window, kRecordLayoutType, config.model, config.nodes, config.nodeCount, row->nodes, kMaxColumns);
            row->window = window;
            row->configVersion = config.version;
        }
        ShowTexts(*list, row->nodes, config.nodeCount, row->text[0]);
    }

    void __fastcall TitleStart(Title* title, void* window)
    {
        List* list = title->list;
        if (!list || list->retired.load(std::memory_order_acquire))
            return;

        TitleConfig config;
        {
            std::lock_guard<std::mutex> lock(list->lock);
            config = list->titleConfig;
            for (int column = 0; column < config.nodeCount; ++column)
                CopyText(title->text[column], list->titleText, static_cast<std::size_t>(column));
        }
        if (title->window != window || title->configVersion != config.version)
        {
            ResolveNodes(*list, window, kTitleLayoutType, config.model, config.nodes, config.nodeCount, title->nodes, kMaxTitleColumns);
            title->window = window;
            title->configVersion = config.version;
        }
        ShowTexts(*list, title->nodes, config.nodeCount, title->text[0]);
    }

    void* __fastcall NoDestroy(void* self, unsigned) { return self; }   // the list object owns these, the engine never frees them
    bool __fastcall AlwaysTrue(void*) { return true; }

    // call func interface: dtor, Start(window), then the default-true slots; the title one has only the first three
    const void* const kRowVtable[5] = {
        reinterpret_cast<const void*>(&NoDestroy), reinterpret_cast<const void*>(&RowStart), reinterpret_cast<const void*>(&AlwaysTrue),
        reinterpret_cast<const void*>(&AlwaysTrue), reinterpret_cast<const void*>(&AlwaysTrue),
    };
    const void* const kTitleVtable[3] = {
        reinterpret_cast<const void*>(&NoDestroy), reinterpret_cast<const void*>(&TitleStart), reinterpret_cast<const void*>(&AlwaysTrue),
    };

    bool FindContextSEH(void*& ui, void*& prefab)
    {
        __try
        {
            auto getTable = reinterpret_cast<GetQuarkTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
            if (!getTable)
                return false;
            std::uint8_t* table = getTable();
            void* root = table ? *reinterpret_cast<void**>(table + kQuarkTableHolder) : nullptr;
            void* source = root ? *reinterpret_cast<void**>(static_cast<std::uint8_t*>(root) + kHolderContextSource) : nullptr;
            if (!source)
                return false;
            auto getContext = reinterpret_cast<void*(__fastcall*)(void*)>((*reinterpret_cast<void***>(source))[0]);
            auto* context = static_cast<std::uint8_t*>(getContext(source));
            if (!context)
                return false;
            ui = *reinterpret_cast<void**>(context + kContextUi);
            prefab = *reinterpret_cast<void**>(context + kContextPrefab);
            return ui && prefab;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool PrefabSystemMatchesSEH(void* prefab)
    {
        __try
        {
            void** vtable = *reinterpret_cast<void***>(prefab);
            return vtable[kPrefabGetLayout / sizeof(void*)] == ResolveGameAddress(gAddr.PrefabSystem_GetPrefabLayout)
                && vtable[kPrefabCreateCursorFunc / sizeof(void*)] == ResolveGameAddress(gAddr.PrefabSystem_CreateCursorCallFunc)
                && vtable[kPrefabCreateScrollbarFunc / sizeof(void*)] == ResolveGameAddress(gAddr.PrefabSystem_CreateScrollbarCallFunc);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool CreateCallFuncsSEH(void* prefab, void*& cursorFunc, void*& scrollFunc)
    {
        __try
        {
            cursorFunc = VirtualOf<CreateFunc_t>(prefab, kPrefabCreateCursorFunc)(prefab);
            scrollFunc = VirtualOf<CreateFunc_t>(prefab, kPrefabCreateScrollbarFunc)(prefab);
            return cursorFunc && scrollFunc;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void DestroyEngineObjectSEH(void* object)
    {
        if (!object)
            return;
        __try { VirtualOf<Destroy_t>(object, 0)(object, 1); }   // slot 0 is the deleting destructor
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void* FindChildSEH(void* window, std::uint64_t id)
    {
        __try { return VirtualOf<GetChild_t>(window, kWindowGetChild)(window, id); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    std::uint32_t ButtonNumber(std::uint64_t hash)
    {
        std::uint32_t number = static_cast<std::uint32_t>(hash);   // with mode 0 the game returns the low 32 bits unchanged
        void* fn = UixUtilityControl::Method(UixUtilityControl::dev::GetButtonNumber);
        void** util = UixUtilityControl::Utility();
        if (!fn || !util)
            return number;
        auto call = [&]() { number = static_cast<std::uint32_t>(reinterpret_cast<GetButton_t>(fn)(util, hash, 0)); };
        UixUtilityControl::Guarded(call);
        return number;
    }

    bool TaggedTextIsSafe(const std::string& text)
    {
        const char* problem = nullptr;
        std::size_t run = 0;
        for (std::size_t i = 0; i < text.size() && !problem; )
        {
            if (text[i] == '<' && i + 2 < text.size() && (text[i + 1] == 'I' || text[i + 1] == 'i') && text[i + 2] == '=')
            {
                const char kind = i + 3 < text.size() ? text[i + 3] : '\0';
                const std::size_t end = text.find('>', i);
                if ((kind != 'G' && kind != 'C' && kind != 'R' && kind != 'r') || i + 4 >= text.size() || text[i + 4] != '=' || end == std::string::npos)
                {
                    problem = "a tag is not <I=G=name>, <I=C=name> or <I=R=text>";
                    break;
                }
                if (kind == 'G')
                {
                    const std::size_t amp = text.find('&', i + 5);
                    const std::size_t nameEnd = amp < end ? amp : end;
                    if (nameEnd == i + 5 || nameEnd - (i + 5) >= 0x40)   // the game's text parser kills the process on an icon name this long
                        problem = "an icon name is empty or 64 bytes or longer";
                }
                run = 0;
                i = end + 1;
                continue;
            }
            if (++run > 0xFFF)
                problem = "a run without tags is longer than 4095 bytes";
            ++i;
        }
        if (problem)
            LogWarnOnce("PrefabList: row text refused, %s", problem);
        return problem == nullptr;
    }

    std::string CleanText(std::string text)
    {
        if (text.size() >= kTextBytes)
        {
            std::size_t cut = kTextBytes - 1;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
                --cut;
            text.resize(cut);
        }
        return TaggedTextIsSafe(text) ? text : std::string();
    }

    void CleanRows(std::vector<RowText>& rows, int count)
    {
        if (count < 0 || count > kMaxRows)
            count = kMaxRows;
        rows.resize(static_cast<std::size_t>(count));
        for (RowText& row : rows)
        {
            if (row.size() > static_cast<std::size_t>(kMaxColumns))
                row.resize(kMaxColumns);
            for (std::string& text : row)
                text = CleanText(std::move(text));
        }
    }

    std::uint32_t AbsoluteRow(const List& list)
    {
        const std::uint32_t count = list.param.count;
        return count ? (list.param.cursor + list.param.top) % count : 0;
    }

    // the same top/cursor split as the Missions list page when it opens its list
    void Place(List& list, int count, int absolute, int topHint)
    {
        const int capacity = list.capacity;
        const int visible = count < capacity ? count : capacity;
        if (absolute > count - 1)
            absolute = count - 1;
        if (absolute < 0)
            absolute = 0;
        const int span = visible > 0 ? visible : 1;
        int top = 0, cursor = 0;
        if (topHint >= 0 && topHint <= count - visible && absolute >= topHint && absolute < topHint + span)
        {
            top = topHint;
            cursor = absolute - topHint;
        }
        else if (count < visible + absolute)
        {
            if (count < visible)
                cursor = absolute;
            else
            {
                top = count - visible;
                cursor = absolute - top;
            }
        }
        else
            top = absolute;
        list.param.count = static_cast<std::uint32_t>(count);
        list.param.visible = static_cast<std::uint32_t>(visible);
        list.param.top = static_cast<std::uint32_t>(top);
        list.param.cursor = static_cast<std::uint32_t>(cursor);
        list.param.capacityMinusOne = static_cast<std::uint32_t>(capacity - 1);
    }

    void ReleaseResources(List& list)
    {
        list.retired.store(true, std::memory_order_release);
        list.param.flags = kFlagsClosed;
        list.param.rows = nullptr;
        list.param.title = nullptr;
        for (Row*& row : list.rows)
        {
            delete row;
            row = nullptr;
        }
        for (void*& slot : list.rowArray)
            slot = nullptr;
        delete list.title;
        list.title = nullptr;
        DestroyEngineObjectSEH(list.param.cursorFunc);
        DestroyEngineObjectSEH(list.param.scrollFunc);
        list.param.cursorFunc = nullptr;
        list.param.scrollFunc = nullptr;
        if (list.unitsHandle)
            UixUtilityControl::DeleteTextUnit(list.unitsHandle);
        list.unitsHandle = 0;
        list.units = nullptr;
    }

    void EnsureRows(List& list, int capacity)
    {
        for (int slot = 0; slot < capacity; ++slot)
        {
            if (list.rows[slot])
                continue;
            auto* row = new Row();
            row->vtable = kRowVtable;
            row->list = &list;
            row->slot = slot;
            list.rows[slot] = row;
            list.rowArray[slot] = row;
        }
    }

    std::shared_ptr<List> CreateList(const PrefabListControl::SetupRequest& request, void* child)
    {
        auto list = std::make_shared<List>();
        list->windowHandle = request.window;
        list->name = request.list;
        list->child = child;
        if (!FindContextSEH(list->ui, list->prefab))
        {
            LogError("PrefabList: the UI context was not found, the list is not set up");
            return nullptr;
        }
        list->ui = UixUtilityControl::Utility();   // the same UixUtility the page code calls, and one whose vtable is checked
        if (!list->ui)
            return nullptr;
        if (!PrefabSystemMatchesSEH(list->prefab))
        {
            LogError("PrefabList: the prefab system does not match this build, the list is not set up");
            return nullptr;
        }
        if (!CreateCallFuncsSEH(list->prefab, list->param.cursorFunc, list->param.scrollFunc))
        {
            LogError("PrefabList: the cursor and scrollbar objects could not be made, the list is not set up");
            ReleaseResources(*list);
            return nullptr;
        }
        list->unitsHandle = UixUtilityControl::CreateTextUnit(1);
        Object units;
        if (!UixUtilityControl::Lookup(list->unitsHandle, units))
        {
            LogError("PrefabList: the text unit could not be made, the list is not set up");
            ReleaseResources(*list);
            return nullptr;
        }
        list->units = units.ptr;

        list->title = new Title();
        list->title->vtable = kTitleVtable;
        list->title->list = list.get();

        list->param.kind = 1;
        for (int i = 0; i < 6; ++i)
            list->param.buttons[i] = ButtonNumber(kButtonHashes[i]);
        list->param.rows = list->rowArray;
        list->param.title = list->title;
        list->param.flags = kFlagsClosed;
        return list;
    }

    std::shared_ptr<List> FindById(std::uint32_t id)
    {
        std::lock_guard<std::mutex> lock(g_RegistryLock);
        for (const auto& list : g_Lists)
            if (list->id == id && !list->retired.load(std::memory_order_acquire))
                return list;
        return nullptr;
    }

    void SendListTrigger(const std::shared_ptr<List>& list, void* child)
    {
        UixUtilityControl::Queue(list->windowHandle, [list, child](const Object&) {
            if (list->retired.load(std::memory_order_acquire))
                return;
            void* fn = UixUtilityControl::Method(UixUtilityControl::dev::SendTrigger);
            void** util = UixUtilityControl::Utility();
            if (fn && util)
                reinterpret_cast<SendTrigger_t>(fn)(util, child, kListTrigger, reinterpret_cast<std::uint64_t>(&list->param));
        });
    }
}

bool PrefabListControl::IsAvailable()
{
    return g_Installed.load(std::memory_order_acquire) && g_IsFobMode && UixUtilityControl::IsSupportedOnThisBuild()
        && !MissionCodeGuard::ShouldBypassHooks() && !InFobSEH();
}

void PrefabListControl::Sweep()
{
    std::vector<std::shared_ptr<List>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_RegistryLock);
        snapshot = g_Lists;
    }
    const ULONGLONG now = GetTickCount64();
    std::vector<std::shared_ptr<List>> toFree;
    for (const auto& list : snapshot)
    {
        Object window;
        if (UixUtilityControl::Lookup(list->windowHandle, window) && window.kind == Kind::Window)
            continue;
        if (list->deadSince == 0)
        {
            list->deadSince = now;
            list->retired.store(true, std::memory_order_release);
            list->param.flags = kFlagsClosed;
        }
        else if (now - list->deadSince >= kFreeDelayMs)
            toFree.push_back(list);
    }
    for (const auto& list : toFree)
        ReleaseResources(*list);
    if (toFree.empty())
        return;
    std::lock_guard<std::mutex> lock(g_RegistryLock);
    for (const auto& list : toFree)
        for (auto it = g_Lists.begin(); it != g_Lists.end(); ++it)
            if (*it == list)
            {
                g_Lists.erase(it);
                break;
            }
}

std::uint32_t PrefabListControl::Setup(const SetupRequest& request)
{
    if (!IsAvailable())
        return 0;
    Sweep();

    Object window;
    if (!UixUtilityControl::Lookup(request.window, window) || window.kind != Kind::Window)
    {
        LogWarnOnce("V_UixUtility.SetupPrefabList: window is not a live window handle");
        return 0;
    }
    if (request.visible < 1 || request.visible > kMaxVisibleRows)
    {
        LogWarnOnce("V_UixUtility.SetupPrefabList: visible must be 1 to %d", kMaxVisibleRows);
        return 0;
    }
    if (!request.rowModel || request.rowTextNodes.empty() || request.rowTextNodes.size() > static_cast<std::size_t>(kMaxColumns))
    {
        LogWarnOnce("V_UixUtility.SetupPrefabList: rowModel and rowText (1 to %d text nodes) are required", kMaxColumns);
        return 0;
    }
    if (!request.title.empty() && (!request.titleModel || request.titleTextNodes.empty()))
        LogWarnOnce("V_UixUtility.SetupPrefabList: title needs titleModel and titleText, the title stays empty");

    void* child = FindChildSEH(window.ptr, request.list & kId48);
    if (!child)
    {
        LogWarnOnce("V_UixUtility.SetupPrefabList: the window has no child window with that list name");
        return 0;
    }

    std::shared_ptr<List> list;
    {
        std::lock_guard<std::mutex> lock(g_RegistryLock);
        for (const auto& known : g_Lists)
            if (known->windowHandle == request.window && known->name == request.list && !known->retired.load(std::memory_order_acquire))
            {
                list = known;
                break;
            }
    }
    const bool isNew = !list;
    if (isNew)
    {
        list = CreateList(request, child);
        if (!list)
            return 0;
    }
    EnsureRows(*list, request.visible);

    std::vector<RowText> rows = request.rows;
    const int count = request.count >= 0 ? request.count : static_cast<int>(rows.size());
    CleanRows(rows, count);
    RowText titleText = request.title;
    if (titleText.size() > static_cast<std::size_t>(kMaxTitleColumns))
        titleText.resize(kMaxTitleColumns);
    for (std::string& text : titleText)
        text = CleanText(std::move(text));

    {
        std::lock_guard<std::mutex> lock(list->lock);
        list->child = child;
        list->capacity = request.visible;
        list->rowConfig.version++;
        list->rowConfig.model = request.rowModel;
        list->rowConfig.nodeCount = static_cast<int>(request.rowTextNodes.size());
        for (std::size_t i = 0; i < request.rowTextNodes.size(); ++i)
            list->rowConfig.nodes[i] = request.rowTextNodes[i];
        list->titleConfig.version++;
        list->titleConfig.model = request.titleModel;
        const std::size_t titleNodes = request.titleTextNodes.size() < static_cast<std::size_t>(kMaxTitleColumns) ? request.titleTextNodes.size() : static_cast<std::size_t>(kMaxTitleColumns);
        list->titleConfig.nodeCount = request.titleModel ? static_cast<int>(titleNodes) : 0;
        for (int i = 0; i < list->titleConfig.nodeCount; ++i)
            list->titleConfig.nodes[i] = request.titleTextNodes[static_cast<std::size_t>(i)];
        list->titleText = std::move(titleText);
        list->data = std::move(rows);
        list->openFlags = request.flags ? (request.flags | 1u) : kFlagsOpen;
        list->closed = false;
        Place(*list, static_cast<int>(list->data.size()), request.cursor - 1, request.top > 0 ? request.top - 1 : -1);
        list->param.flags = list->openFlags;
    }

    if (isNew)
    {
        std::lock_guard<std::mutex> lock(g_RegistryLock);
        list->id = g_NextId++;
        g_Lists.push_back(list);
    }
    SendListTrigger(list, child);
    return list->id;
}

bool PrefabListControl::SetRows(std::uint32_t id, std::vector<RowText> rows)
{
    if (!IsAvailable())
        return false;
    Sweep();
    const std::shared_ptr<List> list = FindById(id);
    if (!list)
        return false;
    CleanRows(rows, static_cast<int>(rows.size()));
    std::lock_guard<std::mutex> lock(list->lock);
    const int absolute = static_cast<int>(AbsoluteRow(*list));
    const int topHint = static_cast<int>(list->param.top);
    list->data = std::move(rows);
    Place(*list, static_cast<int>(list->data.size()), absolute, topHint);
    if (!list->closed)
        list->param.flags = list->openFlags;   // bit 0 asks the list to redo its rows
    return true;
}

bool PrefabListControl::GetCursor(std::uint32_t id, int& absolute, int& top)
{
    if (!IsAvailable())
        return false;
    Sweep();
    const std::shared_ptr<List> list = FindById(id);
    if (!list)
        return false;
    const std::uint32_t count = list->param.count;
    if (!count)
    {
        absolute = 0;
        top = 0;
        return true;
    }
    absolute = static_cast<int>(AbsoluteRow(*list)) + 1;
    top = static_cast<int>(list->param.top % count) + 1;
    return true;
}

bool PrefabListControl::Close(std::uint32_t id)
{
    if (!IsAvailable())
        return false;
    Sweep();
    const std::shared_ptr<List> list = FindById(id);
    if (!list)
        return false;
    std::lock_guard<std::mutex> lock(list->lock);
    list->closed = true;
    list->param.flags = kFlagsClosed;
    return true;
}

bool Install_PrefabListControl()
{
    g_IsFobMode = reinterpret_cast<IsFobMode_t>(ResolveGameAddress(gAddr.Barrier_IsFobMode));
    if (!g_IsFobMode || !gAddr.PrefabSystem_GetPrefabLayout || !gAddr.PrefabSystem_CreateCursorCallFunc || !gAddr.PrefabSystem_CreateScrollbarCallFunc
        || !gAddr.GetQuarkSystemTable)
    {
        LogError("PrefabList: addresses missing for %s, V_UixUtility.SetupPrefabList does nothing", GetGameBuildName(gGameBuild));
        return false;
    }
    g_Installed.store(true, std::memory_order_release);
    return true;
}

bool Uninstall_PrefabListControl()
{
    g_Installed.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(g_RegistryLock);
    for (const auto& list : g_Lists)
    {
        list->retired.store(true, std::memory_order_release);
        list->param.flags = kFlagsClosed;
    }
    static auto* kept = new std::vector<std::shared_ptr<List>>();   // the engine may still poll a list's parameter block, so the memory stays until the process ends
    kept->insert(kept->end(), g_Lists.begin(), g_Lists.end());
    g_Lists.clear();
    return true;
}
