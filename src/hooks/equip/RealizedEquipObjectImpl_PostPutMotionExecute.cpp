#include <pch.h>
#include "RealizedEquipObjectImpl_PostPutMotionExecute.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "log.h"

namespace
{
    constexpr std::uint32_t kEquipRows       = 65536;
    constexpr std::uint32_t kEntryStride     = 0x28;
    constexpr std::uint32_t kEquipIdMask     = 0x7FF;
    constexpr std::uint32_t kMotionOffset    = 0x00;
    constexpr std::uint32_t kPoseOffset      = 0x10;
    constexpr std::uint32_t kFlagOffset      = 0x20;
    constexpr std::uint32_t kFirstCustomCol4 = (kPartMotionRowReceiver - 1) * 4;
    constexpr std::uint32_t kFirstCustomCol2 = (kPartMotionRowReceiver - 1) * 2;

    constexpr std::size_t kTableBytes = static_cast<std::size_t>(kEquipRows) * kEntryStride;
    constexpr std::size_t kTrampBytes = 0x400;
    constexpr std::size_t kSiteLength = 9;

    const std::uint8_t kMotionAnchor[] = {
        0x42, 0x8D, 0x0C, 0x41, 0x8D, 0x04, 0x4F,
        0x42, 0x0F, 0xB6, 0x84, 0x08, 0x88, 0x06, 0x00, 0x00 };
    constexpr std::size_t kMotionPatchOff = 7;

    const std::uint8_t kPoseAnchor[] = {
        0x8D, 0x0C, 0x48, 0x8D, 0x04, 0x4F,
        0x42, 0x0F, 0xB6, 0x84, 0x00, 0x2C, 0x0A, 0x00, 0x00 };
    constexpr std::size_t kPosePatchOff = 6;

    const std::uint8_t kFlagAnchor[] = {
        0x41, 0x0F, 0xB6, 0x06, 0x42, 0x8D, 0x04, 0x40, 0x83, 0xC0, 0xFE,
        0x48, 0x05, 0xB9, 0x0E, 0x00, 0x00, 0x49, 0x03, 0xC1 };
    constexpr std::size_t kFlagPatchOff = 11;

    std::mutex        g_Mutex;
    std::atomic<bool> g_Active{ false };
    std::uint8_t*     g_Table = nullptr;
    std::uint8_t*     g_Tramp = nullptr;

    constexpr std::size_t kJumpLength = 5;
    NativeCodePatch g_SitePatches[3] = {};

    std::atomic<bool> g_ArmTried{ false };
    bool ArmPartMotionRows();

