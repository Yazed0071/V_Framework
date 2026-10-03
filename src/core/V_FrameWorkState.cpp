#include "pch.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "AddressSet.h"
#include "../hooks/equip/CustomBluePrint.h"
#include "../hooks/equip/EquipIdCompression.h"
#include "../hooks/equip/DevelopArrayGrow.h"
#include "FeatureModule.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace equip
{
    std::uint32_t NativeFlowBound();
}

namespace V_FrameWorkState
{
    namespace
    {
        static constexpr const char* kSavePath    = "mod\\V_FrameWork\\V_FrameWork_State.lua";
        static constexpr const char* kLegacyPath  = "mod\\saves\\V_FrameWork_State.lua";


        static constexpr std::int32_t kFirstCustomEquipIdMinimum = 1;
        static constexpr std::int32_t kFirstCustomDevelopId = 0x1000;
        static constexpr std::int32_t kFirstCustomFlowIndex = 922;
        static constexpr std::int32_t kGunsmithFlowFirst    = 0x3FD;
        static constexpr std::int32_t kGunsmithFlowLast     = 0x3FF;

        static bool IsReservedFlowIndex(std::int32_t i)
        {
            return i >= kGunsmithFlowFirst && i <= kGunsmithFlowLast;
        }
        static constexpr std::int32_t kNativeFlowSentinel   = 0x400;

        static std::int32_t NativeFlowIndexBound()
        {
            return static_cast<std::int32_t>(equip::NativeFlowBound());
        }
        static constexpr std::int16_t kFirstCustomTapeSaveIndex = 300;
        static constexpr std::int16_t kMaxCustomTapeSaveIndex = 32000;
        static constexpr std::int32_t kTapeOrphanGraceLaunches = 2;
        static constexpr std::int32_t kEquipOrphanGraceLaunches = 2;
        static constexpr std::int32_t kConstantOrphanGraceLaunches = 2;
        static constexpr std::int32_t kBluePrintOrphanGraceLaunches = 2;
        static constexpr std::int32_t kCallMenuOrphanGraceLaunches = 2;
        static constexpr std::int32_t kMaxCustomConstantValue = 0xFFF0;

        static constexpr char kUniqueStaffKeyPrefix[] = "USTAFF:";
        static constexpr std::size_t kUniqueStaffKeyPrefixLen =
            sizeof(kUniqueStaffKeyPrefix) - 1;

        static constexpr char kFaceIdKeyPrefix[] = "FACEID:";
        static constexpr std::size_t kFaceIdKeyPrefixLen =
            sizeof(kFaceIdKeyPrefix) - 1;

        static bool IsUniqueStaffConstantKey(const std::string& key)
        {
            return key.compare(0, kUniqueStaffKeyPrefixLen, kUniqueStaffKeyPrefix) == 0;
        }

        static bool IsFaceIdConstantKey(const std::string& key)
        {
            return key.compare(0, kFaceIdKeyPrefixLen, kFaceIdKeyPrefix) == 0;
        }

        static constexpr char kFaceMetaKeyPrefix[] = "FACEMETA:";
        static constexpr char kStaffFaceKeyPrefix[] = "STAFFFACE:";
        static constexpr char kStaffProxyKeyPrefix[] = "STAFFPROXY:";
        static constexpr char kFaceReusedKeyPrefix[] = "FACEREUSED:";
        static constexpr char kFaceRetiredKeyPrefix[] = "FACERETIRED:";

        static bool HasKeyPrefix(const std::string& key, const char* prefix, std::size_t length)
        {
            return key.compare(0, length, prefix) == 0;
        }

        static bool IsFaceLinkConstantKey(const std::string& key)
        {
            return HasKeyPrefix(key, kFaceMetaKeyPrefix, sizeof(kFaceMetaKeyPrefix) - 1)
                || HasKeyPrefix(key, kFaceReusedKeyPrefix, sizeof(kFaceReusedKeyPrefix) - 1)
                || HasKeyPrefix(key, kStaffFaceKeyPrefix, sizeof(kStaffFaceKeyPrefix) - 1)
                || HasKeyPrefix(key, kStaffProxyKeyPrefix, sizeof(kStaffProxyKeyPrefix) - 1);
        }

        static bool IsNeverEvictConstantKey(const std::string& key)
        {
            return IsUniqueStaffConstantKey(key) || IsFaceLinkConstantKey(key)
                || HasKeyPrefix(key, kFaceRetiredKeyPrefix, sizeof(kFaceRetiredKeyPrefix) - 1);
        }

        static bool FaceIdOfLinkKey(const std::string& key, std::int32_t value, std::int32_t& faceId)
        {
            const bool meta = HasKeyPrefix(key, kFaceMetaKeyPrefix, sizeof(kFaceMetaKeyPrefix) - 1);
            if (meta || HasKeyPrefix(key, kFaceReusedKeyPrefix, sizeof(kFaceReusedKeyPrefix) - 1))
            {
                const char* name = key.c_str()
                    + (meta ? sizeof(kFaceMetaKeyPrefix) : sizeof(kFaceReusedKeyPrefix)) - 1;
                if (name[0] != 'f' || !name[1])
                    return false;
                std::int32_t id = 0;
                for (const char* digit = name + 1; *digit; ++digit)
                {
                    if (*digit < '0' || *digit > '9' || id > 0xFFFF)
                        return false;
                    id = id * 10 + (*digit - '0');
                }
                faceId = id;
                return true;
            }
            if (HasKeyPrefix(key, kStaffFaceKeyPrefix, sizeof(kStaffFaceKeyPrefix) - 1))
            {
                faceId = value - 1;
                return faceId >= 0;
            }
            if (!HasKeyPrefix(key, kStaffProxyKeyPrefix, sizeof(kStaffProxyKeyPrefix) - 1))
                return false;
            const std::string hex = key.substr(sizeof(kStaffProxyKeyPrefix) - 1);
            if (hex.size() != 16)
                return false;
            std::uint64_t staffId = 0;
            for (const char c : hex)
            {
                std::uint32_t digit = 0;
                if (c >= '0' && c <= '9')
                    digit = static_cast<std::uint32_t>(c - '0');
                else if (c >= 'A' && c <= 'F')
                    digit = static_cast<std::uint32_t>(c - 'A' + 10);
                else
                    return false;
                staffId = (staffId << 4) | digit;
            }
            faceId = static_cast<std::int32_t>((staffId >> 20) & 0x3FF);
            return true;
        }

        struct RetiredFace
        {
            std::string key;
            std::int32_t faceId = 0;
            std::int32_t launches = 0;
        };

        struct EquipEntry
        {


            std::int32_t developId = 0;
            std::int32_t flowIndex = 0;

            std::int32_t equipId = 0;
            std::int32_t subId = 0;

            std::int32_t partsType = 0;
            std::int32_t selector  = 0;
            std::uint8_t variantSelectors[V_FrameWorkState::kPersistedVariantSelectorSlots] = {};

            std::int32_t misses = 0;

            std::int8_t developed = -1;

            bool isNew = false;
            bool devReqAnnounced = false;

            std::uint8_t kind = 0;
        };

        struct TapeEntry
        {
            std::int16_t saveIndex = -1;
            bool owned = false;
            bool isNew = false;
            std::int32_t misses = 0;
        };

        struct BluePrintEntry
        {
            std::int32_t id = 0;
            bool owned = false;
            bool isNew = false;
            std::int32_t misses = 0;
        };

        struct State
        {
            bool loaded = false;
            bool dirty = false;
            std::unordered_map<std::string, EquipEntry> equips;
            std::unordered_map<std::string, TapeEntry> tapes;
            std::unordered_map<std::string, BluePrintEntry> bluePrints;
            std::unordered_map<std::string, std::int32_t> constants;
            std::unordered_map<std::string, std::int32_t> constantMisses;
            std::unordered_set<std::int32_t> pinnedEquipIds;
            std::unordered_map<std::string, V_FrameWorkState::DispatchMission> dispatchMissions;
            std::vector<V_FrameWorkState::DispatchSlotFact> dispatchSlots;
            V_FrameWorkState::DispatchCursor dispatchCursors[2];
            std::unordered_map<std::string, V_FrameWorkState::CallMenuItem> callMenuItems;
        };

        static constexpr char kLegacyDispatchIdSpace[]      = "DEPLOYW:";
        static constexpr char kLegacyDispatchRewardSpace[]  = "DEPLOYH:";
        static constexpr char kLegacyDispatchPendingSpace[] = "DEPLOYHPEND:";
        static constexpr char kLegacyDispatchReservedMark   = '~';

        static constexpr std::int32_t kFirstDispatchId        = 0x100;
        static constexpr std::int32_t kLastDispatchId         = 0xFFF0;
        static constexpr std::int32_t kFirstDispatchRewardRow = 0x86;
        static constexpr std::int32_t kLastDispatchRewardRow  = 0xFD;

        struct DispatchRewardField
        {
            const char*  name;
            std::size_t  offset;
            std::size_t  width;
        };

        static constexpr DispatchRewardField kDispatchRewardFields[] =
        {
            { "keyValue2", 0x01, 1 },           { "mainRewardType", 0x02, 1 },      { "gmp", 0x04, 4 },
            { "staffHitRate", 0x08, 1 },        { "staffDrawCount", 0x09, 1 },
            { "staffGRate", 0x0A, 1 },          { "staffFRate", 0x0B, 1 },          { "staffERate", 0x0C, 1 },
            { "staffDRate", 0x0D, 1 },          { "staffCRate", 0x0E, 1 },          { "staffBRate", 0x0F, 1 },
            { "staffARate", 0x10, 1 },          { "staffSRate", 0x11, 1 },          { "staffSpRate", 0x12, 1 },
            { "staffSppRate", 0x13, 1 },        { "staffRelativeRate", 0x14, 1 },   { "staffRelativeP1Rate", 0x15, 1 },
            { "resourceHitRate", 0x16, 1 },     { "resourceDrawCount", 0x18, 2 },
            { "fuelResourceRate", 0x1A, 1 },    { "bioticResourceRate", 0x1B, 1 },  { "commonMetalRate", 0x1C, 1 },
            { "minorMetalRate", 0x1D, 1 },      { "preciousMetalRate", 0x1E, 1 },   { "goldenCrescentRate", 0x1F, 1 },
            { "blackCarrotRate", 0x20, 1 },     { "wormwoodRate", 0x21, 1 },        { "tarragonRate", 0x22, 1 },
            { "africanPeachRate", 0x23, 1 },    { "digitalisPRate", 0x24, 1 },      { "digitalisLRate", 0x25, 1 },
            { "haomaRate", 0x26, 1 },           { "keyItemDataBaseId", 0x28, 2 },   { "keyItemRate", 0x2A, 1 },
            { "rewardRate", 0x2B, 1 },
        };
        static constexpr std::size_t   kDispatchRewardSectionOffset = 0x01;
        static constexpr std::size_t   kDispatchRewardKeyItemOffset = 0x28;
        static constexpr std::size_t   kDispatchRewardFlagsOffset   = 0x2C;
        static constexpr std::uint16_t kDispatchNoKeyItem           = 0xFFFF;

        static void NormalizeDispatchReward(std::uint8_t* reward)
        {
            std::uint8_t clean[V_FrameWorkState::kDispatchRewardRecordSize] = {};
            for (const DispatchRewardField& field : kDispatchRewardFields)
                std::memcpy(clean + field.offset, reward + field.offset, field.width);
            clean[kDispatchRewardFlagsOffset] = reward[kDispatchRewardFlagsOffset] & 1;
            std::memcpy(reward, clean, sizeof(clean));
        }

        static bool SameDispatchMission(const V_FrameWorkState::DispatchMission& a,
                                        const V_FrameWorkState::DispatchMission& b)
        {
            return a.deployMissionId == b.deployMissionId && a.rewardRow == b.rewardRow
                && a.cleared == b.cleared && a.rewardPending == b.rewardPending
                && a.hasReward == b.hasReward
                && (!a.hasReward || std::memcmp(a.reward, b.reward, sizeof(a.reward)) == 0);
        }

        static State g_State;
        static std::mutex g_Mutex;
        static std::vector<std::int32_t> g_PendingDevelopedResets;
        static int g_BatchDepth = 0;
        static std::unordered_set<std::int16_t> g_TapeSaveIndexInUse;

        static constexpr unsigned long long kCoalesceMs = 250;

        static std::condition_variable g_FlushCv;
        static std::thread             g_FlusherThread;
        static bool                    g_FlusherRunning = false;
        static bool                    g_FlusherStop    = false;
        static unsigned long long      g_SaveDueTick    = 0;


        static std::unordered_set<std::string> g_ConstantsTouched;
        static std::unordered_set<std::string> g_DispatchTouched;
        static bool g_ExitSave = false;
        static bool g_LaunchHealthy = false;

        static bool CanCountConstantMisses()
        {
            if (!g_ExitSave)
                return false;
            if (!g_LaunchHealthy)
                return false;
            if (FeatureAnyDisabled())
                return false;
            return true;
        }
        static std::unordered_set<std::int32_t> g_SessionPinnedIds;
        static std::unordered_set<std::int32_t> g_StickyPinnedIds;
        static bool g_PinSetFreshThisSession = false;

        static std::unordered_map<std::string, std::int32_t> g_SessionEquipIds;

        static std::atomic<std::uint64_t> g_ClaimedEquipBits[0x10000 / 64] = {};

        static void NoteClaimedEquipId_NoLock(std::int32_t equipId)
        {
            if (equipId > 0 && equipId <= 0xFFFF)
                g_ClaimedEquipBits[equipId >> 6].fetch_or(
                    1ull << (equipId & 63), std::memory_order_relaxed);
        }

        static std::unordered_set<std::int32_t> g_VanillaIdentityIds;

        static std::unordered_map<std::string, std::int32_t> g_SessionFlowIndices;

        static std::unordered_map<std::int32_t, std::int32_t> g_OldFlowLayout;

