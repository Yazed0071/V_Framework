#include "pch.h"

#include "FaceFovaWiden.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaApi.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace outfit
{
    namespace
    {
        constexpr std::size_t kRows = 900;
        constexpr std::size_t kStride = 40;
        constexpr std::uint16_t kEngineNone = 32767;
        constexpr std::uint16_t kWideNone = 0xFFFF;
        constexpr std::uint64_t kValid = 1ull << 63;
        constexpr std::uint32_t kWidths[] = { 256, 1024, 64, 64 };
        constexpr const char* kColumns[] = {
            "faceFova", "faceDecoFova", "hairFova", "hairDecoFova", "bodyFova"
        };

        struct WideCell
        {
            std::atomic<std::uint64_t> indices{ 0 };
            std::atomic<std::uint64_t> fingerprint{ 0 };
            std::uint8_t padding[24]{};
        };

        struct WideTable
        {
            const std::uint8_t* rows = nullptr;
            std::array<WideCell, kRows> cells{};
        };

        static_assert(sizeof(WideCell) == kStride);
        static_assert(offsetof(WideCell, indices) == 0);
        static_assert(offsetof(WideCell, fingerprint) == 8);
        static_assert(offsetof(WideTable, cells) == 8);
        static_assert(std::atomic<WideTable*>::is_always_lock_free);
        static_assert(sizeof(std::atomic<WideTable*>) == sizeof(WideTable*));
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

        std::atomic<WideTable*> g_Table{ nullptr };
        constexpr unsigned kInactive = 0;
        constexpr unsigned kActive = 1;
        constexpr unsigned kArming = 2;
        constexpr unsigned kFailed = 3;
        std::atomic<unsigned> g_Ready{ kInactive };
        std::atomic<bool> g_FileGuardSeen{ false };
        std::atomic<bool> g_LateInstall{ false };
        std::mutex g_TableMutex;
        std::mutex g_InstallMutex;
        bool g_InstallAttempted = false;
        std::array<NativeCallPatch, 11> g_Readers{};

        using DefinitionWriter = int(__fastcall*)(void*, lua_State*);
        using FileTableWriter = void(__fastcall*)(void*, lua_State*);
        DefinitionWriter g_DefinitionWriter = nullptr;
        FileTableWriter g_FileTableWriter = nullptr;

        bool FaceAssetsAlreadyLoaded();

        bool NativeRegistrationReady()
        {
            unsigned state = g_Ready.load(std::memory_order_acquire);
            if (state == kArming
                && g_Ready.compare_exchange_strong(state, kFailed, std::memory_order_acq_rel))
            {
                g_LateInstall.store(true, std::memory_order_release);
                return false;
            }
            return state == kActive;
        }

        std::uint64_t Fingerprint(const void* row)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, static_cast<const std::uint8_t*>(row) + 0x1c, sizeof(value));
            return value & 0x3fffffffu;
        }

        WideCell* CellFor(WideTable* table, const void* row)
        {
            if (!table || !row)
                return nullptr;
            const auto address = reinterpret_cast<std::uintptr_t>(row);
            const auto base = reinterpret_cast<std::uintptr_t>(table->rows);
            if (address < base || address - base >= kRows * kStride
                || (address - base) % kStride != 0)
                return nullptr;
            return &table->cells[(address - base) / kStride];
        }

        bool PublishWideFaceFova(void* row, const std::uint16_t (&indices)[4])
        {
            auto cell = CellFor(g_Table.load(std::memory_order_acquire), row);
            if (!cell)
                return false;
            std::uint64_t packed = 0;
            for (unsigned column = 0; column < 4; ++column)
                packed |= static_cast<std::uint64_t>(indices[column]) << (column * 16);
            cell->fingerprint.store(0, std::memory_order_release);
            cell->indices.store(packed, std::memory_order_release);
            cell->fingerprint.store(Fingerprint(row) | kValid, std::memory_order_release);
            return true;
        }

        struct PendingRow
        {
            bool touched = false;
            bool wide = false;
            std::uint16_t indices[4]{};
        };

        bool LuaAvailable()
        {
            return ResolveLuaApi() && g_lua_gettop && g_lua_settop && g_lua_getfield
                && g_lua_rawgeti && g_lua_type && g_lua_isnumber && g_lua_tonumber
                && g_lua_objlen && g_lua_createtable && g_lua_pushvalue
                && g_lua_pushnumber && g_lua_pushstring && g_lua_rawset;
        }

        bool LuaUnsigned(lua_State* state, int index, std::uint32_t maximum,
                         std::uint32_t& value)
        {
            if (!g_lua_isnumber(state, index))
                return false;
            const double number = g_lua_tonumber(state, index);
            if (!std::isfinite(number) || number < 0 || number > maximum
                || std::floor(number) != number)
                return false;
            value = static_cast<std::uint32_t>(number);
            return true;
        }

        void PushKey(lua_State* state, const char* key)
        {
            g_lua_pushstring(state, const_cast<char*>(key));
        }

        void GetField(lua_State* state, int index, const char* key)
        {
            g_lua_getfield(state, index, const_cast<char*>(key));
        }

        bool ValidWideAsset(const std::uint8_t* impl, int column, std::uint32_t index)
        {
            const auto registered = reinterpret_cast<const std::uint32_t*>(impl + 0xf8);
            const auto bases = reinterpret_cast<const std::uint32_t*>(impl + 0xe4);
            const std::uint32_t total = *reinterpret_cast<const std::uint32_t*>(impl + 0x608);
            const std::uint32_t end = bases[column + 1];
            const auto files = *reinterpret_cast<const std::uint8_t* const*>(impl + 0x88);
            if (index >= kWideNone || index >= registered[column] || !files
                || bases[column] > end || end > total || index >= end - bases[column])
                return false;
            const auto entry = files + (static_cast<std::size_t>(bases[column]) + index) * 16;
            return *reinterpret_cast<const std::uint64_t*>(entry + 8) != 0;
        }

        bool MakeDefinitionShadow(std::uint8_t* impl, lua_State* state, int argument,
                                  std::array<PendingRow, kRows>& pending)
        {
            GetField(state, argument, "table");
            if (g_lua_type(state, -1) != LUA_TTABLE)
            {
                g_lua_settop(state, argument);
                return false;
            }
            const int sourceArray = g_lua_gettop(state);
            const std::size_t count = g_lua_objlen(state, sourceArray);
            if (count > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                g_lua_settop(state, argument);
                return false;
            }

            g_lua_createtable(state, 0, 2);
            const int shadow = g_lua_gettop(state);
            PushKey(state, "uiTexBasePath");
            GetField(state, argument, "uiTexBasePath");
            g_lua_rawset(state, shadow);
            PushKey(state, "table");
            g_lua_createtable(state, static_cast<int>(count), 0);
            const int destinationArray = g_lua_gettop(state);

            for (std::size_t i = 0; i < count; ++i)
            {
                g_lua_rawgeti(state, sourceArray, static_cast<int>(i + 1));
                const int sourceRow = g_lua_gettop(state);
                if (g_lua_type(state, sourceRow) != LUA_TTABLE)
                {
                    LogWarn("FaceFova: definition row %zu is not a table, face definitions rejected", i + 1);
                    g_lua_settop(state, argument);
                    return false;
                }
                g_lua_rawgeti(state, sourceRow, 1);
                std::uint32_t faceId = 0;
                const bool validId = LuaUnsigned(state, -1, 899, faceId);
                g_lua_settop(state, sourceRow);
                PendingRow captured{};
                captured.touched = validId;

                for (int column = 0; column < 4; ++column)
                {
                    g_lua_rawgeti(state, sourceRow, column + 5);
                    std::uint32_t value = 0;
                    const bool validIndex = LuaUnsigned(state, -1, kWideNone - 1, value);
                    g_lua_settop(state, sourceRow);
                    if (!validIndex)
                    {
                        LogWarn("FaceFova: definition row %zu: invalid %s index, face definitions rejected",
                            i + 1, kColumns[column]);
                        g_lua_settop(state, argument);
                        return false;
                    }
                    captured.indices[column] = value == kEngineNone
                        ? kWideNone : static_cast<std::uint16_t>(value);
                    if (value != kEngineNone && value >= kWidths[column])
                    {
                        if (!validId || !ValidWideAsset(impl, column, value))
                        {
                            LogWarn("FaceFova: face %u: %s index %u has no registered asset, face definitions rejected",
                                faceId, kColumns[column], value);
                            g_lua_settop(state, argument);
                            return false;
                        }
                        captured.wide = true;
                    }
                }

                g_lua_createtable(state, 17, 0);
                const int destinationRow = g_lua_gettop(state);
                for (int field = 1; field <= 17; ++field)
                {
                    g_lua_pushnumber(state, field);
                    if (field >= 5 && field <= 8 && captured.indices[field - 5] != kWideNone
                        && captured.indices[field - 5] >= kWidths[field - 5])
                        g_lua_pushnumber(state, 0);
                    else
                        g_lua_rawgeti(state, sourceRow, field);
                    g_lua_rawset(state, destinationRow);
                }
                g_lua_rawgeti(state, destinationRow, 12);
                if (g_lua_type(state, -1) != LUA_TSTRING)
                {
                    LogWarn("FaceFova: definition row %zu: portrait name is not a string, face definitions rejected",
                        i + 1);
                    g_lua_settop(state, argument);
                    return false;
                }
                g_lua_settop(state, destinationRow);
                g_lua_pushnumber(state, static_cast<double>(i + 1));
                g_lua_pushvalue(state, destinationRow);
                g_lua_rawset(state, destinationArray);
                g_lua_settop(state, destinationArray);
                if (validId)
                    pending[faceId] = captured;
            }
            g_lua_rawset(state, shadow);
            return true;
        }

        int __fastcall WriteDefinitions(void* object, lua_State* state)
        {
            if (!NativeRegistrationReady() || !LuaAvailable())
                return g_DefinitionWriter(object, state);
            const int top = g_lua_gettop(state);
            if (top < 1 || g_lua_type(state, top) != LUA_TTABLE)
                return g_DefinitionWriter(object, state);
            GetField(state, top, "table");
            const bool hasDefinitions = g_lua_type(state, -1) == LUA_TTABLE;
            g_lua_settop(state, top);
            if (!hasDefinitions)
                return g_DefinitionWriter(object, state);
            auto impl = static_cast<std::uint8_t*>(object);
            auto rows = *reinterpret_cast<std::uint8_t**>(impl + 0x90);
            if (!rows || !ResetWideFaceFova(rows))
            {
                LogError("FaceFova: wide index table unavailable, face definitions rejected");
                return 0;
            }
            std::array<PendingRow, kRows> pending{};
            if (!MakeDefinitionShadow(impl, state, top, pending))
                return 0;
            for (std::size_t id = 0; id < kRows; ++id)
                if (pending[id].touched)
                    ClearWideFaceFova(rows + id * kStride);

            const int result = g_DefinitionWriter(object, state);
            g_lua_settop(state, top);
            if (*reinterpret_cast<std::uint8_t**>(impl + 0x90) != rows)
            {
                ResetWideFaceFova(*reinterpret_cast<std::uint8_t**>(impl + 0x90));
                LogWarn("FaceFova: face table moved during registration, wide indices dropped");
                return result;
            }
            for (std::size_t id = 0; id < kRows; ++id)
            {
                auto& row = pending[id];
                if (!row.touched || !row.wide)
                    continue;
                const auto native = rows + id * kStride;
                const auto flags = *reinterpret_cast<const std::uint32_t*>(native + 0x20);
                for (int column = 0; column < 4; ++column)
                    if ((flags & (0x20000u << column)) == 0)
                        row.indices[column] = kWideNone;
                if (!PublishWideFaceFova(native, row.indices))
                    LogWarn("FaceFova: face %zu wide indices not stored, vanilla limits apply", id);
            }
            return result;
        }

        bool CheckFileTable(void* object, lua_State* state)
        {
            const int top = g_lua_gettop(state);
            if (top < 1 || g_lua_type(state, top) != LUA_TTABLE)
                return false;
            auto impl = static_cast<const std::uint8_t*>(object);
            const auto oldTable = *reinterpret_cast<void* const*>(impl + 0x88);
            const auto bases = reinterpret_cast<const std::uint32_t*>(impl + 0xe4);
            const auto total = *reinterpret_cast<const std::uint32_t*>(impl + 0x608);
            std::uint64_t nextBase = 0;
            for (int column = 0; column < 5; ++column)
            {
                GetField(state, top, kColumns[column]);
                std::uint32_t count = 0;
                std::uint32_t maximum = 0;
                bool valid = true;
                if (g_lua_type(state, -1) == LUA_TTABLE)
                {
                    const int source = g_lua_gettop(state);
                    GetField(state, source, "table");
                    if (g_lua_type(state, -1) == LUA_TTABLE)
                    {
                        const auto length = g_lua_objlen(state, -1);
                        if (length > (std::numeric_limits<std::uint32_t>::max)())
                            valid = false;
                        else
                            count = static_cast<std::uint32_t>(length);
                    }
                    g_lua_settop(state, source);
                    GetField(state, source, "maxCount");
                    if (g_lua_type(state, -1) != LUA_TNIL)
                        valid = valid && LuaUnsigned(state, -1, 0x07ffffffu, maximum);
                }
                g_lua_settop(state, top);
                if (!valid || count > maximum || (column < 4 && maximum > kWideNone)
                    || nextBase + maximum > 0x07ffffffu)
                {
                    LogWarn("FaceFova: file table %s count/maxCount invalid, file table rejected",
                        kColumns[column]);
                    return false;
                }
                if (oldTable)
                {
                    const std::uint32_t end = column == 4 ? total : bases[column + 1];
                    if (end < bases[column] || bases[column] != nextBase
                        || maximum != end - bases[column])
                    {
                        LogWarn("FaceFova: file table %s maxCount changed after allocation, rejected; restart the game",
                            kColumns[column]);
                        return false;
                    }
                }
                nextBase += maximum;
            }
            return true;
        }

        void __fastcall WriteFileTable(void* object, lua_State* state)
        {
            if (!NativeRegistrationReady())
            {
                g_FileTableWriter(object, state);
                return;
            }
            const auto impl = static_cast<const std::uint8_t*>(object);
            if (!g_FileGuardSeen.load(std::memory_order_acquire)
                && *reinterpret_cast<void* const*>(impl + 0x88))
                g_LateInstall.store(true, std::memory_order_release);
            if (!LuaAvailable() || !CheckFileTable(object, state))
            {
                LogWarn("FaceFova: file table update not applied");
                return;
            }
            g_FileGuardSeen.store(true, std::memory_order_release);
            g_FileTableWriter(object, state);
        }

        enum Register : unsigned
        {
            Rax = 0, Rcx = 1, Rdx = 2, Rbx = 3, Rbp = 5,
            Rsi = 6, Rdi = 7, R8 = 8, R9 = 9, R10 = 10, R11 = 11, R15 = 15
        };

        struct Reader
        {
            std::uintptr_t address;
            Register output;
            Register row;
            Register column;
            int fixedColumn;
            bool indexed;
            bool indirectId;
        };

        struct Code
        {
            std::vector<std::uint8_t> bytes;

            void Emit(std::initializer_list<std::uint8_t> values)
            {
                bytes.insert(bytes.end(), values);
            }

            template<class T> void Value(T value)
            {
                const auto data = reinterpret_cast<const std::uint8_t*>(&value);
                bytes.insert(bytes.end(), data, data + sizeof(value));
            }

            void Rex(bool wide, unsigned reg, unsigned base)
            {
                const auto rex = static_cast<std::uint8_t>(0x40 | (wide ? 8 : 0)
                    | ((reg >> 3) << 2) | (base >> 3));
                if (rex != 0x40)
                    Emit({ rex });
            }

            void Move(unsigned destination, unsigned source, bool wide = true)
            {
                Rex(wide, destination, source);
                Emit({ 0x8b, static_cast<std::uint8_t>(0xc0 | ((destination & 7) << 3) | (source & 7)) });
            }

            void Memory(std::uint8_t opcode, unsigned reg, unsigned base, int offset = 0,
                        bool wide = true)
            {
                Rex(wide, reg, base);
                const bool byteOffset = offset >= -128 && offset <= 127;
                const auto mode = static_cast<std::uint8_t>(byteOffset ? 0x40 : 0x80);
                Emit({ opcode, static_cast<std::uint8_t>(mode | ((reg & 7) << 3) | (base & 7)) });
                if ((base & 7) == 4)
                    Emit({ 0x24 });
                if (byteOffset)
                    Value(static_cast<std::int8_t>(offset));
                else
                    Value(static_cast<std::int32_t>(offset));
            }

            std::size_t Jump(std::uint8_t condition)
            {
                Emit({ 0x0f, condition });
                const auto at = bytes.size();
                Value<std::int32_t>(0);
                return at;
            }

            void Bind(std::size_t at, std::size_t destination)
            {
                const auto displacement = static_cast<std::int32_t>(destination - at - 4);
                std::memcpy(bytes.data() + at, &displacement, sizeof(displacement));
            }

            void Shift(unsigned destination, unsigned bits)
            {
                Rex(true, 0, destination);
                Emit({ 0xc1, static_cast<std::uint8_t>(0xe8 | (destination & 7)),
                       static_cast<std::uint8_t>(bits) });
            }

            void ColumnCompare(unsigned reg, unsigned value)
            {
                Rex(false, 0, reg);
                Emit({ 0x83, static_cast<std::uint8_t>(0xf8 | (reg & 7)),
                       static_cast<std::uint8_t>(value) });
            }
        };

        bool CheckReaderUnwind(std::uint8_t* storage, RUNTIME_FUNCTION* functions,
                               std::size_t epilogue, std::size_t leafStart, std::size_t codeEnd)
        {
            const std::size_t offsets[] = { 0, 1, 3, 5, epilogue, leafStart, codeEnd - 1 };
            const std::size_t depths[] = { 0, 8, 16, 24, 24, 0, 0 };
            for (std::size_t i = 0; i < std::size(offsets); ++i)
            {
                DWORD64 stack[32]{};
                stack[16] = 0x123456789abcdef0ull;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_FULL;
                context.Rip = reinterpret_cast<DWORD64>(storage + offsets[i]);
                context.Rsp = reinterpret_cast<DWORD64>(&stack[16]) - depths[i];
                void* handler = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, reinterpret_cast<DWORD64>(storage),
                    context.Rip, functions + (offsets[i] >= leafStart ? 1 : 0),
                    &context, &handler, &frame, nullptr);
                if (context.Rip != stack[16] || context.Rsp != reinterpret_cast<DWORD64>(&stack[17]))
                    return false;
            }
            return true;
        }

        bool BuildReader(const Reader& reader, NativeCallPatch& patch)
        {
            Code code;
            code.bytes.reserve(256);
            const unsigned output = reader.output;
            code.Emit({ static_cast<std::uint8_t>(0x50 + output), 0x41, 0x52, 0x41, 0x53 });
            if (output == Rcx)
                code.Emit({ 0x48, 0xff, 0x44, 0x24, 0x18 });
            if (reader.indexed)
            {
                if (reader.indirectId)
                    code.Memory(0x8b, R10, reader.row, 0, false);
                else
                    code.Move(R10, reader.row, false);
                code.Emit({ 0x4d, 0x6b, 0xd2, 0x28 });
                code.Memory(0x03, R10, reader.row == R8 ? R9 : R15,
                            reader.row == R8 ? 0x88 : 0x90);
            }
            else
                code.Move(R10, reader.row);

            code.Emit({ 0x49, 0xbb });
            code.Value(reinterpret_cast<std::uint64_t>(&g_Table));
            code.Memory(0x8b, R11, R11);
            code.Emit({ 0x4d, 0x85, 0xdb });
            std::vector<std::size_t> fallback{ code.Jump(0x84) };
            code.Move(output, R10);
            code.Memory(0x2b, output, R11);
            code.Rex(true, 0, output);
            code.Emit({ 0x81, static_cast<std::uint8_t>(0xf8 | output) });
            code.Value<std::uint32_t>(static_cast<std::uint32_t>(kRows * kStride));
            fallback.push_back(code.Jump(0x83));
            code.Rex(true, R11, output);
            code.Emit({ 0x03, static_cast<std::uint8_t>(0xc0 | ((R11 & 7) << 3) | output) });
            code.Emit({ 0x49, 0x83, 0xc3, 0x08 });
            code.Memory(0x8b, R10, R10, 0x1c, false);
            code.Emit({ 0x41, 0x81, 0xe2, 0xff, 0xff, 0xff, 0x3f });
            code.Rex(true, 0, R10);
            code.Emit({ 0x0f, 0xba, static_cast<std::uint8_t>(0xe8 | (R10 & 7)), 0x3f });
            code.Memory(0x3b, R10, R11, 8);
            fallback.push_back(code.Jump(0x85));
            code.Memory(0x8b, output, R11);

            if (reader.fixedColumn >= 0)
            {
                if (reader.fixedColumn)
                    code.Shift(output, static_cast<unsigned>(reader.fixedColumn) * 16);
            }
            else
            {
                std::vector<std::size_t> selected;
                for (unsigned column = 0; column < 3; ++column)
                {
                    code.ColumnCompare(reader.column, column);
                    auto next = code.Jump(0x85);
                    if (column)
                        code.Shift(output, column * 16);
                    code.Emit({ 0xe9 });
                    const auto end = code.bytes.size();
                    code.Value<std::int32_t>(0);
                    selected.push_back(end);
                    code.Bind(next, code.bytes.size());
                }
                code.Shift(output, 48);
                for (auto at : selected)
                    code.Bind(at, code.bytes.size());
            }
            code.Emit({ 0x81, static_cast<std::uint8_t>(0xe0 | output) });
            code.Value<std::uint32_t>(kWideNone);
            code.Emit({ 0x81, static_cast<std::uint8_t>(0xf8 | output) });
            code.Value<std::uint32_t>(kWideNone);
            const auto present = code.Jump(0x85);
            code.Emit({ 0x48, 0xc7, 0x44, 0x24, 0x10 });
            code.Value<std::uint32_t>(kEngineNone);
            code.Emit({ 0xe9 });
            fallback.push_back(code.bytes.size());
            code.Value<std::int32_t>(0);
            code.Bind(present, code.bytes.size());
            code.Memory(0x89, output, 4, 16);
            code.Emit({ 0x83, static_cast<std::uint8_t>(0xf8 | output), 0xff });
            code.Emit({ 0xe9 });
            const auto toRestore = code.bytes.size();
            code.Value<std::int32_t>(0);
            for (auto at : fallback)
                code.Bind(at, code.bytes.size());
            code.Emit({ 0x81, 0x7c, 0x24, 0x10 });
            code.Value<std::uint32_t>(kEngineNone);
            code.Bind(toRestore, code.bytes.size());
            code.Memory(0x8b, R11, 4, 0);
            code.Memory(0x8b, R10, 4, 8);
            code.Memory(0x8b, output, 4, 16);
            const auto epilogue = code.bytes.size();
            code.Emit({ 0x48, 0x8d, 0x64, 0x24, 0x18 });
            const auto leafStart = code.bytes.size();
            code.Emit({ 0xc3 });
            const auto codeEnd = code.bytes.size();
            while (code.bytes.size() % 4)
                code.Emit({ 0x90 });
            const auto unwindOffset = code.bytes.size();
            code.Emit({ 1, 5, 3, 0, 5, 2, 3, 2, 1, 2, 0, 0 });
            const auto leafUnwindOffset = code.bytes.size();
            code.Emit({ 1, 0, 0, 0 });

            patch.target = ResolveGameAddress(reader.address);
            auto storage = static_cast<std::uint8_t*>(HookArena::AllocateNear(
                reinterpret_cast<std::uintptr_t>(patch.target), code.bytes.size()));
            auto functions = new (std::nothrow) RUNTIME_FUNCTION[2]{};
            if (!storage || !functions)
                return false;
            std::memcpy(storage, code.bytes.data(), code.bytes.size());
            functions[0].BeginAddress = 0;
            functions[0].EndAddress = static_cast<DWORD>(leafStart);
            functions[0].UnwindData = static_cast<DWORD>(unwindOffset);
            functions[1].BeginAddress = static_cast<DWORD>(leafStart);
            functions[1].EndAddress = static_cast<DWORD>(codeEnd);
            functions[1].UnwindData = static_cast<DWORD>(leafUnwindOffset);
            if (!RtlAddFunctionTable(functions, 2, reinterpret_cast<DWORD64>(storage)))
                return false;
            if (!CheckReaderUnwind(storage, functions, epilogue, leafStart, codeEnd))
            {
                LogError("FaceFova: reader unwind check failed, face index widening off");
                return false;
            }
            FlushInstructionCache(GetCurrentProcess(), storage, code.bytes.size());
            patch.destination = storage;
            patch.size = output == Rax ? 5 : 6;
            if (output == Rax)
                std::memcpy(patch.expected, "\x3d\xff\x7f\x00\x00", 5);
            else
                std::memcpy(patch.expected, "\x81\xf9\xff\x7f\x00\x00", 6);
            return true;
        }

        bool Matches(void* address, const std::uint8_t* bytes, std::size_t count)
        {
            if (!address)
                return false;
            __try
            {
                return std::memcmp(address, bytes, count) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReaderContextMatches(void* address, std::size_t index)
        {
            struct Signature
            {
                std::size_t size;
                std::uint64_t hash;
                std::uint64_t ignored[3];
            };
            static constexpr Signature signatures[] = {
                { 126, 0x73a4c4527f0b7188ull, { 0x0403c01e11100000ull, 0x03c01000f00400f0ull, 0 } },
                { 126, 0xd90f22e5c31dcb35ull, { 0x0403c01e11100000ull, 0x03c01000f00400f0ull, 0 } },
                { 115, 0xbd54aac39d29201eull, { 0x1e00807803c22200ull, 0x00007802001e0080ull, 0 } },
                { 103, 0x922d0ab551ed349bull, { 0x0100804022220000ull, 0x0000000402001008ull, 0 } },
                { 29, 0x814fca25880f32c9ull, { 0x0000000000800000ull, 0, 0 } },
                { 103, 0x922d0ab551ed349bull, { 0x0100804022220000ull, 0x0000000402001008ull, 0 } },
                { 29, 0x814fca25880f32c9ull, { 0x0000000000800000ull, 0, 0 } },
                { 134, 0xe184f9e47f62b334ull, { 0x803c00f088800000ull, 0x004000f002003c00ull, 1 } },
                { 40, 0xdf9e7a53160d9920ull, { 0x0000000007800000ull, 0, 0 } },
                { 34, 0x712a5e83b1df2f82ull, { 0x0000000000800000ull, 0, 0 } },
                { 128, 0x900f6685e993c602ull, { 0x100f007844400000ull, 0x0f004003c01003c0ull, 0 } }
            };
            if (!address || index >= std::size(signatures))
                return false;
            const auto& signature = signatures[index];
            const auto bytes = static_cast<const std::uint8_t*>(address) - signature.size;
            auto expected = signature.hash;
            if (gGameBuild == AddressSetRuntime::GameBuild::En_1_0_15_3
                || gGameBuild == AddressSetRuntime::GameBuild::Jp_1_0_15_3)
            {
                if (index == 3 || index == 5)
                    expected = 0xdfced5dcf8575952ull;
                else if (index == 4 || index == 6)
                    expected = 0x53a61617bf1671c4ull;
                else if (index == 9)
                    expected = 0xba510eccbc03b0b1ull;
            }
            __try
            {
                std::uint64_t hash = 14695981039346656037ull;
                for (std::size_t i = 0; i < signature.size; ++i)
                {
                    const auto value = signature.ignored[i / 64] & (1ull << (i % 64)) ? 0 : bytes[i];
                    hash = (hash ^ value) * 1099511628211ull;
                }
                return hash == expected;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool FaceAssetsAlreadyLoaded()
        {
            auto slot = static_cast<const std::uint8_t* const*>(
                ResolveGameAddress(gAddr.Soldier2FaceSystem_Instance));
            if (!slot)
                return false;
            __try
            {
                return *slot && *reinterpret_cast<void* const*>(*slot + 0x88) != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return true;
            }
        }

        void RetireUnactivatedHooks(void* definitionTarget, void* fileTarget)
        {
            g_Ready.store(kFailed, std::memory_order_release);
            const auto fileQueued = MH_QueueDisableHook(fileTarget);
            const auto definitionQueued = MH_QueueDisableHook(definitionTarget);
            const auto applied = MH_ApplyQueued();
            const auto fileDisabled = MH_DisableHook(fileTarget);
            const auto definitionDisabled = MH_DisableHook(definitionTarget);
            if (fileQueued != MH_OK || definitionQueued != MH_OK || applied != MH_OK
                || (fileDisabled != MH_OK && fileDisabled != MH_ERROR_DISABLED)
                || (definitionDisabled != MH_OK && definitionDisabled != MH_ERROR_DISABLED))
                LogError("FaceFova: writer hook rollback incomplete, hooks left as pass-through");
            if (!RemoveNativeCallPatches(g_Readers.data(), g_Readers.size()))
                LogError("FaceFova: reader rollback incomplete, reader memory kept");
        }
    }

    bool FaceFovaWidenReady()
    {
        return g_Ready.load(std::memory_order_acquire) == kActive;
    }

    bool ResetWideFaceFova(void* rows)
    {
        if (!rows)
            return false;
        std::lock_guard<std::mutex> lock(g_TableMutex);
        auto previous = g_Table.load(std::memory_order_acquire);
        if (previous && previous->rows == rows)
            return true;
        auto table = new (std::nothrow) WideTable;
        if (!table)
            return false;
        table->rows = static_cast<const std::uint8_t*>(rows);
        g_Table.store(table, std::memory_order_release);
        return true;
    }

    bool StoreWideFaceFova(void* row, const std::uint16_t (&indices)[4])
    {
        return FaceFovaWidenReady() && PublishWideFaceFova(row, indices);
    }

    void ClearWideFaceFova(void* row)
    {
        if (auto cell = CellFor(g_Table.load(std::memory_order_acquire), row))
            cell->fingerprint.store(0, std::memory_order_release);
    }

    bool ReadWideFaceFova(const void* row, std::uint16_t (&indices)[4])
    {
        auto cell = CellFor(g_Table.load(std::memory_order_acquire), row);
        if (!cell)
            return false;
        const std::uint64_t expected = Fingerprint(row) | kValid;
        if (cell->fingerprint.load(std::memory_order_acquire) != expected)
            return false;
        const auto packed = cell->indices.load(std::memory_order_acquire);
        if (cell->fingerprint.load(std::memory_order_acquire) != expected)
            return false;
        for (unsigned column = 0; column < 4; ++column)
            indices[column] = static_cast<std::uint16_t>((packed >> (column * 16)) & kWideNone);
        return true;
    }

    bool InstallFaceFovaWiden()
    {
        std::lock_guard<std::mutex> installLock(g_InstallMutex);
        if (FaceFovaWidenReady())
            return true;
        if (g_HookBatchMode)
        {
            LogError("FaceFova: installed inside the hook batch, face index widening off");
            return false;
        }
        if (g_InstallAttempted)
            return false;
        g_InstallAttempted = true;
        g_FileGuardSeen.store(false, std::memory_order_release);
        g_LateInstall.store(false, std::memory_order_release);
        const Reader readers[] = {
            { gAddr.FaceFovaRead_Calc, Rax, Rdx, Rbp, -1, false, false },
            { gAddr.FaceFovaRead_ConvertExtend, Rax, Rdx, Rsi, -1, false, false },
            { gAddr.FaceFovaRead_Convert, Rax, Rdx, Rsi, -1, false, false },
            { gAddr.FaceFovaRead_GetFpkArray, Rax, Rcx, R9, -1, false, false },
            { gAddr.FaceFovaRead_GetFpk, Rcx, R8, Rax, 0, true, false },
            { gAddr.FaceFovaRead_GetPathArray, Rax, Rcx, R9, -1, false, false },
            { gAddr.FaceFovaRead_GetPath, Rcx, R8, Rax, 0, true, false },
            { gAddr.FaceFovaRead_GetFpkList, Rcx, Rbp, Rbx, -1, true, false },
            { gAddr.FaceFovaRead_GetSpecialFpkList, Rcx, Rbx, Rax, 1, true, true },
            { gAddr.FaceFovaRead_GetHairPath, Rcx, R8, Rax, 2, true, false },
            { gAddr.FaceFovaRead_PushFpk, Rax, Rdx, Rdi, -1, false, false }
        };
        const std::uint8_t definitionHead[] = {
            0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8d, 0xac, 0x24, 0xf0, 0xfd, 0xff, 0xff
        };
        const std::uint8_t fileHead[] = {
            0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8d, 0x6c, 0x24, 0xd9
        };
        auto definitionTarget = ResolveGameAddress(gAddr.FaceFova_SetDefinitionTableImpl);
        auto fileTarget = ResolveGameAddress(gAddr.FaceFova_SetFileTableImpl);
        if (!Matches(definitionTarget, definitionHead, sizeof(definitionHead))
            || !Matches(fileTarget, fileHead, sizeof(fileHead)) || !LuaAvailable())
        {
            LogError("FaceFova: registration code does not match this build, face index widening off");
            return false;
        }
        const bool assetsAlreadyLoaded = FaceAssetsAlreadyLoaded();
        for (std::size_t i = 0; i < std::size(readers); ++i)
        {
            if (!readers[i].address || !ReaderContextMatches(ResolveGameAddress(readers[i].address), i)
                || !BuildReader(readers[i], g_Readers[i])
                || !Matches(g_Readers[i].target, g_Readers[i].expected, g_Readers[i].size))
            {
                LogError("FaceFova: reader %zu does not match this build, face index widening off", i);
                return false;
            }
        }
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&InstallFaceFovaWiden), &module))
        {
            LogError("FaceFova: module pin failed, face index widening off");
            return false;
        }
        if (assetsAlreadyLoaded)
        {
            if (!InstallNativeCallPatches(g_Readers.data(), g_Readers.size()))
            {
                if (!RemoveNativeCallPatches(g_Readers.data(), g_Readers.size()))
                    LogError("FaceFova: reader rollback incomplete, reader memory kept");
                LogError("FaceFova: reader patch failed, face index widening off");
                return false;
            }
            g_Ready.store(kActive, std::memory_order_release);
            return true;
        }
        if (MH_CreateHook(definitionTarget, &WriteDefinitions,
                          reinterpret_cast<void**>(&g_DefinitionWriter)) != MH_OK)
        {
            LogError("FaceFova: SetDefinitionTable hook failed, face index widening off");
            return false;
        }
        if (MH_CreateHook(fileTarget, &WriteFileTable,
                          reinterpret_cast<void**>(&g_FileTableWriter)) != MH_OK)
        {
            MH_RemoveHook(definitionTarget);
            LogError("FaceFova: SetFileTable hook failed, face index widening off");
            return false;
        }
        if (!InstallNativeCallPatches(g_Readers.data(), g_Readers.size()))
        {
            if (!RemoveNativeCallPatches(g_Readers.data(), g_Readers.size()))
                LogError("FaceFova: reader rollback incomplete, reader memory kept");
            MH_RemoveHook(definitionTarget);
            MH_RemoveHook(fileTarget);
            LogError("FaceFova: reader patch failed, face index widening off");
            return false;
        }
        g_Ready.store(kArming, std::memory_order_release);
        if (MH_QueueEnableHook(fileTarget) != MH_OK || MH_QueueEnableHook(definitionTarget) != MH_OK
            || MH_ApplyQueued() != MH_OK)
        {
            RetireUnactivatedHooks(definitionTarget, fileTarget);
            LogError("FaceFova: hook activation failed, face index widening off");
            return false;
        }
        unsigned expected = kArming;
        if (g_LateInstall.load(std::memory_order_acquire)
            || FaceAssetsAlreadyLoaded()
            || !g_Ready.compare_exchange_strong(expected, kActive, std::memory_order_acq_rel))
        {
            RetireUnactivatedHooks(definitionTarget, fileTarget);
            LogError("FaceFova: face registration raced the install, face index widening off; restart the game");
            return false;
        }
        return true;
    }
}