    bool RegionSpan(const void* p, std::size_t* span, bool* readable)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        const auto start = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto here  = reinterpret_cast<std::uintptr_t>(p);
        *span = mbi.RegionSize - (here - start);
        const DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
        *readable = mbi.State == MEM_COMMIT && (mbi.Protect & bad) == 0;
        return *span != 0;
    }

    int ScanCode(const std::uint8_t* anchor, std::size_t len,
                 std::uintptr_t* out, int cap)
    {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base || !out || cap <= 0)
            return 0;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;

        int found = 0;
        const auto* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const DWORD size = sec->Misc.VirtualSize
                ? sec->Misc.VirtualSize : sec->SizeOfRawData;
            if (size < len)
                continue;

            std::uint8_t* p   = base + sec->VirtualAddress;
            std::uint8_t* end = p + size;
            while (p < end && found < cap)
            {
                std::size_t span     = 0;
                bool        readable = false;
                if (!RegionSpan(p, &span, &readable))
                    break;
                if (span > static_cast<std::size_t>(end - p))
                    span = static_cast<std::size_t>(end - p);

                if (readable && span >= len)
                {
                    const std::uint8_t* q    = p;
                    const std::uint8_t* last = p + span - len;
                    while (q <= last && found < cap)
                    {
                        const void* f = std::memchr(
                            q, anchor[0], static_cast<std::size_t>(last - q) + 1);
                        if (!f)
                            break;
                        q = static_cast<const std::uint8_t*>(f);
                        if (std::memcmp(q, anchor, len) == 0)
                            out[found++] = reinterpret_cast<std::uintptr_t>(q);
                        ++q;
                    }
                }
                p += (span > len) ? (span - (len - 1)) : span;
            }
        }
        return found;
    }

    std::uint8_t* FindSite(const std::uint8_t* anchor, std::size_t len,
                           std::size_t patchOff, const char* what)
    {
        std::uintptr_t hits[4] = {};
        const int n = ScanCode(anchor, len, hits, 4);
        if (n != 1)
        {
            LogError("PartMotionRows: %s read site found %d times (need 1), custom slide/bolt motion off",
                what, n);
            return nullptr;
        }
        return reinterpret_cast<std::uint8_t*>(hits[0] + patchOff);
    }

    constexpr std::uintptr_t kRel32Reach = 0x78000000ull;

    void* TryAllocInRegion(const MEMORY_BASIC_INFORMATION& mbi, std::size_t size,
                           std::uintptr_t gran, std::uintptr_t lo, std::uintptr_t hi)
    {
        if (mbi.State != MEM_FREE)
            return nullptr;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t regionEnd = regionBase + mbi.RegionSize;
        std::uintptr_t base = (regionBase + gran - 1) & ~(gran - 1);
        if (base < lo)
            base = (lo + gran - 1) & ~(gran - 1);
        for (; base + size <= regionEnd && base <= hi; base += gran)
            if (void* p = VirtualAlloc(reinterpret_cast<LPVOID>(base), size,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return p;
        return nullptr;
    }

    void* AllocExecNear(std::uintptr_t nearAddr, std::size_t size)
    {
        if (void* arena = HookArena::AllocateNear(nearAddr, size))
            return arena;
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const std::uintptr_t gran = si.dwAllocationGranularity;
        const std::uintptr_t lo   = (nearAddr > kRel32Reach) ? (nearAddr - kRel32Reach) : gran;
        const std::uintptr_t hi   = nearAddr + kRel32Reach;

        for (std::uintptr_t a = nearAddr & ~(gran - 1); a <= hi; )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                break;
            if (void* p = TryAllocInRegion(mbi, size, gran, lo, hi))
                return p;
            const auto next = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= a)
                break;
            a = next;
        }

        for (std::uintptr_t a = nearAddr & ~(gran - 1); a > lo; )
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi)) != sizeof(mbi))
                break;
            if (void* p = TryAllocInRegion(mbi, size, gran, lo, hi))
                return p;
            const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            if (base < gran)
                break;
            a = base - gran;
        }
        return nullptr;
    }

    bool InRel32Range(std::uintptr_t from, std::uintptr_t to)
    {
        const std::int64_t d = static_cast<std::int64_t>(to)
                             - static_cast<std::int64_t>(from);
        return d >= -0x7FFF0000ll && d <= 0x7FFF0000ll;
    }

    struct Emitter
    {
        std::uint8_t* buf;
        std::size_t   off;

        void b(std::initializer_list<std::uint8_t> bytes)
        {
            for (std::uint8_t x : bytes)
                buf[off++] = x;
        }
        void imm32(std::uint32_t v)
        {
            std::memcpy(buf + off, &v, 4);
            off += 4;
        }
        void imm64(std::uint64_t v)
        {
            std::memcpy(buf + off, &v, 8);
            off += 8;
        }
        void rel32To(std::uintptr_t target)
        {
            const std::int64_t rel = static_cast<std::int64_t>(target)
                - (reinterpret_cast<std::int64_t>(buf)
                   + static_cast<std::int64_t>(off) + 4);
            imm32(static_cast<std::uint32_t>(static_cast<std::int32_t>(rel)));
        }
        std::size_t hole8()
        {
            buf[off++] = 0x00;
            return off - 1;
        }
        void patch8(std::size_t at)
        {
            buf[at] = static_cast<std::uint8_t>(off - at - 1);
        }
    };

}