        static std::string Trim(const std::string& s)
        {
            const auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return {};
            const auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        static void EnsureSaveDirectory()
        {
            CreateDirectoryA("mod", nullptr);
            CreateDirectoryA("mod\\V_FrameWork", nullptr);
        }


        static void MigrateLegacyStateFile_NoLock()
        {
            const DWORD newAttr = GetFileAttributesA(kSavePath);
            if (newAttr != INVALID_FILE_ATTRIBUTES)
                return;

            const DWORD oldAttr = GetFileAttributesA(kLegacyPath);
            if (oldAttr == INVALID_FILE_ATTRIBUTES)
                return;

            CreateDirectoryA("mod", nullptr);
            CreateDirectoryA("mod\\V_FrameWork", nullptr);

            MoveFileA(kLegacyPath, kSavePath);
        }


        static std::int32_t ParseStateInt(const std::string& text)
        {
            std::size_t at = 0;
            bool negative = false;
            if (at < text.size() && (text[at] == '-' || text[at] == '+'))
            {
                negative = (text[at] == '-');
                ++at;
            }
            int base = 10;
            if (text.size() - at > 2 && text[at] == '0'
                && (text[at + 1] == 'x' || text[at + 1] == 'X'))
            {
                base = 16;
                at += 2;
            }
            try
            {
                const long v = std::stol(text.substr(at), nullptr, base);
                return static_cast<std::int32_t>(negative ? -v : v);
            }
            catch (...) { return 0; }
        }

        static bool ParseEquipLine(const std::string& line, std::string& outKey, EquipEntry& out)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            out = {};

            auto findField = [&](const char* name) -> std::int32_t
            {
                const std::string field = std::string(name) + " = ";
                const auto pos = line.find(field, rb);
                if (pos == std::string::npos) return 0;
                const auto start = pos + field.size();
                std::string numStr;
                for (auto i = start; i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                return ParseStateInt(numStr);
            };

            out.developId = findField("developId");
            out.flowIndex = findField("flowIndex");
            out.equipId   = findField("equipId");
            out.subId     = findField("subId");
            out.partsType = findField("partsType");
            out.selector  = findField("selector");
            out.misses    = findField("misses");

            {
                const char* tag = "variantSelectors = \"";
                const auto pos = line.find(tag, rb);
                if (pos != std::string::npos)
                {
                    std::size_t i = pos + std::strlen(tag);
                    std::size_t vi = 0;
                    std::string numStr;
                    for (; i < line.size()
                           && vi < V_FrameWorkState::kPersistedVariantSelectorSlots; ++i)
                    {
                        const char c = line[i];
                        if (c >= '0' && c <= '9') { numStr.push_back(c); continue; }
                        if (!numStr.empty())
                        {
                            long v = 0;
                            try { v = std::stol(numStr); }
                            catch (...) { v = 0; }
                            if (v > 0 && v <= 0xFF)
                                out.variantSelectors[vi] =
                                    static_cast<std::uint8_t>(v);
                            ++vi;
                            numStr.clear();
                        }
                        if (c == '"') break;
                    }
                }
            }
            if (line.find("developed = true", rb) != std::string::npos)
                out.developed = 1;
            else if (line.find("developed = false", rb) != std::string::npos)
                out.developed = 0;

            out.isNew =
                line.find("new = true", rb) != std::string::npos
                || line.find("seen = true", rb) == std::string::npos;
            out.devReqAnnounced =
                line.find("reqAnnounced = true", rb) != std::string::npos
                || line.find("announcePending = true", rb) == std::string::npos;

            return !outKey.empty();
        }


        static bool ParseTapeLine(const std::string& line, std::string& outKey, TapeEntry& out)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            out = {};

            auto findIntField = [&](const char* name) -> std::int32_t
            {
                const std::string field = std::string(name) + " = ";
                const auto pos = line.find(field, rb);
                if (pos == std::string::npos) return 0;
                const auto start = pos + field.size();
                std::string numStr;
                for (auto i = start; i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                return ParseStateInt(numStr);
            };

            out.saveIndex = static_cast<std::int16_t>(findIntField("saveIndex"));
            out.owned = line.find("owned = true", rb) != std::string::npos;
            out.isNew = line.find("new = true", rb) != std::string::npos;
            out.misses = findIntField("misses");
            return !outKey.empty() && out.saveIndex > 0;
        }

        static bool ParseBluePrintLine(const std::string& line, std::string& outKey,
                                       BluePrintEntry& out)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            out = {};

            const std::string field = "bluePrintId = ";
            const auto pos = line.find(field, rb);
            if (pos != std::string::npos)
            {
                std::string numStr;
                for (auto i = pos + field.size(); i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                out.id = ParseStateInt(numStr);
            }

            out.owned = line.find("owned = true", rb) != std::string::npos;
            out.isNew = line.find("new = true", rb) != std::string::npos;

            const std::string missField = "misses = ";
            const auto mpos = line.find(missField, rb);
            if (mpos != std::string::npos)
            {
                std::string numStr;
                for (auto i = mpos + missField.size(); i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (c == ',' || c == '}' || c == ' ') break;
                    numStr.push_back(c);
                }
                out.misses = ParseStateInt(numStr);
            }

            return !outKey.empty();
        }

        static bool ParseConstantLine(const std::string& line, std::string& outKey, std::int32_t& outValue)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;

            outKey = line.substr(lb + 2, rb - (lb + 2));
            outValue = 0;

            const auto eq = line.find('=', rb);
            if (eq == std::string::npos) return false;

            std::string numStr;
            for (auto i = eq + 1; i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == ' ' && numStr.empty()) continue;
                if (c == ',' || c == '}' || c == ' ') break;
                numStr.push_back(c);
            }
            outValue = ParseStateInt(numStr);
            return !outKey.empty() && outValue != 0;
        }

        static bool ParseRowKey(const std::string& line, std::string& outKey, std::size_t& keyEnd)
        {
            const auto lb = line.find("[\"");
            if (lb == std::string::npos) return false;
            const auto rb = line.find("\"]", lb + 2);
            if (rb == std::string::npos) return false;
            outKey = line.substr(lb + 2, rb - (lb + 2));
            keyEnd = rb;
            return !outKey.empty();
        }

