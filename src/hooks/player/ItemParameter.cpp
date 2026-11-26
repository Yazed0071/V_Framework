#include <pch.h>
#include "ItemParameter.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipControllerImpl_UpdateBattery.h"

namespace
{
    using GetItemParamWithEqpId_t = std::uint64_t(__fastcall*)(void* self, float* out, std::uint32_t equipId, std::uint32_t level, std::uint32_t paramType);

    constexpr int kMaxGrades = 5;

    struct Tunable
    {
        std::uint32_t equipId;
        int           grades;
        std::uint32_t countType;     // the item parameter that holds the uses carried
        std::uint32_t secondsType;   // the one that holds the effect length, 0 = the item has none in this table
    };

    const Tunable kTunables[] = {
        { 0x1C2, 3, 0x08, 0x07 },   // EQP_IT_InstantStealth
        { 0x1C3, 3, 0x0B, 0x00 },   // EQP_IT_TimeCigarette
        { 0x1E3, 3, 0x0D, 0x0C },   // EQP_IT_Pentazemin
        { 0x1E4, 5, 0x0F, 0x0E },   // EQP_IT_Clairvoyance
        { 0x1E5, 5, 0x11, 0x10 },   // EQP_IT_ReflexMedicine
        { 0x1E6, 3, 0x13, 0x00 },   // EQP_IT_ParasiteMist
        { 0x1E7, 3, 0x15, 0x00 },   // EQP_IT_ParasiteCamouf
        { 0x1E8, 3, 0x18, 0x00 },   // EQP_IT_ParasiteHard
    };
    constexpr int kTunableCount = static_cast<int>(sizeof(kTunables) / sizeof(kTunables[0]));

    const std::uint8_t kPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8D,
                                       0x54, 0x24, 0x30, 0x33, 0xDB, 0x48, 0x83, 0xC1, 0xE0, 0x48, 0x89, 0x5C, 0x24, 0x30, 0xE8 };

    std::atomic<std::uint32_t> g_Bits[kTunableCount][kMaxGrades][2];   // float bits, 0 = the value of the game
    std::atomic<bool>          g_Any{ false };

    GetItemParamWithEqpId_t g_Orig   = nullptr;
    void*                   g_Target = nullptr;

    int FindTunable(std::uint32_t equipId)
    {
        for (int i = 0; i < kTunableCount; ++i)
            if (kTunables[i].equipId == equipId)
                return i;
        return -1;
    }

    std::uint64_t __fastcall hkGetItemParamWithEqpId(void* self, float* out, std::uint32_t equipId, std::uint32_t level, std::uint32_t paramType)
    {
        if (out && g_Any.load(std::memory_order_acquire))
        {
            const int i = FindTunable(equipId);
            if (i >= 0)
            {
                const Tunable& item = kTunables[i];
                const int field = paramType == item.countType ? 0 : (item.secondsType != 0 && paramType == item.secondsType) ? 1 : -1;
                if (field >= 0)
                {
                    const int grade = level < 1 ? 1 : static_cast<int>(level) > item.grades ? item.grades : static_cast<int>(level);   // the clamp of the game
                    const std::uint32_t bits = g_Bits[i][grade - 1][field].load(std::memory_order_acquire);
                    if (bits != 0 && ItemBattery::IsActiveHere())
                    {
                        std::memcpy(out, &bits, sizeof(bits));
                        return 1;
                    }
                }
            }
        }
        return g_Orig(self, out, equipId, level, paramType);
    }
}

int ItemParameter::GradeCount(std::uint32_t equipId)
{
    const int i = FindTunable(equipId);
    return i < 0 ? 0 : kTunables[i].grades;
}

bool ItemParameter::HasField(std::uint32_t equipId, Field field)
{
    const int i = FindTunable(equipId);
    return i >= 0 && (field == Field::Count || kTunables[i].secondsType != 0);
}

void ItemParameter::Set(std::uint32_t equipId, int grade, Field field, float value)
{
    const int i = FindTunable(equipId);
    if (i < 0 || grade < 1 || grade > kTunables[i].grades || !HasField(equipId, field) || !(value > 0.0f))
        return;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    g_Bits[i][grade - 1][field == Field::Count ? 0 : 1].store(bits, std::memory_order_release);
    g_Any.store(true, std::memory_order_release);
}

bool Install_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook()
{
    void* target = gAddr.SkillAndItemParameterSystemImpl_GetItemParamWithEqpId
        ? ResolveGameAddress(gAddr.SkillAndItemParameterSystemImpl_GetItemParamWithEqpId) : nullptr;
    if (!target)
    {
        LogError("ItemParameter: GetItemParamWithEqpId address missing for %s, V_TppEquip.SetItemParameter is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("ItemParameter: SkillAndItemParameterSystemImpl::GetItemParamWithEqpId does not match this build, V_TppEquip.SetItemParameter is off");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ItemParameter: SkillAndItemParameterSystemImpl::GetItemParamWithEqpId", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkGetItemParamWithEqpId), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemParameter: GetItemParamWithEqpId hook failed, V_TppEquip.SetItemParameter is off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_SkillAndItemParameterSystemImpl_GetItemParamWithEqpId_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