bool PartMotionRows_Active()
{
    return g_Active.load(std::memory_order_acquire);
}

bool PartMotionRows_SetEquipRow(unsigned int equipId, int control,
                                const std::uint16_t motion[4],
                                const std::uint16_t pose[4],
                                const std::uint8_t flags[2])
{
    if (!g_ArmTried.exchange(true, std::memory_order_acq_rel))
        ArmPartMotionRows();
    if (!g_Table || equipId >= kEquipRows || control < 0 || control > 1)
        return false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    std::uint8_t* entry = g_Table + static_cast<std::size_t>(equipId) * kEntryStride;
    auto* m = reinterpret_cast<std::uint16_t*>(entry + kMotionOffset);
    auto* p = reinterpret_cast<std::uint16_t*>(entry + kPoseOffset);
    auto* f = entry + kFlagOffset;
    for (int i = 0; i < 4; ++i)
    {
        m[control * 4 + i] = motion ? motion[i] : 0;
        p[control * 4 + i] = pose ? pose[i] : 0;
    }
    for (int i = 0; i < 2; ++i)
        f[control * 2 + i] = flags ? flags[i] : 0;
    return true;
}

void PartMotionRows_ClearEquipRow(unsigned int equipId, int control)
{
    PartMotionRows_SetEquipRow(equipId, control, nullptr, nullptr, nullptr);
}

namespace
{
    constexpr std::uint32_t kTableMotionsPtr = 0x670;
    constexpr std::uint32_t kTablePosesPtr   = 0x678;
    constexpr std::uint32_t kTableMtarsPtr   = 0x680;
    constexpr std::uint32_t kTableRowMotions = 0x688;   // per receiver row: 4 motion indices, 4 pose indices, 2 casing flags
    constexpr std::uint32_t kTableRowPoses   = 0xA2C;
    constexpr std::uint32_t kTableRowFlags   = 0xEB9;
    constexpr int           kNativeRows      = kPartMotionRowReceiver - 1;
    constexpr std::size_t   kNativeRowStride = 12;
    constexpr std::size_t   kNativeRowCap    = 255;
    constexpr std::size_t   kNativeSlackMax  = 8;
    constexpr std::uint32_t kDegToRadBits    = 0x3C8EFA35;
    constexpr std::uint8_t  kAxisXRot        = 1;
    constexpr std::uint8_t  kAxisZRot        = 2;
    constexpr float         kFullTurn        = 6.2831853f;

    struct NativeRowList
    {
        std::uint8_t* buffer     = nullptr;
        std::size_t   used       = 0;
        std::uint32_t generation = 0;
    };
    NativeRowList g_NativeMotions;
    NativeRowList g_NativePoses;