        static std::int32_t RowIntField(const std::string& line, std::size_t from, const char* name)
        {
            const std::string field = std::string(name) + " = ";
            const auto pos = line.find(field, from);
            if (pos == std::string::npos) return 0;
            std::string numStr;
            for (auto i = pos + field.size(); i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == ',' || c == '}' || c == ' ') break;
                numStr.push_back(c);
            }
            return ParseStateInt(numStr);
        }

        static std::string RowStringField(const std::string& line, std::size_t from, const char* name)
        {
            const std::string field = std::string(name) + " = \"";
            const auto pos = line.find(field, from);
            if (pos == std::string::npos) return {};
            const auto start = pos + field.size();
            const auto end = line.find('"', start);
            if (end == std::string::npos) return {};
            return line.substr(start, end - start);
        }

        static bool BytesOfHex(const std::string& text, std::uint8_t* bytes, std::size_t count)
        {
            if (text.size() != count * 2)
                return false;
            auto nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            for (std::size_t i = 0; i < count; ++i)
            {
                const int high = nibble(text[i * 2]);
                const int low = nibble(text[i * 2 + 1]);
                if (high < 0 || low < 0)
                    return false;
                bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
            }
            return true;
        }

        static bool RowHasField(const std::string& line, std::size_t from, const char* name)
        {
            return line.find(std::string(name) + " = ", from) != std::string::npos;
        }

        static std::string HexText(std::uint64_t value)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
            return text;
        }

        static std::uint64_t ParseHexText(const std::string& text)
        {
            const std::size_t at = (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 2 : 0;
            if (text.size() <= at)
                return 0;
            try { return std::stoull(text.substr(at), nullptr, 16); }
            catch (...) { return 0; }
        }

        static std::string FloatPairText(const float (&pair)[2])
        {
            char text[48];
            std::snprintf(text, sizeof(text), "%.9g %.9g", pair[0], pair[1]);
            return text;
        }

        static bool ParseFloatPair(const std::string& text, float (&pair)[2])
        {
            float values[2] = {};
            if (text.empty() || sscanf_s(text.c_str(), "%f %f", &values[0], &values[1]) != 2)
                return false;
            pair[0] = values[0];
            pair[1] = values[1];
            return true;
        }

        static std::int8_t RowOverride(const std::string& line, std::size_t from, const char* name)
        {
            if (line.find(std::string(name) + " = true", from) != std::string::npos)
                return 1;
            if (line.find(std::string(name) + " = false", from) != std::string::npos)
                return 0;
            return V_FrameWorkState::kNoOverride;
        }

        static bool ParseCallMenuItemLine(const std::string& line, V_FrameWorkState::CallMenuItem& out)
        {
            std::size_t keyEnd = 0;
            out = {};
            if (!ParseRowKey(line, out.key, keyEnd))
                return false;

            out.label       = ParseHexText(RowStringField(line, keyEnd, "label"));
            out.description = ParseHexText(RowStringField(line, keyEnd, "description"));
            if (out.description == 0)
                out.description = out.label;
            out.column      = RowIntField(line, keyEnd, "column");
            out.when        = RowIntField(line, keyEnd, "when");
            out.message     = static_cast<std::uint32_t>(ParseHexText(RowStringField(line, keyEnd, "message")));
            out.exclusive   = static_cast<std::uint32_t>(ParseHexText(RowStringField(line, keyEnd, "exclusive")));
            out.inPlaceOf   = RowHasField(line, keyEnd, "inPlaceOf") ? RowIntField(line, keyEnd, "inPlaceOf") : -1;
            out.icon        = RowIntField(line, keyEnd, "icon");
            out.iconTexture = ParseHexText(RowStringField(line, keyEnd, "iconTexture"));
            out.iconCell    = RowIntField(line, keyEnd, "iconCell");
            out.hasIconUvShift = ParseFloatPair(RowStringField(line, keyEnd, "iconUvShift"), out.iconUvShift);
            ParseFloatPair(RowStringField(line, keyEnd, "iconUvRepeat"), out.iconUvRepeat);

            const std::string voices = RowStringField(line, keyEnd, "voices");
            std::size_t at = 0;
            while (at < voices.size())
            {
                const std::size_t end = voices.find(' ', at);
                const std::uint64_t voice = ParseHexText(voices.substr(at, end == std::string::npos ? std::string::npos : end - at));
                if (voice != 0)
                    out.voices.push_back(static_cast<std::uint32_t>(voice));
                if (end == std::string::npos)
                    break;
                at = end + 1;
            }

            out.enabled = RowOverride(line, keyEnd, "enabled");
            out.visible = RowOverride(line, keyEnd, "visible");
            out.hasInPlaceOfOverride = RowHasField(line, keyEnd, "inPlaceOfOverride");
            out.inPlaceOfOverride    = out.hasInPlaceOfOverride ? RowIntField(line, keyEnd, "inPlaceOfOverride") : -1;
            out.page    = RowIntField(line, keyEnd, "page");
            out.row     = RowHasField(line, keyEnd, "row") ? RowIntField(line, keyEnd, "row") : -1;
            out.misses  = RowIntField(line, keyEnd, "misses");
            return out.label != 0;
        }

        static bool SameCallMenuItem(const V_FrameWorkState::CallMenuItem& a, const V_FrameWorkState::CallMenuItem& b)
        {
            return a.label == b.label && a.description == b.description && a.column == b.column && a.when == b.when
                && a.message == b.message && a.exclusive == b.exclusive && a.inPlaceOf == b.inPlaceOf
                && a.icon == b.icon && a.iconTexture == b.iconTexture && a.iconCell == b.iconCell
                && a.hasIconUvShift == b.hasIconUvShift
                && a.iconUvShift[0] == b.iconUvShift[0] && a.iconUvShift[1] == b.iconUvShift[1]
                && a.iconUvRepeat[0] == b.iconUvRepeat[0] && a.iconUvRepeat[1] == b.iconUvRepeat[1]
                && a.voices == b.voices && a.enabled == b.enabled && a.visible == b.visible
                && a.hasInPlaceOfOverride == b.hasInPlaceOfOverride && a.inPlaceOfOverride == b.inPlaceOfOverride
                && a.page == b.page && a.row == b.row && a.misses == b.misses;
        }

        static bool ParseDispatchMissionLine(const std::string& line,
                                             V_FrameWorkState::DispatchMission& out)
        {
            std::size_t keyEnd = 0;
            out = {};
            if (!ParseRowKey(line, out.key, keyEnd))
                return false;

            if (RowHasField(line, keyEnd, "mission"))
            {
                out.hasReward = BytesOfHex(RowStringField(line, keyEnd, "reward"), out.reward,
                                           V_FrameWorkState::kDispatchRewardRecordSize);
                if (out.hasReward)
                    NormalizeDispatchReward(out.reward);
                return true;
            }

            out.deployMissionId = RowIntField(line, keyEnd, "deployMissionId");
            out.rewardRow       = RowIntField(line, keyEnd, "rewardRow");
            out.misses          = RowIntField(line, keyEnd, "misses");
            out.cleared         = line.find("cleared = true", keyEnd) != std::string::npos;
            out.rewardPending   = line.find("rewardPending = true", keyEnd) != std::string::npos;
            out.hasReward       = RowHasField(line, keyEnd, "keyValue2");
            if (!out.hasReward)
                return true;

            std::memcpy(out.reward + kDispatchRewardKeyItemOffset, &kDispatchNoKeyItem, sizeof(kDispatchNoKeyItem));
            for (const DispatchRewardField& field : kDispatchRewardFields)
            {
                if (!RowHasField(line, keyEnd, field.name))
                    continue;
                const std::int32_t value = RowIntField(line, keyEnd, field.name);
                std::memcpy(out.reward + field.offset, &value, field.width);
            }
            if (line.find("materialIsBeforeProcess = true", keyEnd) != std::string::npos)
                out.reward[kDispatchRewardFlagsOffset] = 1;
            return true;
        }

        static bool ParseDispatchSlotLine(const std::string& line,
                                          V_FrameWorkState::DispatchSlotFact& out)
        {
            out = {};
            if (line.rfind("{", 0) == 0)
            {
                out.slot          = RowIntField(line, 0, "slot");
                out.placeholderId = RowIntField(line, 0, "placeholderId");
                out.seed          = RowIntField(line, 0, "seed");
                out.key           = RowStringField(line, 0, "mission");
                out.misses        = RowIntField(line, 0, "misses");
                return !out.key.empty();
            }

            std::string composite;
            std::size_t keyEnd = 0;
            if (!ParseRowKey(line, composite, keyEnd))
                return false;

            std::int32_t numbers[3] = {};
            std::size_t at = 0;
            for (std::int32_t& number : numbers)
            {
                const auto slash = composite.find('/', at);
                if (slash == std::string::npos)
                    return false;
                number = ParseStateInt(composite.substr(at, slash - at));
                at = slash + 1;
            }
            out.slot = numbers[0];
            out.placeholderId = numbers[1];
            out.seed = numbers[2];
            out.key = composite.substr(at);
            out.misses = RowIntField(line, keyEnd, "misses");
            return !out.key.empty();
        }

        static bool ParseDispatchCursorLine(const std::string& line, bool& rareBand,
                                            V_FrameWorkState::DispatchCursor& out)
        {
            std::string band;
            std::size_t keyEnd = 0;
            out = {};
            if (!ParseRowKey(line, band, keyEnd))
                return false;
            if (band != "N" && band != "R")
                return false;
            rareBand = (band == "R");
            out.key = RowStringField(line, keyEnd, "mission");
            if (out.key.empty())
                out.key = RowStringField(line, keyEnd, "key");
            out.anchor = RowIntField(line, keyEnd, "anchor");
            return !out.key.empty();
        }

        static std::string CanonicalConstantKey(std::string key)
        {
            static const char kLegacyWeaponSlot[] = "WPSLOT:";
            if (key.rfind(kLegacyWeaponSlot, 0) == 0)
                key = "WP:" + key.substr(sizeof(kLegacyWeaponSlot) - 1);
            return key;
        }

        static bool RetireFaceLinks_NoLock(const std::vector<RetiredFace>& retired)
        {
            std::unordered_set<std::int32_t> live;
            for (const auto& kv : g_State.constants)
                if (IsFaceIdConstantKey(kv.first) && kv.second != 0)
                    live.insert(kv.second);

            bool changed = false;
            for (auto it = g_State.constants.begin(); it != g_State.constants.end(); )
            {
                std::int32_t faceId = -1;
                if (!IsFaceLinkConstantKey(it->first)
                    || !FaceIdOfLinkKey(it->first, it->second, faceId)
                    || live.count(faceId) != 0)
                {
                    ++it;
                    continue;
                }
                g_State.constantMisses.erase(it->first);
                it = g_State.constants.erase(it);
                changed = true;
            }

            for (const RetiredFace& face : retired)
            {
                if (live.count(face.faceId) != 0)
                    LogInfo("CustomFace '%s': unregistered for %d launches, entry removed (faceId %d kept by another face)",
                        face.key.c_str(), face.launches, face.faceId);
                else
                {
                    g_State.constants[std::string(kFaceRetiredKeyPrefix) + "f" + std::to_string(face.faceId)] = 1;
                    changed = true;
                    LogInfo("CustomFace '%s': unregistered for %d launches, faceId %d retired",
                        face.key.c_str(), face.launches, face.faceId);
                }
            }
            return changed;
        }

        static void SaveToDisk_NoLock();
        static void WriteToDisk_NoLock();

        static V_FrameWorkState::DispatchMission& DispatchRowOf_NoLock(const std::string& key)
        {
            V_FrameWorkState::DispatchMission& row = g_State.dispatchMissions[key];
            row.key = key;
            return row;
        }

        static bool FoldLegacyDispatchConstants_NoLock()
        {
            const std::size_t idLength = sizeof(kLegacyDispatchIdSpace) - 1;
            const std::size_t rowLength = sizeof(kLegacyDispatchRewardSpace) - 1;
            const std::size_t pendingLength = sizeof(kLegacyDispatchPendingSpace) - 1;

            bool changed = false;
            for (auto it = g_State.constants.begin(); it != g_State.constants.end(); )
            {
                const std::string& name = it->first;
                const auto missEntry = g_State.constantMisses.find(name);
                const std::int32_t misses = (missEntry != g_State.constantMisses.end()) ? missEntry->second : 0;

                if (HasKeyPrefix(name, kLegacyDispatchPendingSpace, pendingLength))
                {
                    if (it->second != 0)
                        DispatchRowOf_NoLock(name.substr(pendingLength)).rewardPending = true;
                }
                else if (HasKeyPrefix(name, kLegacyDispatchIdSpace, idLength))
                {
                    const bool reserved = name.size() > idLength && name[idLength] == kLegacyDispatchReservedMark;
                    V_FrameWorkState::DispatchMission& row =
                        DispatchRowOf_NoLock(name.substr(idLength + (reserved ? 1 : 0)));
                    if (!reserved)
                    {
                        row.deployMissionId = it->second;
                        row.misses = misses;
                    }
                    else
                    {
                        row.rewardPending = true;
                        if (row.deployMissionId == 0)
                        {
                            row.deployMissionId = it->second;
                            row.misses = V_FrameWorkState::kDispatchOrphanGraceLaunches;
                        }
                    }
                }
                else if (HasKeyPrefix(name, kLegacyDispatchRewardSpace, rowLength))
                {
                    DispatchRowOf_NoLock(name.substr(rowLength)).rewardRow = it->second;
                }
                else
                {
                    ++it;
                    continue;
                }

                if (missEntry != g_State.constantMisses.end())
                    g_State.constantMisses.erase(missEntry);
                it = g_State.constants.erase(it);
                changed = true;
            }
            return changed;
        }

        static bool CollectDispatchGarbage_NoLock()
        {
            bool changed = false;

            std::vector<std::string> keys;
            for (const auto& kv : g_State.dispatchMissions)
                keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());

            std::unordered_set<std::int32_t> ids;
            std::unordered_set<std::int32_t> rows;
            for (const std::string& key : keys)
            {
                V_FrameWorkState::DispatchMission& row = g_State.dispatchMissions[key];
                row.key = key;
                if (row.deployMissionId != 0
                    && (row.deployMissionId < kFirstDispatchId || row.deployMissionId > kLastDispatchId
                        || !ids.insert(row.deployMissionId).second))
                {
                    row.deployMissionId = 0;
                    changed = true;
                }
                if (row.rewardRow != 0
                    && (row.rewardRow < kFirstDispatchRewardRow || row.rewardRow > kLastDispatchRewardRow
                        || !rows.insert(row.rewardRow).second))
                {
                    row.rewardRow = 0;
                    changed = true;
                }
            }

            for (auto it = g_State.dispatchMissions.begin(); it != g_State.dispatchMissions.end(); )
            {
                V_FrameWorkState::DispatchMission& row = it->second;
                if (!row.rewardPending
                    && (row.deployMissionId == 0 || row.misses >= V_FrameWorkState::kDispatchOrphanGraceLaunches))
                {
                    if (row.deployMissionId != 0)
                        LogInfo("Dispatch '%s': unregistered for %d launches, deployMissionId %d freed",
                            it->first.c_str(), row.misses, row.deployMissionId);
                    it = g_State.dispatchMissions.erase(it);
                    changed = true;
                    continue;
                }
                if (!row.rewardPending && row.hasReward)
                {
                    row.hasReward = false;
                    std::memset(row.reward, 0, sizeof(row.reward));
                    changed = true;
                }
                ++it;
            }

            for (V_FrameWorkState::DispatchCursor& cursor : g_State.dispatchCursors)
            {
                if (cursor.key.empty())
                    continue;
                const auto row = g_State.dispatchMissions.find(cursor.key);
                if (row == g_State.dispatchMissions.end()
                    || row->second.misses >= V_FrameWorkState::kDispatchOrphanGraceLaunches)
                {
                    cursor = {};
                    changed = true;
                }
            }
            return changed;
        }

        static void LoadFromDisk_NoLock()
        {
            if (g_State.loaded) return;
            g_State.loaded = true;
            g_State.equips.clear();
            g_State.tapes.clear();
            g_State.bluePrints.clear();
            g_State.constants.clear();
            g_State.constantMisses.clear();
            g_State.pinnedEquipIds.clear();
            g_State.dispatchMissions.clear();
            g_State.dispatchSlots.clear();
            g_State.dispatchCursors[0] = {};
            g_State.dispatchCursors[1] = {};
            g_State.callMenuItems.clear();
            g_ConstantsTouched.clear();
            g_DispatchTouched.clear();
            g_TapeSaveIndexInUse.clear();

            MigrateLegacyStateFile_NoLock();

            std::ifstream in(kSavePath);
            if (!in)
                return;

            enum Section { None, Equips, Weapons, Outfits, HeadOptions, Tapes,
                           BluePrints, Constants, ConstantMisses,
                           PinnedIds, DispatchMissions, DispatchSlots,
                           DispatchCursors, CallMenuItems } section = None;

            std::string constantSpace;

            std::string line;
            while (std::getline(in, line))
            {
                const std::string trimmed = Trim(line);

                if ((section == Constants || section == ConstantMisses)
                    && trimmed.find("[\"") == std::string::npos
                    && trimmed.find('{') != std::string::npos)
                {
                    const auto eq = trimmed.find('=');
                    if (eq != std::string::npos)
                        constantSpace = Trim(trimmed.substr(0, eq));
                    continue;
                }

                if (trimmed.rfind("equips", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Equips;
                    continue;
                }

                if (trimmed.rfind("develop", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Equips;
                    continue;
                }

                if (trimmed.rfind("weapons", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Weapons;
                    continue;
                }

                if (trimmed.rfind("outfits", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Outfits;
                    continue;
                }

                if (trimmed.rfind("headOptions", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = HeadOptions;
                    continue;
                }

                if (trimmed.rfind("tapes", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Tapes;
                    continue;
                }

                if ((trimmed.rfind("DataBase", 0) == 0
                     || trimmed.rfind("BluePrint", 0) == 0) &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = BluePrints;
                    continue;
                }

                if (trimmed.rfind("constantMisses", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = ConstantMisses;
                    continue;
                }

                if (trimmed.rfind("loadoutPinnedIds", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = PinnedIds;
                    continue;
                }

                if (trimmed.rfind("constants", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = Constants;
                    continue;
                }

                if (trimmed.rfind("dispatchMissions", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = DispatchMissions;
                    continue;
                }

                if (trimmed.rfind("dispatchSlots", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = DispatchSlots;
                    continue;
                }

                if (trimmed.rfind("dispatchCursor", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = DispatchCursors;
                    continue;
                }

                if (trimmed.rfind("callMenuItems", 0) == 0 &&
                    trimmed.find('{') != std::string::npos)
                {
                    section = CallMenuItems;
                    continue;
                }


                if (trimmed == "}," || (trimmed == "}" && section != None))
                {
                    if ((section == Constants || section == ConstantMisses)
                        && !constantSpace.empty())
                        constantSpace.clear();
                    else
                        section = None;
                    continue;
                }

                if (section == Equips || section == Weapons
                    || section == Outfits || section == HeadOptions)
                {
                    std::string key;
                    EquipEntry entry;
                    if (ParseEquipLine(trimmed, key, entry))
                    {
                        entry.kind = (section == Weapons) ? kRowKindWeapon
                                   : (section == Outfits) ? kRowKindOutfit
                                   : (section == HeadOptions) ? kRowKindHeadOption
                                                              : kRowKindUnknown;
                        const auto colon = key.rfind(':');
                        if (colon != std::string::npos && entry.equipId != 0)
                        {
                            const std::string bareKey = key.substr(colon + 1);
                            EquipEntry& bare = g_State.equips[bareKey];
                            bare.equipId = entry.equipId;
                            if (entry.misses != 0)
                                bare.misses = entry.misses;
                            entry.equipId = 0;
                        }
                        g_State.equips[key] = entry;
                    }
                }
                else if (section == Tapes)
                {
                    std::string key;
                    TapeEntry entry;
                    if (ParseTapeLine(trimmed, key, entry))
                    {
                        g_State.tapes[key] = entry;
                        g_TapeSaveIndexInUse.insert(entry.saveIndex);
                        LogInfo("CustomTapes: '%s' loaded, saveIndex %d", key.c_str(), static_cast<int>(entry.saveIndex));
                    }
                }
                else if (section == BluePrints)
                {
                    std::string key;
                    BluePrintEntry entry;
                    if (ParseBluePrintLine(trimmed, key, entry))
                        g_State.bluePrints[key] = entry;
                }
                else if (section == Constants)
                {
                    std::string key;
                    std::int32_t value = 0;
                    if (ParseConstantLine(trimmed, key, value))
                    {
                        if (!constantSpace.empty())
                            key = constantSpace + ":" + key;
                        g_State.constants[CanonicalConstantKey(key)] = value;
                    }
                }
                else if (section == ConstantMisses)
                {
                    std::string key;
                    std::int32_t value = 0;
                    if (ParseConstantLine(trimmed, key, value))
                    {
                        if (!constantSpace.empty())
                            key = constantSpace + ":" + key;
                        g_State.constantMisses[CanonicalConstantKey(key)] = value;
                    }
                }
                else if (section == PinnedIds)
                {
                    std::string key;
                    std::int32_t value = 0;
                    if (ParseConstantLine(trimmed, key, value))
                        g_State.pinnedEquipIds.insert(value);
                }
                else if (section == DispatchMissions)
                {
                    V_FrameWorkState::DispatchMission mission;
                    if (ParseDispatchMissionLine(trimmed, mission))
                    {
                        V_FrameWorkState::DispatchMission& row = g_State.dispatchMissions[mission.key];
                        const std::int32_t legacyId = row.deployMissionId;
                        const std::int32_t legacyRow = row.rewardRow;
                        row = mission;
                        if (row.deployMissionId == 0)
                            row.deployMissionId = legacyId;
                        if (row.rewardRow == 0)
                            row.rewardRow = legacyRow;
                    }
                }
                else if (section == DispatchSlots)
                {
                    V_FrameWorkState::DispatchSlotFact fact;
                    if (ParseDispatchSlotLine(trimmed, fact))
                        g_State.dispatchSlots.push_back(fact);
                }
                else if (section == DispatchCursors)
                {
                    bool rareBand = false;
                    V_FrameWorkState::DispatchCursor cursor;
                    if (ParseDispatchCursorLine(trimmed, rareBand, cursor))
                        g_State.dispatchCursors[rareBand ? 1 : 0] = cursor;
                }
                else if (section == CallMenuItems)
                {
                    V_FrameWorkState::CallMenuItem item;
                    if (ParseCallMenuItemLine(trimmed, item))
                        g_State.callMenuItems[item.key] = item;
                }
            }

            in.close();

            {
                const std::string prefix = "BLUEPRINT:";
                for (auto it = g_State.constants.begin(); it != g_State.constants.end(); )
                {
                    if (it->first.compare(0, prefix.size(), prefix) == 0 && it->second > 0)
                    {
                        const std::string key = it->first.substr(prefix.size());
                        auto bp = g_State.bluePrints.find(key);
                        if (bp == g_State.bluePrints.end() || bp->second.id <= 0)
                            g_State.bluePrints[key].id = it->second;
                        g_State.constantMisses.erase(it->first);
                        it = g_State.constants.erase(it);
                        g_State.dirty = true;
                        continue;
                    }
                    ++it;
                }

                for (auto it = g_State.bluePrints.begin(); it != g_State.bluePrints.end(); )
                {
                    if (it->second.id <= 0)
                        it = g_State.bluePrints.erase(it);
                    else
                        ++it;
                }

                for (auto it = g_State.bluePrints.begin(); it != g_State.bluePrints.end(); )
                {
                    if (it->second.misses >= kBluePrintOrphanGraceLaunches)
                    {
                        LogWarn("State: blueprint '%s' (id %d) unregistered for %d launches, removed with its ownership",
                            it->first.c_str(), it->second.id, it->second.misses);
                        it = g_State.bluePrints.erase(it);
                        continue;
                    }
                    ++it->second.misses;
                    ++it;
                }
            }

            static const char kRewardLangKeyPrefix[] = "REWARDLANG32:";
            const std::size_t kRewardLangKeyPrefixLen = sizeof(kRewardLangKeyPrefix) - 1;

            bool gcChanged = false;
            std::size_t evicted = 0;
            for (auto it = g_State.tapes.begin(); it != g_State.tapes.end(); )
            {
                if (it->second.misses >= kTapeOrphanGraceLaunches)
                {
                    LogInfo("CustomTapes: '%s' deleted, saveIndex %d freed (mod uninstalled)", it->first.c_str(), static_cast<int>(it->second.saveIndex));
                    it = g_State.tapes.erase(it);
                    gcChanged = true;
                }
                else
                {
                    ++it->second.misses;
                    gcChanged = true;
                    ++it;
                }
            }

            for (auto it = g_State.callMenuItems.begin(); it != g_State.callMenuItems.end(); )
            {
                if (it->second.misses >= kCallMenuOrphanGraceLaunches)
                {
                    LogInfo("CallMenu: '%s' not added for %d launches, removed", it->first.c_str(), it->second.misses);
                    it = g_State.callMenuItems.erase(it);
                }
                else
                {
                    ++it->second.misses;
                    ++it;
                }
                gcChanged = true;
            }

            if (FoldLegacyDispatchConstants_NoLock())
                gcChanged = true;
            if (CollectDispatchGarbage_NoLock())
                gcChanged = true;

            std::vector<RetiredFace> retiredFaces;
            for (auto it = g_State.constants.begin(); it != g_State.constants.end(); )
            {
                if (IsNeverEvictConstantKey(it->first))
                {
                    ++it;
                    continue;
                }

                const auto mit = g_State.constantMisses.find(it->first);
                const std::int32_t misses =
                    (mit != g_State.constantMisses.end()) ? mit->second : 0;
                if (misses >= kConstantOrphanGraceLaunches)
                {
                    if (mit != g_State.constantMisses.end())
                        g_State.constantMisses.erase(mit);
                    if (IsFaceIdConstantKey(it->first))
                        retiredFaces.push_back({ it->first.substr(kFaceIdKeyPrefixLen),
                                                 it->second, misses });
                    else if (it->first.compare(0, kRewardLangKeyPrefixLen,
                                               kRewardLangKeyPrefix) != 0)
                        ++evicted;
                    it = g_State.constants.erase(it);
                    gcChanged = true;
                    continue;
                }
                ++it;
            }
            if (RetireFaceLinks_NoLock(retiredFaces))
                gcChanged = true;

            if (evicted != 0)
                LogWarn("State: %zu equip constant(s) expired, new equipIds shift; re-pick affected loadout slots",
                    evicted);

            for (auto it = g_State.equips.begin(); it != g_State.equips.end(); )
            {
                if (it->second.misses >= kEquipOrphanGraceLaunches
                    && it->second.equipId != 0)
                {
                    const std::int32_t slot =
                        EquipIdCompression::ComputeCompressed(it->second.equipId);
                    bool pinned = false;
                    for (const std::int32_t p : g_State.pinnedEquipIds)
                        if (EquipIdCompression::ComputeCompressed(p) == slot)
                        {
                            pinned = true;
                            break;
                        }
                    if (pinned)
                    {
                        ++it;
                        continue;
                    }
                }
                if (it->second.misses >= kEquipOrphanGraceLaunches)
                {
                    it = g_State.equips.erase(it);
                    gcChanged = true;
                    continue;
                }
                ++it->second.misses;
                gcChanged = true;
                ++it;
            }

            g_OldFlowLayout.clear();
            for (const auto& kv : g_State.equips)
            {
                const std::int32_t fi = kv.second.flowIndex;
                const std::int32_t dv = kv.second.developId;
                if (dv == 0 || fi < kFirstCustomFlowIndex
                    || fi >= static_cast<std::int32_t>(equip::kMaxFlowSlots)
                    || fi == kNativeFlowSentinel || IsReservedFlowIndex(fi))
                    continue;
                auto found = g_OldFlowLayout.find(fi);
                if (found == g_OldFlowLayout.end())
                    g_OldFlowLayout[fi] = dv;
                else if (found->second != dv)
                    found->second = -1;
            }

            if (gcChanged)
            {
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
        }

        static void FlusherMain()
        {
            std::unique_lock<std::mutex> lock(g_Mutex);
            for (;;)
            {
                g_FlushCv.wait(lock,
                    [] { return g_FlusherStop || g_SaveDueTick != 0; });

                if (g_FlusherStop)
                    return;

                for (;;)
                {
                    const unsigned long long now = GetTickCount64();
                    if (now >= g_SaveDueTick)
                        break;
                    g_FlushCv.wait_for(lock,
                        std::chrono::milliseconds(g_SaveDueTick - now));
                    if (g_FlusherStop)
                        return;
                }

                g_SaveDueTick = 0;

                WriteToDisk_NoLock();
            }
        }

        static void SaveToDisk_NoLock()
        {
            if (g_BatchDepth > 0)
                return;

            if (g_FlusherStop)
            {
                WriteToDisk_NoLock();
                return;
            }

            g_SaveDueTick = GetTickCount64() + kCoalesceMs;

            if (!g_FlusherRunning)
            {
                g_FlusherRunning = true;
                g_FlusherThread = std::thread(&FlusherMain);
            }

            g_FlushCv.notify_one();
        }

        static void EmitAlignedRows(std::ostream& out,
                                    const std::vector<std::string>& keys,
                                    const std::vector<std::vector<std::string>>& rows)
        {
            std::size_t keyWidth = 0;
            std::vector<std::size_t> colWidth;
            for (std::size_t r = 0; r < keys.size(); ++r)
            {
                if (keys[r].size() > keyWidth) keyWidth = keys[r].size();
                if (colWidth.size() < rows[r].size())
                    colWidth.resize(rows[r].size(), 0);
                for (std::size_t c = 0; c < rows[r].size(); ++c)
                    if (rows[r][c].size() > colWidth[c])
                        colWidth[c] = rows[r][c].size();
            }
            if (keyWidth > 56) keyWidth = 56;

            for (std::size_t r = 0; r < keys.size(); ++r)
            {
                std::string body;
                for (std::size_t c = 0; c < rows[r].size(); ++c)
                {
                    if (colWidth[c] == 0) continue;
                    body.append(rows[r][c]);
                    if (rows[r][c].size() < colWidth[c])
                        body.append(colWidth[c] - rows[r][c].size(), ' ');
                    body.push_back(' ');
                }
                while (!body.empty() && body.back() == ' ')
                    body.pop_back();

                std::string key = keys[r];
                if (key.size() < keyWidth)
                    key.append(keyWidth - key.size(), ' ');
                out << "        " << key << " = { " << body << " },\n";
            }
        }

        static const char* ConstantSpaceNote(const std::string& tag)
        {
            if (tag == "WP")           return "gunBasic weapon slot per WP_ name";
            if (tag == "REWARDLANG32") return "TppReward LANG_ENUM index per langId";
            if (tag == "CUSTOMHEAD")   return "(slotByte << 16) | equipId per head name";
            return nullptr;
        }

        static void EmitConstantRows(
            std::ostream& out,
            const std::vector<std::pair<std::string, std::int32_t>>& rows,
            bool withNotes = true)
        {
            auto tagOf = [](const std::string& key)
            {
                const auto colon = key.find(':');
                return (colon == std::string::npos)
                    ? std::string() : key.substr(0, colon);
            };
            auto nameOf = [](const std::string& key)
            {
                const auto colon = key.find(':');
                return (colon == std::string::npos)
                    ? key : key.substr(colon + 1);
            };

            std::string tag;
            std::size_t keyWidth = 0;
            bool first = true;
            bool open = false;

            for (std::size_t r = 0; r < rows.size(); ++r)
            {
                const std::string rowTag = tagOf(rows[r].first);
                if (first || rowTag != tag)
                {
                    if (open)
                    {
                        out << "        },\n";
                        open = false;
                    }
                    if (!first) out << "\n";
                    tag = rowTag;
                    first = false;

                    keyWidth = 0;
                    for (std::size_t s = r; s < rows.size(); ++s)
                    {
                        if (tagOf(rows[s].first) != tag) break;
                        const std::size_t w = nameOf(rows[s].first).size() + 4;
                        if (w > keyWidth) keyWidth = w;
                    }
                    if (keyWidth > 60) keyWidth = 60;

                    if (!tag.empty())
                    {
                        const char* note =
                            withNotes ? ConstantSpaceNote(tag) : nullptr;
                        if (note)
                            out << "        -- " << note << "\n";
                        out << "        " << tag << " = {\n";
                        open = true;
                    }
                }

                std::string key = "[\"" + nameOf(rows[r].first) + "\"]";
                if (key.size() < keyWidth)
                    key.append(keyWidth - key.size(), ' ');
                out << (open ? "            " : "        ")
                    << key << " = " << rows[r].second << ",\n";
            }

            if (open)
                out << "        },\n";
        }

        static std::int32_t DispatchMissesToWrite(const V_FrameWorkState::DispatchMission& mission)
        {
            if (g_DispatchTouched.count(mission.key) != 0)
                return 0;
            if (mission.misses >= V_FrameWorkState::kDispatchOrphanGraceLaunches)
                return V_FrameWorkState::kDispatchOrphanGraceLaunches;
            return CanCountConstantMisses() ? mission.misses + 1 : mission.misses;
        }

        static std::string DispatchRewardCells(const std::uint8_t* reward)
        {
            std::string text;
            for (const DispatchRewardField& field : kDispatchRewardFields)
            {
                std::int32_t value = 0;
                std::memcpy(&value, reward + field.offset, field.width);
                const bool keyItem = field.offset == kDispatchRewardKeyItemOffset;
                const bool empty = keyItem ? value == kDispatchNoKeyItem : value == 0;
                if (empty && field.offset != kDispatchRewardSectionOffset)
                    continue;
                text += std::string(field.name) + " = " + std::to_string(value) + ", ";
            }
            if (reward[kDispatchRewardFlagsOffset] & 1)
                text += "materialIsBeforeProcess = true, ";
            while (!text.empty() && text.back() == ' ')
                text.pop_back();
            return text;
        }

        static const char* DispatchSlotNote(std::int32_t slot)
        {
            if (slot >= 1 && slot <= 20)  return "story";
            if (slot == 21)               return "tutorial";
            if (slot >= 31 && slot <= 35) return "R/SR";
            if (slot >= 36 && slot <= 40) return "N";
            if (slot >= 41 && slot <= 54) return "revenge";
            if (slot >= 55 && slot <= 60) return "script";
            if (slot >= 61 && slot <= 70) return "result log";
            return nullptr;
        }

        static void EmitKeylessRows(std::ostream& out,
                                    const std::vector<std::vector<std::string>>& rows,
                                    const std::vector<const char*>& notes)
        {
            std::vector<std::size_t> colWidth;
            for (const auto& row : rows)
            {
                if (colWidth.size() < row.size())
                    colWidth.resize(row.size(), 0);
                for (std::size_t c = 0; c < row.size(); ++c)
                    colWidth[c] = (std::max)(colWidth[c], row[c].size());
            }

            std::vector<std::string> lines;
            std::size_t lineWidth = 0;
            for (const auto& row : rows)
            {
                std::string body;
                for (std::size_t c = 0; c < row.size(); ++c)
                {
                    if (colWidth[c] == 0) continue;
                    body.append(row[c]);
                    body.append(colWidth[c] - row[c].size(), ' ');
                    body.push_back(' ');
                }
                while (!body.empty() && body.back() == ' ')
                    body.pop_back();
                lines.push_back("{ " + body + " },");
                lineWidth = (std::max)(lineWidth, lines.back().size());
            }

            for (std::size_t r = 0; r < lines.size(); ++r)
            {
                std::string line = lines[r];
                if (notes[r])
                {
                    line.append(lineWidth - line.size(), ' ');
                    line += "   -- ";
                    line += notes[r];
                }
                out << "        " << line << "\n";
            }
        }

        static void WriteDispatchSections_NoLock(std::ostream& out)
        {
            {
                std::vector<const V_FrameWorkState::DispatchMission*> sorted;
                for (const auto& kv : g_State.dispatchMissions)
                    sorted.push_back(&kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto* a, const auto* b) { return a->key < b->key; });

                std::vector<std::string> keys;
                std::vector<std::vector<std::string>> rows;
                for (const V_FrameWorkState::DispatchMission* mission : sorted)
                {
                    std::vector<std::string> row(6);
                    row[0] = "deployMissionId = " + std::to_string(mission->deployMissionId) + ",";
                    if (mission->rewardRow != 0)
                        row[1] = "rewardRow = " + std::to_string(mission->rewardRow) + ",";
                    if (mission->cleared)
                        row[2] = "cleared = true,";
                    if (mission->rewardPending)
                        row[3] = "rewardPending = true,";
                    const std::int32_t misses = DispatchMissesToWrite(*mission);
                    if (misses != 0)
                        row[4] = "misses = " + std::to_string(misses) + ",";
                    if (mission->rewardPending && mission->hasReward)
                        row[5] = DispatchRewardCells(mission->reward);
                    keys.push_back("[\"" + mission->key + "\"]");
                    rows.push_back(std::move(row));
                }

                out << "    dispatchMissions = {\n";
                EmitAlignedRows(out, keys, rows);
                out << "    },\n\n";
            }

            {
                std::vector<V_FrameWorkState::DispatchSlotFact> facts = g_State.dispatchSlots;
                std::stable_sort(facts.begin(), facts.end(),
                    [](const auto& a, const auto& b) { return a.slot < b.slot; });

                std::vector<std::vector<std::string>> rows;
                std::vector<const char*> notes;
                for (const V_FrameWorkState::DispatchSlotFact& fact : facts)
                {
                    std::vector<std::string> row(5);
                    row[0] = "slot = " + std::to_string(fact.slot) + ",";
                    row[1] = "mission = \"" + fact.key + "\",";
                    row[2] = "placeholderId = " + std::to_string(fact.placeholderId) + ",";
                    row[3] = "seed = " + std::to_string(fact.seed) + ",";
                    if (fact.misses != 0)
                        row[4] = "misses = " + std::to_string(fact.misses) + ",";
                    rows.push_back(std::move(row));
                    notes.push_back(DispatchSlotNote(fact.slot));
                }

                out << "    dispatchSlots = {\n";
                EmitKeylessRows(out, rows, notes);
                out << "    },\n\n";
            }

            {
                std::vector<std::string> keys;
                std::vector<std::vector<std::string>> rows;
                static const char* const kBandName[2] = { "N", "R" };
                for (int band = 0; band < 2; ++band)
                {
                    const V_FrameWorkState::DispatchCursor& cursor = g_State.dispatchCursors[band];
                    if (cursor.key.empty())
                        continue;
                    keys.push_back(std::string("[\"") + kBandName[band] + "\"]");
                    rows.push_back({ "mission = \"" + cursor.key + "\",",
                                     "anchor = " + std::to_string(cursor.anchor) + "," });
                }

                if (!keys.empty())
                {
                    out << "    dispatchCursor = {\n";
                    EmitAlignedRows(out, keys, rows);
                    out << "    },\n\n";
                }
            }
        }

        static void WriteCallMenuSection_NoLock(std::ostream& out)
        {
            std::vector<const V_FrameWorkState::CallMenuItem*> sorted;
            for (const auto& kv : g_State.callMenuItems)
                sorted.push_back(&kv.second);
            std::sort(sorted.begin(), sorted.end(),
                [](const auto* a, const auto* b) { return a->key < b->key; });

            std::vector<std::string> keys;
            std::vector<std::vector<std::string>> rows;
            for (const V_FrameWorkState::CallMenuItem* item : sorted)
            {
                std::vector<std::string> row(19);
                if (item->hasInPlaceOfOverride)
                    row[15] = "inPlaceOfOverride = " + std::to_string(item->inPlaceOfOverride) + ",";
                if (item->page > 0)
                {
                    row[16] = "page = " + std::to_string(item->page) + ",";
                    row[17] = "row = " + std::to_string(item->row) + ",";
                }
                if (item->misses != 0)
                    row[18] = "misses = " + std::to_string(item->misses) + ",";
                row[0] = "label = \"" + HexText(item->label) + "\",";
                if (item->description != item->label)
                    row[1] = "description = \"" + HexText(item->description) + "\",";
                row[2] = "column = " + std::to_string(item->column) + ",";
                if (item->when != 0)
                    row[3] = "when = " + std::to_string(item->when) + ",";
                if (item->message != 0)
                    row[4] = "message = \"" + HexText(item->message) + "\",";
                if (item->exclusive != 0)
                    row[5] = "exclusive = \"" + HexText(item->exclusive) + "\",";
                if (item->inPlaceOf >= 0)
                    row[6] = "inPlaceOf = " + std::to_string(item->inPlaceOf) + ",";
                if (item->icon != 0)
                    row[7] = "icon = " + std::to_string(item->icon) + ",";
                if (item->iconTexture != 0)
                    row[8] = "iconTexture = \"" + HexText(item->iconTexture) + "\",";
                if (item->iconCell != 0)
                    row[9] = "iconCell = " + std::to_string(item->iconCell) + ",";
                if (item->hasIconUvShift)
                    row[10] = "iconUvShift = \"" + FloatPairText(item->iconUvShift) + "\",";
                if (item->iconUvRepeat[0] != 0.5f || item->iconUvRepeat[1] != 0.5f)
                    row[11] = "iconUvRepeat = \"" + FloatPairText(item->iconUvRepeat) + "\",";
                if (!item->voices.empty())
                {
                    std::string voices;
                    for (const std::uint32_t voice : item->voices)
                        voices += (voices.empty() ? "" : " ") + HexText(voice);
                    row[12] = "voices = \"" + voices + "\",";
                }
                if (item->enabled != V_FrameWorkState::kNoOverride)
                    row[13] = item->enabled ? "enabled = true," : "enabled = false,";
                if (item->visible != V_FrameWorkState::kNoOverride)
                    row[14] = item->visible ? "visible = true," : "visible = false,";
                keys.push_back("[\"" + item->key + "\"]");
                rows.push_back(std::move(row));
            }

            out << "    callMenuItems = {\n";
            EmitAlignedRows(out, keys, rows);
            out << "    },\n\n";
        }

        static void WriteToDisk_NoLock()
        {
            EnsureSaveDirectory();

            const std::string tmpPath = std::string(kSavePath) + ".tmp";

            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                LogError("State: cannot write '%s', state not saved", tmpPath.c_str());
                return;
            }

            {
                std::vector<std::pair<std::string, EquipEntry>> sorted;
                sorted.reserve(g_State.equips.size());
                for (const auto& kv : g_State.equips)
                    sorted.emplace_back(kv.first, kv.second);

                std::unordered_map<std::string, std::int32_t> bareEquipId;
                for (const auto& kv : sorted)
                    if (kv.first.find(':') == std::string::npos
                        && kv.second.equipId != 0)
                        bareEquipId[kv.first] = kv.second.equipId;
                std::unordered_set<std::string> folded;
                for (auto& kv : sorted)
                {
                    const auto colon = kv.first.rfind(':');
                    if (colon == std::string::npos)
                        continue;
                    const auto b = bareEquipId.find(kv.first.substr(colon + 1));
                    if (b != bareEquipId.end())
                    {
                        kv.second.equipId = b->second;
                        folded.insert(b->first);
                    }
                }
                if (!folded.empty())
                    sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
                        [&](const std::pair<std::string, EquipEntry>& kv) {
                            return kv.first.find(':') == std::string::npos
                                && folded.count(kv.first) != 0;
                        }), sorted.end());

                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                std::vector<std::string> keys[5];
                std::vector<std::vector<std::string>> rows[5];
                for (const auto& kv : sorted)
                {
                    if (kv.second.developId == 0 && kv.second.flowIndex == 0 &&
                        kv.second.equipId == 0 && kv.second.subId == 0 &&
                        kv.second.partsType == 0 && kv.second.selector == 0 &&
                        kv.second.misses == 0 && kv.second.developed < 0 &&
                        !kv.second.isNew && !kv.second.devReqAnnounced)
                        continue;

                    std::vector<std::string> row(11);
                    auto cell = [&row](std::size_t i, const char* name,
                                       const std::string& value)
                    { row[i] = std::string(name) + " = " + value + ","; };

                    if (kv.second.developId != 0)
                        cell(0, "developId", std::to_string(kv.second.developId));
                    if (kv.second.flowIndex != 0)
                        cell(1, "flowIndex", std::to_string(kv.second.flowIndex));
                    if (kv.second.developed >= 0)
                        cell(2, "developed",
                             kv.second.developed == 1 ? "true" : "false");
                    if (kv.second.developId != 0)
                    {
                        if (!kv.second.isNew)
                            cell(3, "seen", "true");
                        if (!kv.second.devReqAnnounced)
                            cell(4, "announcePending", "true");
                    }
                    if (kv.second.equipId != 0)
                        cell(5, "equipId", std::to_string(kv.second.equipId));
                    if (kv.second.subId != 0)
                        cell(6, "subId", std::to_string(kv.second.subId));
                    if (kv.second.partsType != 0)
                        cell(7, "partsType", std::to_string(kv.second.partsType));
                    if (kv.second.selector != 0)
                        cell(8, "selector", std::to_string(kv.second.selector));
                    if (kv.second.misses != 0)
                        cell(9, "misses", std::to_string(kv.second.misses));
                    {
                        std::size_t last = 0;
                        for (std::size_t vi = 0;
                             vi < V_FrameWorkState::kPersistedVariantSelectorSlots; ++vi)
                            if (kv.second.variantSelectors[vi] != 0) last = vi + 1;
                        if (last != 0)
                        {
                            std::string csv = "\"";
                            for (std::size_t vi = 0; vi < last; ++vi)
                            {
                                if (vi) csv.push_back(',');
                                csv += std::to_string(
                                    static_cast<int>(kv.second.variantSelectors[vi]));
                            }
                            csv.push_back('"');
                            cell(10, "variantSelectors", csv);
                        }
                    }

                    int group = 4;
                    if (kv.second.kind == kRowKindHeadOption)
                        group = 3;
                    else if (kv.second.kind == kRowKindOutfit
                        || kv.second.partsType != 0 || kv.second.selector != 0)
                        group = 2;
                    else if (kv.second.kind == kRowKindWeapon)
                        group = 1;
                    else if (kv.second.developId == 0)
                        group = 0;

                    keys[group].push_back("[\"" + kv.first + "\"]");
                    rows[group].push_back(std::move(row));
                }

                std::size_t liveConstants = 0;
                for (const auto& kv : g_State.constants)
                    if (kv.second != 0) ++liveConstants;

                out <<
"-- V_FrameWork state file - written by V_FrameWork.dll.\n";
                out << "--   equips " << keys[0].size()
                    << "   weapons " << keys[1].size()
                    << "   outfits " << keys[2].size()
                    << "   headOptions " << keys[3].size();
                if (!keys[4].empty())
                    out << "   unclassified " << keys[4].size();
                out << "   tapes " << g_State.tapes.size()
                    << "   database " << g_State.bluePrints.size()
                    << "   dispatch " << g_State.dispatchMissions.size()
                    << "   callMenu " << g_State.callMenuItems.size()
                    << "   constants " << liveConstants
                    << "   pinned " << g_State.pinnedEquipIds.size() << "\n\n";

                out << "return {\n\n";

                static const char* const kEquipTable[5] =
                    { "equips", "weapons", "outfits", "headOptions", "develop" };
                for (int g = 0; g < 5; ++g)
                {
                    if (g == 4 && keys[g].empty())
                        continue;
                    out << "    " << kEquipTable[g] << " = {\n";
                    EmitAlignedRows(out, keys[g], rows[g]);
                    out << "    },\n\n";
                }
            }


            {
                std::vector<std::pair<std::string, TapeEntry>> sorted;
                sorted.reserve(g_State.tapes.size());
                for (const auto& kv : g_State.tapes)
                    sorted.emplace_back(kv.first, kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.second.saveIndex < b.second.saveIndex; });

                std::vector<std::string> keys;
                std::vector<std::vector<std::string>> rows;
                for (const auto& kv : sorted)
                {
                    std::vector<std::string> row(4);
                    row[0] = "saveIndex = "
                           + std::to_string(static_cast<int>(kv.second.saveIndex)) + ",";
                    row[1] = std::string("owned = ")
                           + (kv.second.owned ? "true" : "false") + ",";
                    row[2] = std::string("new = ")
                           + (kv.second.isNew ? "true" : "false") + ",";
                    if (kv.second.misses != 0)
                        row[3] = "misses = " + std::to_string(kv.second.misses) + ",";
                    keys.push_back("[\"" + kv.first + "\"]");
                    rows.push_back(std::move(row));
                }

                out << "    tapes = {\n";
                EmitAlignedRows(out, keys, rows);
                out << "    },\n\n";
            }


            {
                std::vector<std::pair<std::string, BluePrintEntry>> bpSorted;
                bpSorted.reserve(g_State.bluePrints.size());
                for (const auto& kv : g_State.bluePrints)
                    bpSorted.emplace_back(kv.first, kv.second);
                std::sort(bpSorted.begin(), bpSorted.end(),
                    [](const auto& a, const auto& b) { return a.second.id < b.second.id; });

                std::vector<std::string> keys;
                std::vector<std::vector<std::string>> rows;
                for (const auto& kv : bpSorted)
                {
                    std::vector<std::string> row(5);
                    row[0] = "bluePrintId = " + std::to_string(kv.second.id) + ",";
                    row[1] = "pickupNumber = "
                           + std::to_string(bluePrint::PublicId(kv.second.id)) + ",";
                    row[2] = std::string("owned = ")
                           + (kv.second.owned ? "true" : "false") + ",";
                    if (kv.second.isNew)
                        row[3] = "new = true,";
                    if (kv.second.misses != 0)
                        row[4] = "misses = " + std::to_string(kv.second.misses) + ",";
                    keys.push_back("[\"" + kv.first + "\"]");
                    rows.push_back(std::move(row));
                }

                out << "    DataBase = {\n";
                EmitAlignedRows(out, keys, rows);
                out << "    },\n\n";
            }

            WriteDispatchSections_NoLock(out);
            WriteCallMenuSection_NoLock(out);

            {
                std::vector<std::pair<std::string, std::int32_t>> sorted;
                sorted.reserve(g_State.constants.size());
                for (const auto& kv : g_State.constants)
                    if (kv.second != 0)
                        sorted.emplace_back(kv.first, kv.second);
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                out << "    constants = {\n";
                EmitConstantRows(out, sorted);
                out << "    },\n\n";
            }

            {
                const bool countMisses = CanCountConstantMisses();
                std::vector<std::pair<std::string, std::int32_t>> sorted;
                sorted.reserve(g_State.constants.size());
                for (const auto& kv : g_State.constants)
                {
                    if (IsNeverEvictConstantKey(kv.first))
                        continue;
                    if (g_ConstantsTouched.find(kv.first) != g_ConstantsTouched.end())
                        continue;
                    const auto mit = g_State.constantMisses.find(kv.first);
                    const std::int32_t loaded =
                        (mit != g_State.constantMisses.end()) ? mit->second : 0;
                    const std::int32_t misses = countMisses ? loaded + 1 : loaded;
                    if (misses != 0)
                        sorted.emplace_back(kv.first, misses);
                }
                std::sort(sorted.begin(), sorted.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });

                out << "    constantMisses = {\n";
                EmitConstantRows(out, sorted, false);
                out << "    },\n\n";
            }

            {
                std::vector<std::int32_t> sorted(g_State.pinnedEquipIds.begin(),
                                                 g_State.pinnedEquipIds.end());
                std::sort(sorted.begin(), sorted.end());
                std::size_t idWidth = 0;
                for (const std::int32_t id : sorted)
                {
                    const std::size_t w = std::to_string(id).size() + 4;
                    if (w > idWidth) idWidth = w;
                }

                out << "    loadoutPinnedIds = {\n";
                for (const std::int32_t id : sorted)
                {
                    std::string cell = "[\"" + std::to_string(id) + "\"]";
                    if (cell.size() < idWidth)
                        cell.append(idWidth - cell.size(), ' ');
                    out << "        " << cell << " = " << id << ",";
                    for (const auto& kv : g_State.equips)
                        if (kv.second.equipId == id)
                        {
                            out << "   -- " << kv.first;
                            break;
                        }
                    out << "\n";
                }
                out << "    },\n";
            }

            out << "}\n";
            out.close();
            if (!out)
            {
                LogError("State: write to '%s' failed, previous state kept", tmpPath.c_str());
                DeleteFileA(tmpPath.c_str());
                return;
            }

            DWORD lastErr = 0;
            bool replaced = false;
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (MoveFileExA(tmpPath.c_str(), kSavePath,
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    replaced = true;
                    break;
                }
                lastErr = GetLastError();
                if (lastErr != ERROR_ACCESS_DENIED && lastErr != ERROR_SHARING_VIOLATION)
                    break;
                Sleep(3);
            }
            if (!replaced)
            {
                LogError("State: cannot replace '%s' (err %lu), state left in '%s'",
                    kSavePath, lastErr, tmpPath.c_str());
                return;
            }
            g_State.dirty = false;
        }

        static bool IsEquipIdInUse_NoLock(std::int32_t id)
        {
            for (const auto& kv : g_SessionEquipIds)
                if (kv.second == id) return true;
            const std::int32_t slot = EquipIdCompression::ComputeCompressed(id);
            for (const auto& kv : g_State.equips)
                if (kv.second.equipId != 0 &&
                    EquipIdCompression::ComputeCompressed(kv.second.equipId) == slot)
                    return true;
            return false;
        }

        static bool IsDevelopIdInUse_NoLock(std::int32_t id)
        {
            for (const auto& kv : g_State.equips)
                if (kv.second.developId == id) return true;
            return false;
        }

        static bool IsTapeSaveIndexInUse_NoLock(std::int16_t idx)
        {
            return g_TapeSaveIndexInUse.find(idx) != g_TapeSaveIndexInUse.end();
        }

        static bool IsConstantValueInUse_NoLock(const std::string& spacePrefix, std::int32_t value)
        {
            for (const auto& kv : g_State.constants)
                if (kv.second == value &&
                    kv.first.compare(0, spacePrefix.size(), spacePrefix) == 0)
                    return true;
            return false;
        }

        static bool IsSafeConstantNamePart(const char* s)
        {
            for (const char* p = s; *p; ++p)
            {
                const char c = *p;
                if (c == '"' || c == '[' || c == ']' || c == '\\'
                    || c == '\n' || c == '\r')
                    return false;
            }
            return true;
        }

        static bool GuardStateKey(const char* key, const char* who)
        {
            bool bad = false;
            for (const char* p = key; *p; ++p)
                if (*p == '\n' || *p == '\r' || *p == '"' || *p == '\\')
                { bad = true; break; }
            if (!bad)
                return true;
            LogWarn("State: %s rejected key '%s' (line break, quote or backslash), not saved",
                who, key);
            return false;
        }




        static bool g_NativeTableSynced = false;
        static bool g_DevelopOwnershipScanned = false;

        static constexpr std::size_t kNativeOccupancyFloor = 256;

        static bool EnsureNativeSnapshot_NoLock()
        {
            if (g_NativeTableSynced)
                return true;
            const std::size_t occupied = EquipIdCompression::SyncFromNativeTable();
            if (occupied >= kNativeOccupancyFloor)
            {
                g_NativeTableSynced = true;
                return true;
            }
            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogError("State: native equip table has %zu rows (need %zu), new equipIds use the extended band",
                    occupied, kNativeOccupancyFloor);
            }
            return false;
        }

        static void ScanDevelopOwnership_NoLock()
        {
            if (g_DevelopOwnershipScanned)
                return;
            static std::uint64_t s_bits[equip::kVanillaEquipIdBitWords];
            if (equip::ScanVanillaDevelopEquipIds(s_bits, equip::kVanillaEquipIdBitWords) == 0)
                return;
            g_DevelopOwnershipScanned = true;
            for (std::size_t w = 0; w < equip::kVanillaEquipIdBitWords; ++w)
            {
                if (s_bits[w] == 0)
                    continue;
                for (std::uint32_t b = 0; b < 64; ++b)
                    if (s_bits[w] & (1ull << b))
                        g_VanillaIdentityIds.insert(static_cast<std::int32_t>(w * 64 + b));
            }
        }

        static const char* NativeOwner_NoLock(std::int32_t equipId)
        {
            if (!EquipIdCompression::IsLegalNativeCustomId(equipId))
                return "the engine's item and hand window or lies outside the item band";
            if (EquipIdCompression::IsCompressedSlotUsed(equipId))
                return "a native EquipIdTable row";
            if (g_VanillaIdentityIds.count(equipId) != 0)
                return "a vanilla develop row";
            return nullptr;
        }

        static bool IsPinnedEquipId_NoLock(std::int32_t equipId)
        {
            return g_State.pinnedEquipIds.count(equipId) != 0
                || g_SessionPinnedIds.count(equipId) != 0;
        }

        static bool IsEquipIdHeldByAnotherKey_NoLock(std::int32_t equipId,
                                                     const char* key)
        {
            for (const auto& kv : g_State.equips)
                if (kv.second.equipId == equipId
                    && (!key || kv.first != key))
                    return true;
            return false;
        }


        static std::int32_t AllocateNextFreeEquipId_NoLock(std::int32_t minimum,
                                                           const char* key)
        {
            const std::int32_t floor =
                (minimum > kFirstCustomEquipIdMinimum)
                    ? minimum
                    : kFirstCustomEquipIdMinimum;

            std::int32_t result = -1;
            if (EnsureNativeSnapshot_NoLock())
            {
                ScanDevelopOwnership_NoLock();
                const auto taken = [](std::int32_t equipId) {
                    return NativeOwner_NoLock(equipId) != nullptr
                        || IsEquipIdInUse_NoLock(equipId)
                        || IsPinnedEquipId_NoLock(equipId);
                };
                result = EquipIdCompression::FindLowestFreeEquipIdInRange(
                    taken, floor, EquipIdCompression::kItemBandLast);
            }
            if (result < 0)
            {
                result = EquipIdCompression::FindLowestFreeExtendedEquipId(
                    [key](std::int32_t id) {
                        return IsEquipIdHeldByAnotherKey_NoLock(id, key);
                    });
                if (result < 0)
                    LogError("State: '%s' got no equipId, native and extended bands full above 0x%X",
                        key ? key : "?", static_cast<unsigned>(floor));
            }
            return result;
        }


        static std::int32_t AllocateNextFreeDevelopId_NoLock(std::int32_t minimum)
        {
            std::int32_t id = (minimum > kFirstCustomDevelopId) ? minimum : kFirstCustomDevelopId;
            while (IsDevelopIdInUse_NoLock(id))
                ++id;
            return id;
        }

        static std::int16_t AllocateNextFreeTapeSaveIndex_NoLock(std::int16_t minimum)
        {
            std::int16_t idx = (minimum > kFirstCustomTapeSaveIndex) ? minimum : kFirstCustomTapeSaveIndex;
            while (idx <= kMaxCustomTapeSaveIndex && IsTapeSaveIndexInUse_NoLock(idx)) ++idx;
            return (idx <= kMaxCustomTapeSaveIndex) ? idx : static_cast<std::int16_t>(-1);
        }
    }

    void Load()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        g_State.dirty = true;
        WriteToDisk_NoLock();
    }

    void NoteInstallOutcome(bool allInstalled)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_LaunchHealthy = allInstalled;
    }

    void Save()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_SaveDueTick = 0;
        if (g_State.dirty)
            WriteToDisk_NoLock();
    }

    void SaveOnProcessExit()
    {
        std::unique_lock<std::mutex> lock(g_Mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            LogWarn("State: file locked at exit, this launch not recorded");
            return;
        }

        g_ExitSave = true;
        g_SaveDueTick = 0;
        WriteToDisk_NoLock();
    }

    void AbandonFlusherThread()
    {
        if (g_FlusherThread.joinable())
            g_FlusherThread.detach();
    }

    bool PersistNow()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        if (g_State.dirty)
        {
            g_SaveDueTick = 0;
            WriteToDisk_NoLock();
        }
        return !g_State.dirty;
    }

    void FlushPendingSaves()
    {
        std::thread joinMe;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_SaveDueTick != 0 || g_State.dirty)
            {
                g_SaveDueTick = 0;
                WriteToDisk_NoLock();
            }
            if (g_FlusherRunning)
            {
                g_FlusherStop = true;
                g_FlusherRunning = false;
                joinMe = std::move(g_FlusherThread);
            }
        }

        g_FlushCv.notify_all();
        if (joinMe.joinable())
            joinMe.join();
    }

    void BeginBatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        ++g_BatchDepth;
    }

    void EndBatch()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_BatchDepth > 0)
            --g_BatchDepth;
        if (g_BatchDepth == 0 && g_State.dirty)
            SaveToDisk_NoLock();
    }

    bool ResolveOrCreateEquipId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outEquipId,
        bool isWeapon)
    {
        outEquipId = 0;
        if (!key || !key[0]) return false;
        if (!GuardStateKey(key, "ResolveOrCreateEquipId")) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();


        auto it = g_SessionEquipIds.find(key);
        if (it != g_SessionEquipIds.end() && it->second != 0)
        {
            NoteClaimedEquipId_NoLock(it->second);
            outEquipId = it->second;
            return true;
        }

        std::int32_t movedFrom = 0;
        const char* movedBecause = nullptr;
        auto pit = g_State.equips.find(key);
        if (pit != g_State.equips.end() && pit->second.equipId != 0)
        {
            const std::int32_t persisted = pit->second.equipId;
            bool sessionTaken = false;
            for (const auto& kv : g_SessionEquipIds)
                if (kv.second == persisted) { sessionTaken = true; break; }
            const bool isExtended = EquipIdCompression::IsExtendedEquipId(persisted);
            const char* reason = nullptr;
            if (isExtended)
            {
                if (persisted < EquipIdCompression::kExtendedAllocFirst)
                    reason = "the retired extended band 0x289-0x6FF, whose fold can alias a vanilla row";
                else if (EquipIdCompression::IsExtendedEquipIdUsed(persisted))
                    reason = "an extended id another key already claimed";
            }
            else if (EnsureNativeSnapshot_NoLock())
            {
                ScanDevelopOwnership_NoLock();
                reason = NativeOwner_NoLock(persisted);
            }
            if (!sessionTaken && !reason)
            {
                g_SessionEquipIds[key] = persisted;
                NoteClaimedEquipId_NoLock(persisted);
                if (isExtended)
                    EquipIdCompression::MarkExtendedEquipIdUsed(persisted);
                if (pit->second.misses != 0)
                {
                    pit->second.misses = 0;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                outEquipId = persisted;
                return true;
            }
            if (!sessionTaken)
            {
                movedFrom = persisted;
                movedBecause = reason;
            }
        }

        const std::int32_t newId = AllocateNextFreeEquipId_NoLock(minimumId, key);
        if (newId < 0)
        {
            if (movedFrom == 0 || EquipIdCompression::IsExtendedEquipId(movedFrom)
                || EquipIdCompression::IsCompressedSlotUsed(movedFrom))
            {
                outEquipId = 0;
                return false;
            }
            LogError("State: '%s' keeps equipId 0x%X on %s, no free equipId to move to",
                key, static_cast<unsigned>(movedFrom), movedBecause);
            g_SessionEquipIds[key] = movedFrom;
            NoteClaimedEquipId_NoLock(movedFrom);
            outEquipId = movedFrom;
            return true;
        }

        if (movedFrom != 0)
            LogWarn("State: '%s' equipId moved 0x%X -> 0x%X (old id on %s), saved loadout cells blanked",
                key, static_cast<unsigned>(movedFrom), static_cast<unsigned>(newId), movedBecause);

        g_SessionEquipIds[key] = newId;
        NoteClaimedEquipId_NoLock(newId);
        g_State.equips[key].equipId = newId;
        g_State.equips[key].misses = 0;
        g_State.dirty = true;
        SaveToDisk_NoLock();
        outEquipId = newId;

        return true;
    }

    bool IsClaimedEquipId(std::int32_t equipId)
    {
        if (equipId <= 0 || equipId > 0xFFFF)
            return false;
        return (g_ClaimedEquipBits[equipId >> 6].load(std::memory_order_relaxed)
                & (1ull << (equipId & 63))) != 0;
    }

    void SetVanillaIdentityEquipIds(const std::int32_t* equipIds,
                                    std::size_t count)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);

        for (std::size_t i = 0; i < count; ++i)
            if (equipIds[i] > 0)
                g_VanillaIdentityIds.insert(equipIds[i]);

        static std::unordered_set<std::string> s_reported;
        for (const auto& kv : g_SessionEquipIds)
        {
            if (kv.second == 0 || g_VanillaIdentityIds.count(kv.second) == 0)
                continue;
            if (!s_reported.insert(kv.first).second)
                continue;
            LogWarn("State: '%s' equipId 0x%X collides with a vanilla develop row this launch, moves next launch",
                kv.first.c_str(), static_cast<unsigned>(kv.second));
        }
    }

    static bool IsOwnedEquipId_NoLock(std::int32_t equipId)
    {
        for (const auto& kv : g_SessionEquipIds)
            if (kv.second == equipId) return true;
        for (const auto& kv : g_State.equips)
            if (kv.second.equipId == equipId) return true;
        return false;
    }

    static void EnsurePinSetFresh_NoLock()
    {
        if (g_PinSetFreshThisSession)
            return;
        g_PinSetFreshThisSession = true;
        g_SessionPinnedIds.clear();
        g_StickyPinnedIds.clear();
        if (!g_State.pinnedEquipIds.empty())
        {
            g_State.pinnedEquipIds.clear();
            g_State.dirty = true;
        }
    }

    static void AddPin_NoLock(std::int32_t equipId, bool sticky)
    {
        if (equipId <= 0) return;
        LoadFromDisk_NoLock();
        if (!IsOwnedEquipId_NoLock(equipId))
            return;
        EnsurePinSetFresh_NoLock();
        if (sticky)
            g_StickyPinnedIds.insert(equipId);
        if (g_SessionPinnedIds.insert(equipId).second)
        {
            g_State.pinnedEquipIds.insert(equipId);
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void NotePinnedEquipId(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        AddPin_NoLock(equipId, false);
    }

    void NoteStickyPinnedEquipId(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        AddPin_NoLock(equipId, true);
    }

    void UnpinEquipId(std::int32_t equipId)
    {
        if (equipId <= 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!g_PinSetFreshThisSession)
            return;
        if (g_StickyPinnedIds.find(equipId) != g_StickyPinnedIds.end())
            return;
        LoadFromDisk_NoLock();
        bool changed = g_SessionPinnedIds.erase(equipId) != 0;
        if (g_State.pinnedEquipIds.erase(equipId) != 0)
            changed = true;
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    bool ResolveOrCreateDevelopId(
        const char* key,
        std::int32_t minimumId,
        std::int32_t& outDevelopId,
        bool* outCreated)
    {
        outDevelopId = 0;
        if (outCreated) *outCreated = false;
        if (!key || !key[0]) return false;
        if (!GuardStateKey(key, "ResolveOrCreateDevelopId")) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.equips.find(key);
        if (it != g_State.equips.end() && it->second.developId != 0)
        {
            it->second.misses = 0;
            g_State.dirty = true;
            outDevelopId = it->second.developId;
            if (outCreated) *outCreated = false;
            return true;
        }

        const std::int32_t newId = AllocateNextFreeDevelopId_NoLock(minimumId);
        g_State.equips[key].developId = newId;
        g_State.equips[key].misses = 0;
        g_State.dirty = true;
        outDevelopId = newId;
        if (outCreated) *outCreated = true;

        SaveToDisk_NoLock();

        return true;
    }

    void SetRowKind(const char* key, std::uint8_t kind)
    {
        if (!key || !key[0] || kind == kRowKindUnknown)
            return;
        if (!GuardStateKey(key, "SetRowKind"))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto& e = g_State.equips[key];
        if (e.kind != kind)
        {
            e.kind = kind;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    std::int32_t GetDevelopIdByKey(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        return (it != g_State.equips.end()) ? it->second.developId : 0;
    }

    std::int32_t GetDevelopIdAtOldFlowIndex(std::int32_t oldFlowIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_OldFlowLayout.find(oldFlowIndex);
        return (it != g_OldFlowLayout.end()) ? it->second : 0;
    }

    std::int32_t GetFlowIndexByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.flowIndex;
        return 0;
    }

    std::vector<std::int32_t> TakePendingDevelopedResets()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::vector<std::int32_t> out;
        out.swap(g_PendingDevelopedResets);
        return out;
    }

    bool ResolveDevelopedFlag(const char* key, bool defaultDeveloped)
    {
        if (!key || !key[0]) return defaultDeveloped;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end())
            return defaultDeveloped;
        if (it->second.developed < 0)
        {
            it->second.developed = defaultDeveloped ? 1 : 0;
            it->second.isNew = true;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
        return it->second.developed == 1;
    }

    bool IsManagedDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return true;
        return false;
    }

    void SetDevelopedByDevelopId(std::int32_t developId, bool developed)
    {
        if (developId == 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        const std::int8_t v = developed ? 1 : 0;
        for (auto& kv : g_State.equips)
        {
            if (kv.second.developId == developId)
            {
                if (kv.second.developed != v)
                {
                    kv.second.developed = v;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                return;
            }
        }
    }

    bool GetDevelopedByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.developed == 1;
        return false;
    }

    bool IsExplicitlyUndevelopedByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.developed == 0;
        return false;
    }

    void ForEachManagedDevelop(
        const std::function<void(std::int32_t developId, bool developed, bool isNew)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId != 0)
                callback(kv.second.developId, kv.second.developed == 1, kv.second.isNew);
    }

    void ForEachManagedDevelopRow(
        const std::function<void(std::int32_t developId, std::int32_t flowIndex,
                                 bool reqAnnounced)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId != 0)
                callback(kv.second.developId, kv.second.flowIndex,
                         kv.second.devReqAnnounced);
    }

    void SetNewByDevelopId(std::int32_t developId, bool isNew)
    {
        if (developId == 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (auto& kv : g_State.equips)
        {
            if (kv.second.developId == developId)
            {
                if (kv.second.isNew != isNew)
                {
                    kv.second.isNew = isNew;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                return;
            }
        }
    }

    bool IsDevelopedByFlowRowDevelopId(std::int32_t developId, bool& developed)
    {
        if (developId == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
        {
            if (kv.second.developId != developId || kv.second.developed < 0)
                continue;
            developed = (kv.second.developed == 1);
            return true;
        }
        return false;
    }

    bool GetDevReqAnnouncedByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return true;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.devReqAnnounced;
        return false;
    }

    void SetDevReqAnnouncedByDevelopId(std::int32_t developId, bool announced)
    {
        if (developId == 0) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (auto& kv : g_State.equips)
        {
            if (kv.second.developId == developId)
            {
                if (kv.second.devReqAnnounced != announced)
                {
                    kv.second.devReqAnnounced = announced;
                    g_State.dirty = true;
                    SaveToDisk_NoLock();
                }
                return;
            }
        }
    }

    bool GetNewByDevelopId(std::int32_t developId)
    {
        if (developId == 0) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
            if (kv.second.developId == developId)
                return kv.second.isNew;
        return false;
    }

    bool ResolveOrCreateFlowIndex(
        const char* key,
        std::int32_t minimumIndex,
        std::int32_t& outFlowIndex)
    {
        (void)minimumIndex;
        outFlowIndex = 0;
        if (!key || !key[0]) return false;
        if (!GuardStateKey(key, "ResolveOrCreateFlowIndex")) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto sit = g_SessionFlowIndices.find(key);
        if (sit != g_SessionFlowIndices.end())
        {
            if (!IsReservedFlowIndex(sit->second))
            {
                outFlowIndex = sit->second;
                return true;
            }
            LogWarn("State: '%s' flow index %d is a gunsmith slot, reallocating",
                key, sit->second);
            g_SessionFlowIndices.erase(sit);
        }

        const std::int32_t flowBound = NativeFlowIndexBound();
        std::int32_t newIdx = 0;
        {
            std::vector<bool> used(
                static_cast<std::size_t>(flowBound
                                         - kFirstCustomFlowIndex), false);
            for (const auto& kv : g_SessionFlowIndices)
                if (kv.second >= kFirstCustomFlowIndex
                    && kv.second < flowBound)
                    used[static_cast<std::size_t>(
                        kv.second - kFirstCustomFlowIndex)] = true;
            for (std::int32_t i = kFirstCustomFlowIndex;
                 i < flowBound; ++i)
            {
                if (i == kNativeFlowSentinel || IsReservedFlowIndex(i))
                    continue;
                if (!used[static_cast<std::size_t>(i - kFirstCustomFlowIndex)])
                {
                    newIdx = i;
                    break;
                }
            }
        }
        if (newIdx == 0)
        {
            LogWarn("State: '%s' refused, develop flow indices %d..%d full, item stays out of R&D",
                key, kFirstCustomFlowIndex, flowBound - 1);
            return false;
        }

        g_SessionFlowIndices[key] = newIdx;

        g_State.equips[key].flowIndex = newIdx;
        g_State.dirty = true;
        outFlowIndex = newIdx;

        SaveToDisk_NoLock();

        return true;
    }

    std::int32_t GetPersistedFlowIndex(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;
        return it->second.flowIndex;
    }

    void SetSessionFlowIndex(const char* key, std::int32_t flowIndex)
    {
        if (!key || !key[0] || flowIndex <= 0) return;
        if (!GuardStateKey(key, "SetSessionFlowIndex")) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        bool dirty = false;
        for (auto it = g_SessionFlowIndices.begin();
             it != g_SessionFlowIndices.end();)
        {
            if (it->second == flowIndex && it->first != key)
            {
                auto itE = g_State.equips.find(it->first);
                if (itE != g_State.equips.end() && itE->second.flowIndex != 0)
                {
                    itE->second.flowIndex = 0;
                    dirty = true;
                }
                it = g_SessionFlowIndices.erase(it);
            }
            else
                ++it;
        }

        for (auto& kv : g_State.equips)
        {
            if (kv.first != key && kv.second.flowIndex == flowIndex)
            {
                kv.second.flowIndex = 0;
                dirty = true;
            }
        }

        auto sit = g_SessionFlowIndices.find(key);
        if (sit == g_SessionFlowIndices.end() || sit->second != flowIndex)
            g_SessionFlowIndices[key] = flowIndex;

        auto& e = g_State.equips[key];
        if (e.flowIndex != flowIndex)
        {
            e.flowIndex = flowIndex;
            dirty = true;
        }

        if (dirty)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void ReleaseSessionFlowIndex(const char* key)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_SessionFlowIndices.find(key);
        if (it != g_SessionFlowIndices.end())
            g_SessionFlowIndices.erase(it);

        auto itE = g_State.equips.find(key);
        if (itE != g_State.equips.end() && itE->second.flowIndex != 0)
        {
            itE->second.flowIndex = 0;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    bool ResolveOrCreateConstantValue(
        const char* spaceTag,
        const char* name,
        std::int32_t minimumValue,
        std::int32_t& outValue)
    {
        outValue = 0;
        if (!spaceTag || !spaceTag[0] || !name || !name[0]) return false;
        if (!IsSafeConstantNamePart(spaceTag) || !IsSafeConstantNamePart(name))
        {
            LogWarn("Constants: '%s' (space '%s') rejected, unsafe characters", name, spaceTag);
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        const std::string prefix = std::string(spaceTag) + ":";
        const std::string key = prefix + name;

        auto it = g_State.constants.find(key);
        if (it != g_State.constants.end() && it->second != 0)
        {
            g_ConstantsTouched.insert(key);
            outValue = it->second;
            return true;
        }

        std::int32_t value = (minimumValue > 0) ? minimumValue : 1;
        while (value <= kMaxCustomConstantValue && IsConstantValueInUse_NoLock(prefix, value))
            ++value;
        if (value > kMaxCustomConstantValue)
        {
            LogError("Constants: no free value for '%s', space '%s' pool %d..%d full",
                name, spaceTag, minimumValue, kMaxCustomConstantValue);
            return false;
        }

        g_State.constants[key] = value;
        g_ConstantsTouched.insert(key);
        g_State.dirty = true;
        outValue = value;

        SaveToDisk_NoLock();

        return true;
    }

    std::int32_t GetPersistedConstant(const char* spaceTag, const char* name)
    {
        if (!spaceTag || !spaceTag[0] || !name || !name[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        const std::string key = std::string(spaceTag) + ":" + name;
        auto it = g_State.constants.find(key);
        if (it == g_State.constants.end())
            return 0;
        g_ConstantsTouched.insert(key);
        return it->second;
    }

    void SetPersistedConstant(const char* spaceTag, const char* name,
                              std::int32_t value)
    {
        if (!spaceTag || !spaceTag[0] || !name || !name[0]) return;
        if (!IsSafeConstantNamePart(spaceTag) || !IsSafeConstantNamePart(name))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        const std::string key = std::string(spaceTag) + ":" + name;
        g_ConstantsTouched.insert(key);
        auto it = g_State.constants.find(key);
        if (it != g_State.constants.end() && it->second == value)
            return;
        g_State.constants[key] = value;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    void ForEachPersistedConstant(const char* spaceTag,
                                  void (*fn)(const char* name, std::int32_t value))
    {
        if (!spaceTag || !spaceTag[0] || !fn) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        const std::string prefix = std::string(spaceTag) + ":";
        for (const auto& kv : g_State.constants)
            if (kv.first.compare(0, prefix.size(), prefix) == 0)
                fn(kv.first.c_str() + prefix.size(), kv.second);
    }

    std::uint8_t GetPersistedOutfitPartsType(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;
        const std::int32_t v = it->second.partsType;
        return (v > 0 && v <= 0xFF) ? static_cast<std::uint8_t>(v) : 0;
    }

    std::uint8_t GetPersistedOutfitSelector(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;
        const std::int32_t v = it->second.selector;
        return (v > 0 && v <= 0xFF) ? static_cast<std::uint8_t>(v) : 0;
    }

    void SetPersistedOutfitIds(const char* key,
                               std::uint8_t partsType,
                               std::uint8_t selector)
    {
        if (!key || !key[0]) return;
        if (!GuardStateKey(key, "SetPersistedOutfitIds")) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto& e = g_State.equips[key];
        bool changed = false;
        if (partsType != 0 && e.partsType != static_cast<std::int32_t>(partsType))
        {
            e.partsType = static_cast<std::int32_t>(partsType);
            changed = true;
        }
        if (selector != 0 && e.selector != static_cast<std::int32_t>(selector))
        {
            e.selector = static_cast<std::int32_t>(selector);
            changed = true;
        }
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    std::size_t GetPersistedOutfitVariantSelectors(const char* key,std::uint8_t* out,std::size_t cap)
    {
        if (!key || !key[0] || !out || cap == 0) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return 0;

        if (it->second.misses != 0)
        {
            it->second.misses = 0;
            g_State.dirty = true;
        }

        std::size_t nonZero = 0;
        const std::size_t n =
            (cap < V_FrameWorkState::kPersistedVariantSelectorSlots)
                ? cap : V_FrameWorkState::kPersistedVariantSelectorSlots;
        for (std::size_t i = 0; i < n; ++i)
        {
            out[i] = it->second.variantSelectors[i];
            if (out[i] != 0) ++nonZero;
        }
        for (std::size_t i = n; i < cap; ++i) out[i] = 0;
        return nonZero;
    }

    void SetPersistedOutfitVariantSelectors(const char* key, const std::uint8_t* selectors, std::size_t count)
    {
        if (!key || !key[0]) return;
        if (!GuardStateKey(key, "SetPersistedOutfitVariantSelectors")) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto& e = g_State.equips[key];
        bool changed = false;

        if (e.misses != 0)
        {
            e.misses = 0;
            changed = true;
        }
        for (std::size_t i = 0;
             i < V_FrameWorkState::kPersistedVariantSelectorSlots; ++i)
        {
            const std::uint8_t v =
                (selectors && i < count) ? selectors[i] : std::uint8_t{0};
            if (e.variantSelectors[i] != v)
            {
                e.variantSelectors[i] = v;
                changed = true;
            }
        }
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void ClearPersistedOutfitIds(const char* key)
    {
        if (!key || !key[0]) return;
        if (!GuardStateKey(key, "ClearPersistedOutfitIds")) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.equips.find(key);
        if (it == g_State.equips.end()) return;
        auto& e = it->second;
        bool changed = false;
        if (e.partsType != 0) { e.partsType = 0; changed = true; }
        if (e.selector != 0)  { e.selector = 0;  changed = true; }
        for (std::size_t i = 0;
             i < V_FrameWorkState::kPersistedVariantSelectorSlots; ++i)
        {
            if (e.variantSelectors[i] != 0)
            {
                e.variantSelectors[i] = 0;
                changed = true;
            }
        }
        if (changed)
        {
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void ForEachPersistedOutfit(
        const std::function<void(const std::string& key, std::uint8_t partsType, std::uint8_t selector, const std::uint8_t* variants)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.equips)
        {
            const auto& e = kv.second;
            const std::uint8_t pt =
                (e.partsType > 0 && e.partsType <= 0xFF)
                    ? static_cast<std::uint8_t>(e.partsType) : std::uint8_t{0};
            const std::uint8_t sel =
                (e.selector > 0 && e.selector <= 0xFF)
                    ? static_cast<std::uint8_t>(e.selector) : std::uint8_t{0};
            bool anyVariant = false;
            for (std::size_t i = 0;
                 i < V_FrameWorkState::kPersistedVariantSelectorSlots; ++i)
                if (e.variantSelectors[i] != 0) { anyVariant = true; break; }
            if (pt == 0 && sel == 0 && !anyVariant) continue;
            callback(kv.first, pt, sel, e.variantSelectors);
        }
    }

    bool ResolveOrCreateTapeSaveIndex(
        const char* key,
        std::int16_t minimumIndex,
        std::int16_t& outSaveIndex)
    {
        outSaveIndex = -1;
        if (!key || !key[0]) return false;
        if (!GuardStateKey(key, "ResolveOrCreateTapeSaveIndex")) return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.saveIndex > 0)
        {
            if (it->second.misses != 0)
            {
                it->second.misses = 0;
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
            outSaveIndex = it->second.saveIndex;
            return true;
        }

        const std::int16_t newIdx = AllocateNextFreeTapeSaveIndex_NoLock(minimumIndex);
        if (newIdx < 0)
        {
            LogError("CustomTapes: '%s' not registered, save-index pool 300-32000 full", key);
            return false;
        }

        g_State.tapes[key].saveIndex = newIdx;
        g_TapeSaveIndexInUse.insert(newIdx);
        g_State.dirty = true;
        outSaveIndex = newIdx;
        LogInfo("CustomTapes: '%s' added, saveIndex %d", key, static_cast<int>(newIdx));

        SaveToDisk_NoLock();

        return true;
    }

    bool ResolveOrCreateBluePrintId(const char* key, std::int32_t& outId)
    {
        outId = 0;
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.bluePrints.find(key);
        if (it != g_State.bluePrints.end() && it->second.id > 0)
        {
            outId = it->second.id;
            if (it->second.misses != 0)
            {
                it->second.misses = 0;
                g_State.dirty = true;
                SaveToDisk_NoLock();
            }
            return true;
        }

        std::unordered_set<std::int32_t> used;
        for (const auto& kv : g_State.bluePrints)
            if (kv.second.id > 0)
                used.insert(kv.second.id);

        std::int32_t value = 1;
        while (used.find(value) != used.end())
            ++value;

        g_State.bluePrints[key].id = value;
        g_State.dirty = true;
        SaveToDisk_NoLock();
        outId = value;
        return true;
    }

    std::int32_t GetBluePrintId(const char* key)
    {
        if (!key || !key[0]) return 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.bluePrints.find(key);
        return (it != g_State.bluePrints.end()) ? it->second.id : 0;
    }

    void ForEachBluePrint(void (*fn)(const char* key, std::int32_t id, bool owned))
    {
        if (!fn) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.bluePrints)
            fn(kv.first.c_str(), kv.second.id, kv.second.owned);
    }

    void SetBluePrintOwned(const char* key, bool owned)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.bluePrints.find(key);
        if (it != g_State.bluePrints.end() && it->second.owned == owned)
            return;
        g_State.bluePrints[key].owned = owned;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    bool GetBluePrintOwned(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.bluePrints.find(key);
        return (it != g_State.bluePrints.end()) ? it->second.owned : false;
    }

    void SetBluePrintNew(const char* key, bool isNew)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.bluePrints.find(key);
        if (it != g_State.bluePrints.end() && it->second.isNew == isNew)
            return;
        g_State.bluePrints[key].isNew = isNew;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    bool GetBluePrintNew(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.bluePrints.find(key);
        return (it != g_State.bluePrints.end()) ? it->second.isNew : false;
    }

    void SetTapeOwned(const char* key, bool owned)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.owned != owned)
        {
            it->second.owned = owned;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void SetTapeNew(const char* key, bool isNew)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.tapes.find(key);
        if (it != g_State.tapes.end() && it->second.isNew != isNew)
        {
            it->second.isNew = isNew;
            g_State.dirty = true;
            SaveToDisk_NoLock();
        }
    }

    void SetTapeOwnedBySaveIndex(std::int16_t saveIndex, bool owned)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (auto& kv : g_State.tapes)
        {
            if (kv.second.saveIndex == saveIndex && kv.second.owned != owned)
            {
                kv.second.owned = owned;
                g_State.dirty = true;
                SaveToDisk_NoLock();
                return;
            }
        }
    }

    void SetTapeNewBySaveIndex(std::int16_t saveIndex, bool isNew)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (auto& kv : g_State.tapes)
        {
            if (kv.second.saveIndex == saveIndex && kv.second.isNew != isNew)
            {
                kv.second.isNew = isNew;
                g_State.dirty = true;
                SaveToDisk_NoLock();
                return;
            }
        }
    }

    bool GetTapeOwned(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.tapes.find(key);
        return (it != g_State.tapes.end()) ? it->second.owned : false;
    }

    bool GetTapeNew(const char* key)
    {
        if (!key || !key[0]) return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        auto it = g_State.tapes.find(key);
        return (it != g_State.tapes.end()) ? it->second.isNew : false;
    }

    void ForEachTape(
        const std::function<void(const std::string&, std::int16_t, bool, bool)>& callback)
    {
        if (!callback) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        for (const auto& kv : g_State.tapes)
            callback(kv.first, kv.second.saveIndex, kv.second.owned, kv.second.isNew);
    }

    void SetDispatchMission(const DispatchMission& mission)
    {
        if (mission.key.empty()) return;
        if (!GuardStateKey(mission.key.c_str(), "SetDispatchMission")) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        DispatchMission updated = mission;
        if (updated.hasReward)
            NormalizeDispatchReward(updated.reward);
        else
            std::memset(updated.reward, 0, sizeof(updated.reward));

        auto it = g_State.dispatchMissions.find(mission.key);
        updated.misses = (it != g_State.dispatchMissions.end()) ? it->second.misses : 0;
        if (it != g_State.dispatchMissions.end() && SameDispatchMission(it->second, updated))
            return;

        g_State.dispatchMissions[mission.key] = updated;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    void MarkDispatchMissionRegistered(const char* key)
    {
        if (!key || !key[0]) return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        g_DispatchTouched.insert(key);
    }

    std::vector<DispatchMission> GetDispatchMissions()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        std::vector<DispatchMission> missions;
        missions.reserve(g_State.dispatchMissions.size());
        for (const auto& kv : g_State.dispatchMissions)
            missions.push_back(kv.second);
        return missions;
    }

    std::vector<DispatchSlotFact> GetDispatchSlotFacts()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        return g_State.dispatchSlots;
    }

    void SetDispatchSlotFacts(const std::vector<DispatchSlotFact>& facts)
    {
        for (const DispatchSlotFact& fact : facts)
            if (!GuardStateKey(fact.key.c_str(), "SetDispatchSlotFacts"))
                return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        g_State.dispatchSlots = facts;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    DispatchCursor GetDispatchCursor(bool rareBand)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        return g_State.dispatchCursors[rareBand ? 1 : 0];
    }

    void SetDispatchCursor(bool rareBand, const DispatchCursor& cursor)
    {
        if (!cursor.key.empty() && !GuardStateKey(cursor.key.c_str(), "SetDispatchCursor"))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        DispatchCursor& stored = g_State.dispatchCursors[rareBand ? 1 : 0];
        if (stored.key == cursor.key && stored.anchor == cursor.anchor)
            return;
        stored = cursor;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    void SetCallMenuItem(const CallMenuItem& item)
    {
        if (item.key.empty() || !GuardStateKey(item.key.c_str(), "SetCallMenuItem"))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();

        auto it = g_State.callMenuItems.find(item.key);
        if (it != g_State.callMenuItems.end() && SameCallMenuItem(it->second, item))
            return;

        g_State.callMenuItems[item.key] = item;
        g_State.dirty = true;
        SaveToDisk_NoLock();
    }

    std::vector<CallMenuItem> GetCallMenuItems()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        LoadFromDisk_NoLock();
        std::vector<CallMenuItem> items;
        items.reserve(g_State.callMenuItems.size());
        for (const auto& kv : g_State.callMenuItems)
            items.push_back(kv.second);
        return items;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_State.loaded = false;
        g_State.dirty = false;
        g_State.equips.clear();
        g_State.tapes.clear();
        g_State.bluePrints.clear();
        g_State.constants.clear();
        g_State.constantMisses.clear();
        g_State.pinnedEquipIds.clear();
        g_State.dispatchMissions.clear();
        g_State.dispatchSlots.clear();
        g_State.dispatchCursors[0] = {};
        g_State.dispatchCursors[1] = {};
        g_State.callMenuItems.clear();
        g_ConstantsTouched.clear();
        g_DispatchTouched.clear();
        g_ExitSave = false;
        g_SessionPinnedIds.clear();
        g_StickyPinnedIds.clear();
        g_PinSetFreshThisSession = false;
        g_SessionEquipIds.clear();
        g_SessionFlowIndices.clear();
        g_OldFlowLayout.clear();
        g_PendingDevelopedResets.clear();
    }
}