    bool ReadPointerSEH(const void* at, std::uintptr_t* out)
    {
        __try
        {
            *out = *static_cast<const std::uintptr_t*>(at);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WritePointerSEH(void* at, std::uintptr_t value)
    {
        __try
        {
            *static_cast<std::uintptr_t*>(at) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CopyBytesSEH(void* dst, const void* src, std::size_t n)
    {
        __try
        {
            std::memcpy(dst, src, n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float DegToRad()
    {
        float f = 0.f;
        std::memcpy(&f, &kDegToRadBits, sizeof(f));
        return f;
    }

    bool AdoptNativeList(NativeRowList& list, std::uint8_t* table, std::uint32_t ptrOffset,
                         std::uint32_t nextOffset, const char* what)
    {
        std::uintptr_t cur = 0;
        std::uintptr_t next = 0;
        if (!ReadPointerSEH(table + ptrOffset, &cur) || !ReadPointerSEH(table + nextOffset, &next))
        {
            LogError("PartMotionRows: part-motion table %p unreadable, custom slides/hammers stay still",
                static_cast<void*>(table));
            return false;
        }
        if (list.buffer && cur == reinterpret_cast<std::uintptr_t>(list.buffer))
            return true;

        std::size_t span = (cur && next > cur) ? static_cast<std::size_t>(next - cur) : 0;
        const std::size_t slack = span % kNativeRowStride;
        if (span == 0 || slack >= kNativeSlackMax || span > kNativeRowCap * kNativeRowStride)
        {
            LogError("PartMotionRows: %s list layout unexpected (%016llX..%016llX), custom slides/hammers stay still",
                what, static_cast<unsigned long long>(cur), static_cast<unsigned long long>(next));
            return false;
        }
        span -= slack;

        if (!list.buffer)
        {
            list.buffer = static_cast<std::uint8_t*>(VirtualAlloc(
                nullptr, kNativeRowCap * kNativeRowStride, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (!list.buffer)
            {
                LogError("PartMotionRows: %s list copy allocation failed, custom slides/hammers stay still", what);
                return false;
            }
        }
        std::memset(list.buffer, 0, kNativeRowCap * kNativeRowStride);
        if (!CopyBytesSEH(list.buffer, reinterpret_cast<const void*>(cur), span)
            || !WritePointerSEH(table + ptrOffset, reinterpret_cast<std::uintptr_t>(list.buffer)))
        {
            LogError("PartMotionRows: %s list repoint failed, custom slides/hammers stay still", what);
            return false;
        }
        list.used = span;
        ++list.generation;
        return true;
    }

    bool AdoptNativeListsLocked(std::uint8_t* table)
    {
        return AdoptNativeList(g_NativeMotions, table, kTableMotionsPtr, kTablePosesPtr, "motion")
            && AdoptNativeList(g_NativePoses, table, kTablePosesPtr, kTableMtarsPtr, "pose");
    }

    std::uint8_t* MotionTable()
    {
        if (!gAddr.Equip_MotionEntryTable)
            return nullptr;
        return static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Equip_MotionEntryTable));
    }

    std::uint16_t AppendNativeRow(NativeRowList& list, const ReceiverPartMotionRow& row, bool pose)
    {
        if (list.used + kNativeRowStride > kNativeRowCap * kNativeRowStride)
        {
            LogWarn("PartMotionRows: %s list full (%zu rows), bone %s row dropped",
                pose ? "pose" : "motion", kNativeRowCap, row.bone.c_str());
            return 0;
        }
        const bool rotation = row.axis == kAxisXRot || row.axis == kAxisZRot;
        const std::uint32_t hash = FoxHashes::StrCode32(row.bone);
        const float value = static_cast<float>(row.value) * (rotation ? DegToRad() : 1.0f);
        std::uint8_t* r = list.buffer + list.used;
        std::memset(r, 0, kNativeRowStride);
        std::memcpy(r + 0, &hash, sizeof(hash));
        std::memcpy(r + 4, &value, sizeof(value));
        if (pose)
        {
            r[8] = static_cast<std::uint8_t>(row.axis);
        }
        else
        {
            r[8]  = static_cast<std::uint8_t>(static_cast<std::int8_t>(
                        static_cast<long long>(static_cast<float>(row.start) * 100.0f)));
            r[9]  = static_cast<std::uint8_t>(static_cast<std::int8_t>(
                        static_cast<long long>(static_cast<float>(row.end) * 100.0f)));
            r[10] = static_cast<std::uint8_t>(row.axis);
            r[11] = static_cast<std::uint8_t>(row.moveType);
        }
        list.used += kNativeRowStride;
        return static_cast<std::uint16_t>(list.used / kNativeRowStride);
    }
}

bool PartMotionRows_AppendNativeRows(const ReceiverPartMotion& motion, ReceiverPartRowIndices& out)
{
    std::uint8_t* table = MotionTable();
    if (!table)
        return false;

    std::lock_guard<std::mutex> lock(g_Mutex);
    if (!AdoptNativeListsLocked(table))
        return false;

    ReceiverPartRowIndices idx;
    idx.present  = true;
    idx.flags[0] = motion.casingShoot ? 0 : 1;
    idx.flags[1] = motion.casingLast ? 0 : 1;
    const ReceiverPartMotionRow* motionRows[4] =
        { &motion.shoot[0], &motion.shoot[1], &motion.shootLast[0], &motion.shootLast[1] };
    const ReceiverPartMotionRow* poseRows[4] =
        { &motion.poseLoaded[0], &motion.poseLoaded[1], &motion.poseEmpty[0], &motion.poseEmpty[1] };
    for (int i = 0; i < 4; ++i)
    {
        if (motionRows[i]->set)
        {
            idx.motion[i] = AppendNativeRow(g_NativeMotions, *motionRows[i], false);
            if (!idx.motion[i])
                return false;
        }
        if (poseRows[i]->set)
        {
            idx.pose[i] = AppendNativeRow(g_NativePoses, *poseRows[i], true);
            if (!idx.pose[i])
                return false;
        }
    }
    out = idx;
    return true;
}

namespace
{
    const char* const kCylinderBones[] = {
        "SKL_001_CYLINDER", "SKL_002_CYLINDER", "SKL_003_CYLINDER", "SKL_004_CYLINDER",
        "SKL_005_CYLINDER", "SKL_006_CYLINDER", "WEAPON_CYLINDER", "CYLINDER" };

    struct CylinderCopy
    {
        std::uint16_t source;
        std::uint32_t turnBits;
        std::uint16_t copy;
    };
    std::vector<CylinderCopy> g_CylinderCopies;
    std::uint32_t             g_CylinderCopiesGeneration = 0;

    bool IsCylinderBone(std::uint32_t hash)
    {
        static const std::vector<std::uint32_t> hashes = [] {
            std::vector<std::uint32_t> out;
            for (const char* name : kCylinderBones)
                out.push_back(FoxHashes::StrCode32(name));
            return out;
        }();
        for (const std::uint32_t h : hashes)
            if (h == hash)
                return true;
        return false;
    }

    bool ReadMotionRowLocked(std::uint8_t* table, std::uint16_t index, std::uint8_t (&out)[kNativeRowStride])
    {
        std::uintptr_t list = 0;
        if (index == 0 || !ReadPointerSEH(table + kTableMotionsPtr, &list) || !list)
            return false;
        std::size_t used = 0;
        std::uintptr_t next = 0;
        if (list == reinterpret_cast<std::uintptr_t>(g_NativeMotions.buffer))
            used = g_NativeMotions.used;
        else if (ReadPointerSEH(table + kTablePosesPtr, &next) && next > list)
            used = static_cast<std::size_t>(next - list);
        if (used > kNativeRowCap * kNativeRowStride || static_cast<std::size_t>(index) * kNativeRowStride > used)
            return false;
        return CopyBytesSEH(out, reinterpret_cast<const void*>(list + (static_cast<std::size_t>(index) - 1) * kNativeRowStride),
                            kNativeRowStride);
    }

    bool SpinsCylinder(const std::uint8_t (&row)[kNativeRowStride])
    {
        std::uint32_t hash = 0;
        std::memcpy(&hash, row, sizeof(hash));
        return (row[10] == kAxisXRot || row[10] == kAxisZRot) && IsCylinderBone(hash);
    }

    std::uint16_t CylinderCopyLocked(std::uint16_t source, const std::uint8_t (&row)[kNativeRowStride], float turn)
    {
        if (g_CylinderCopiesGeneration != g_NativeMotions.generation)
        {
            g_CylinderCopies.clear();
            g_CylinderCopiesGeneration = g_NativeMotions.generation;
        }
        std::uint32_t turnBits = 0;
        std::memcpy(&turnBits, &turn, sizeof(turnBits));
        for (const CylinderCopy& c : g_CylinderCopies)
            if (c.source == source && c.turnBits == turnBits)
                return c.copy;

        if (g_NativeMotions.used + kNativeRowStride > kNativeRowCap * kNativeRowStride)
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LogWarn("PartMotionRows: motion list full (%zu rows), revolver cylinders keep their receiver's turn", kNativeRowCap);
            }
            return 0;
        }
        std::uint8_t* copy = g_NativeMotions.buffer + g_NativeMotions.used;
        std::memcpy(copy, row, kNativeRowStride);
        std::memcpy(copy + 4, &turn, sizeof(turn));
        g_NativeMotions.used += kNativeRowStride;
        const auto index = static_cast<std::uint16_t>(g_NativeMotions.used / kNativeRowStride);
        g_CylinderCopies.push_back({ source, turnBits, index });
        return index;
    }
}

bool PartMotionRows_ReadNativeRow(int row, ReceiverPartRowIndices& out)
{
    out = {};
    std::uint8_t* table = MotionTable();
    if (row < 1 || row > kNativeRows || !table)
        return false;
    const std::size_t at = static_cast<std::size_t>(row - 1);
    std::uint8_t motion[4] = {};
    std::uint8_t pose[4] = {};
    if (!CopyBytesSEH(motion, table + kTableRowMotions + at * 4, sizeof(motion))
        || !CopyBytesSEH(pose, table + kTableRowPoses + at * 4, sizeof(pose))
        || !CopyBytesSEH(out.flags, table + kTableRowFlags + at * 2, sizeof(out.flags)))
        return false;
    for (int i = 0; i < 4; ++i)
    {
        out.motion[i] = motion[i];
        out.pose[i] = pose[i];
    }
    out.present = true;
    return true;
}

bool PartMotionRows_TurnsCylinder(const ReceiverPartRowIndices& row)
{
    std::uint8_t* table = MotionTable();
    if (!table)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (const std::uint16_t index : row.motion)
    {
        std::uint8_t motion[kNativeRowStride];
        if (ReadMotionRowLocked(table, index, motion) && SpinsCylinder(motion))
            return true;
    }
    return false;
}

bool PartMotionRows_FitCylinderToChambers(ReceiverPartRowIndices& row, unsigned int chambers)
{
    std::uint8_t* table = MotionTable();
    if (chambers < 2 || !table)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (!AdoptNativeListsLocked(table))
        return false;
    const float step = kFullTurn / static_cast<float>(chambers);
    bool changed = false;
    for (std::uint16_t& index : row.motion)
    {
        std::uint8_t motion[kNativeRowStride];
        if (!ReadMotionRowLocked(table, index, motion) || !SpinsCylinder(motion))
            continue;
        float turn = 0.0f;
        std::memcpy(&turn, motion + 4, sizeof(turn));
        const float wanted = turn < 0.0f ? -step : step;
        if (std::fabs(turn - wanted) < 1e-4f)
            continue;
        if (const std::uint16_t copy = CylinderCopyLocked(index, motion, wanted))
        {
            index = copy;
            changed = true;
        }
    }
    return changed;
}

namespace
{
    bool ArmPartMotionRows()
    {
        if (g_Active.load(std::memory_order_acquire))
            return true;

    std::uint8_t* motionSite = FindSite(kMotionAnchor, sizeof(kMotionAnchor),
                                        kMotionPatchOff, "shot part-motion");
    std::uint8_t* poseSite = FindSite(kPoseAnchor, sizeof(kPoseAnchor),
                                      kPosePatchOff, "idle part-pose");
    std::uint8_t* flagSite = FindSite(kFlagAnchor, sizeof(kFlagAnchor),
                                      kFlagPatchOff, "casing-flag");
    if (!motionSite || !poseSite || !flagSite)
        return false;

    g_Table = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, kTableBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_Table)
    {
        LogError("PartMotionRows: %zu-byte row table allocation failed, custom slide/bolt motion off", kTableBytes);
        return false;
    }
    std::memset(g_Table, 0, kTableBytes);

    g_Tramp = static_cast<std::uint8_t*>(
        AllocExecNear(reinterpret_cast<std::uintptr_t>(motionSite), kTrampBytes));
    if (!g_Tramp)
    {
        VirtualFree(g_Table, 0, MEM_RELEASE);
        g_Table = nullptr;
        LogError("PartMotionRows: no executable page near 0x%llX, custom slide/bolt motion off",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(motionSite)));
        return false;
    }
    std::memset(g_Tramp, 0xCC, kTrampBytes);

    struct SiteSpec
    {
        std::uint8_t* site;
        std::uint32_t firstCol;
        std::uint32_t colCount;
        std::uint32_t recordBias;
        bool          halfScale;
        std::uint32_t entryOffset;
        std::uint8_t  vanilla[kSiteLength];
    };
    const SiteSpec specs[3] = {
        { motionSite, kFirstCustomCol4, 8, 0x10, false, kMotionOffset,
          { 0x42, 0x0F, 0xB6, 0x84, 0x08, 0x88, 0x06, 0x00, 0x00 } },
        { poseSite,   kFirstCustomCol4, 8, 0x12, false, kPoseOffset,
          { 0x42, 0x0F, 0xB6, 0x84, 0x00, 0x2C, 0x0A, 0x00, 0x00 } },
        { flagSite,   kFirstCustomCol2, 4, 0x10, true,  kFlagOffset,
          { 0x48, 0x05, 0xB9, 0x0E, 0x00, 0x00, 0x49, 0x03, 0xC1 } },
    };

    std::size_t off = 0;
    std::uint8_t* entries[3] = {};
    for (int i = 0; i < 3; ++i)
    {
        const SiteSpec& s = specs[i];
        const auto resume = reinterpret_cast<std::uintptr_t>(s.site) + kSiteLength;
        Emitter e{ g_Tramp, off };
        entries[i] = g_Tramp + off;

        e.b({ 0x48, 0x3D });
        e.imm32(s.firstCol);
        e.b({ 0x0F, 0x82 });
        const std::size_t vanillaRel = e.off;
        e.imm32(0);

        e.b({ 0x51, 0x52 });
        e.b({ 0x48, 0x89, 0xC1 });
        e.b({ 0x48, 0x81, 0xE9 });
        e.imm32(s.firstCol);
        e.b({ 0x48, 0x83, 0xF9, static_cast<std::uint8_t>(s.colCount) });
        e.b({ 0x73 });
        const std::size_t popHole = e.hole8();

        e.b({ 0x48, 0x89, 0xCA });
        if (s.halfScale)
            e.b({ 0x48, 0xD1, 0xEA });
        else
            e.b({ 0x48, 0xC1, 0xEA, 0x02 });
        e.b({ 0x49, 0x8B, 0xC6 });
        e.b({ 0x48, 0x83, 0xE8, static_cast<std::uint8_t>(s.recordBias) });
        e.b({ 0x48, 0x29, 0xD0 });
        e.b({ 0x0F, 0xB7, 0x40, 0x04 });
        e.b({ 0x25 });
        e.imm32(kEquipIdMask);
        e.b({ 0x6B, 0xC0, static_cast<std::uint8_t>(kEntryStride) });
        e.b({ 0x48, 0xBA });
        e.imm64(reinterpret_cast<std::uint64_t>(g_Table));
        e.b({ 0x48, 0x01, 0xC2 });

        if (s.halfScale)
            e.b({ 0x48, 0x8D, 0x44, 0x0A, static_cast<std::uint8_t>(s.entryOffset) });
        else
            e.b({ 0x0F, 0xB7, 0x44, 0x4A, static_cast<std::uint8_t>(s.entryOffset) });

        e.b({ 0x5A, 0x59 });
        e.b({ 0xE9 });
        e.rel32To(resume);

        e.patch8(popHole);
        e.b({ 0x5A, 0x59 });

        const std::int32_t vrel = static_cast<std::int32_t>(
            static_cast<std::int64_t>(e.off) - static_cast<std::int64_t>(vanillaRel) - 4);
        std::memcpy(g_Tramp + vanillaRel, &vrel, 4);

        for (std::size_t k = 0; k < kSiteLength; ++k)
            e.b({ s.vanilla[k] });
        e.b({ 0xE9 });
        e.rel32To(resume);

        off = (e.off + 15) & ~static_cast<std::size_t>(15);
        if (off > kTrampBytes)
        {
            VirtualFree(g_Tramp, 0, MEM_RELEASE);
            VirtualFree(g_Table, 0, MEM_RELEASE);
            g_Tramp = nullptr;
            g_Table = nullptr;
            LogError("PartMotionRows: emitted code exceeds %zu bytes, custom slide/bolt motion off", kTrampBytes);
            return false;
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        const auto from = reinterpret_cast<std::uintptr_t>(specs[i].site);
        if (!InRel32Range(from + 5, reinterpret_cast<std::uintptr_t>(entries[i])))
        {
            VirtualFree(g_Tramp, 0, MEM_RELEASE);
            VirtualFree(g_Table, 0, MEM_RELEASE);
            g_Tramp = nullptr;
            g_Table = nullptr;
            LogError("PartMotionRows: trampoline out of jump range of site %d, custom slide/bolt motion off", i);
            return false;
        }
    }

    for (int i = 0; i < 3; ++i)
    {
        NativeCodePatch& patch = g_SitePatches[i];
        patch = {};
        patch.target = specs[i].site;
        patch.size = kJumpLength; // only the jump is written, the trampoline replays the whole 9-byte site
        std::memcpy(patch.expected, specs[i].vanilla, kJumpLength);
        patch.replacement[0] = 0xE9;
        const std::int32_t rel = static_cast<std::int32_t>(
            reinterpret_cast<std::int64_t>(entries[i])
            - (reinterpret_cast<std::int64_t>(specs[i].site) + 5));
        std::memcpy(patch.replacement + 1, &rel, 4);
    }

    bool installed = false;
    for (int attempt = 0; attempt < 50 && !installed; ++attempt)
    {
        installed = InstallNativeCodePatches(g_SitePatches, 3);
        if (!installed)
        {
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(4);
        }
    }
    if (!installed)
    {
        LogError("PartMotionRows: site patch refused (%s), custom slide/bolt motion off",
            NativePatchErrorName(GetLastError()));
        if (!g_SitePatches[0].installed && !g_SitePatches[1].installed && !g_SitePatches[2].installed)
        {
            VirtualFree(g_Tramp, 0, MEM_RELEASE);
            VirtualFree(g_Table, 0, MEM_RELEASE);
            g_Tramp = nullptr;
            g_Table = nullptr;
        }
        return false;
    }

    g_Active.store(true, std::memory_order_release);
        return true;
    }
}

bool PartMotionRows_Install()
{
    return true;
}

void PartMotionRows_Uninstall()
{
    if (!g_Active.exchange(false, std::memory_order_acq_rel))
        return;
    bool removed = false;
    for (int attempt = 0; attempt < 50 && !removed; ++attempt)
    {
        removed = RemoveNativeCodePatches(g_SitePatches, 3);
        if (!removed)
        {
            if (!NativePatchErrorIsTransient(GetLastError()))
                break;
            Sleep(4);
        }
    }
    if (!removed)
    {
        LogError("PartMotionRows: site restore refused (%s), trampoline stays allocated",
            NativePatchErrorName(GetLastError()));
        return;
    }
    if (g_Tramp)
    {
        VirtualFree(g_Tramp, 0, MEM_RELEASE);
        g_Tramp = nullptr;
    }
    if (g_Table)
    {
        VirtualFree(g_Table, 0, MEM_RELEASE);
        g_Table = nullptr;
    }
}
