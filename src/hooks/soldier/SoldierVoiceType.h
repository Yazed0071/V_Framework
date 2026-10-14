#pragma once

#include <Windows.h>
#include <cstdint>

namespace SoldierVoiceType
{
    using GetVoiceType_t = std::uint32_t(__fastcall*)(void* soundController, std::uint32_t soldierIndex, bool);

    constexpr std::uint32_t kUnknown           = 0;
    constexpr std::size_t   kSoundController   = 0xA8;
    constexpr std::size_t   kGetVoiceTypeSlot  = 0x10 / sizeof(void*);  // GetVoiceTypeFromSoldierType, takes the soldier index

    constexpr std::uint32_t kEneA              = 0x570C8E21u;  // ene_a, Skull and DD soldiers
    constexpr std::uint32_t kEneB              = 0x570C8E22u;
    constexpr std::uint32_t kEneC              = 0x570C8E23u;
    constexpr std::uint32_t kEneD              = 0x570C8E24u;
    constexpr std::uint32_t kEneARu            = 0x5F04ADF7u;  // ene_a_ru, Soviet soldiers
    constexpr std::uint32_t kEneBRu            = 0xD76994BEu;
    constexpr std::uint32_t kEneCRu            = 0x31F1B7B5u;
    constexpr std::uint32_t kEneDRu            = 0xB1197754u;
    constexpr std::uint32_t kEneAAf            = 0x4E0492D9u;  // ene_a_af, PF soldiers
    constexpr std::uint32_t kEneBAf            = 0xE869AFE8u;
    constexpr std::uint32_t kEneCAf            = 0x44F1D5CFu;
    constexpr std::uint32_t kEneDAf            = 0x9E19595Eu;
    constexpr std::uint32_t kEneAFml           = 0x832C321Du;  // ene_a_fml, female soldiers
    constexpr std::uint32_t kEneBFml           = 0xC1669E72u;
    constexpr std::uint32_t kEneCFml           = 0x6CB5F4EFu;
    constexpr std::uint32_t kEneDFml           = 0x36F8E99Cu;
    constexpr std::uint32_t kChildA            = 0xB2F1D68Au;  // child soldiers, vox_child
    constexpr std::uint32_t kChildB            = 0xB2F1D689u;
    constexpr std::uint32_t kChildC            = 0xB2F1D688u;
    constexpr std::uint32_t kChildD            = 0xB2F1D68Fu;

    inline bool IsAdult(std::uint32_t voiceType)
    {
        switch (voiceType)
        {
        case kEneA:    case kEneB:    case kEneC:    case kEneD:
        case kEneARu:  case kEneBRu:  case kEneCRu:  case kEneDRu:
        case kEneAAf:  case kEneBAf:  case kEneCAf:  case kEneDAf:
        case kEneAFml: case kEneBFml: case kEneCFml: case kEneDFml:
            return true;
        default:
            return false;
        }
    }

    inline std::uint32_t Of(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        if (!soldiers)
            return kUnknown;
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoundController);
            const auto getVoiceType = (*reinterpret_cast<GetVoiceType_t* const*>(soundController))[kGetVoiceTypeSlot];
            return getVoiceType(soundController, soldierIndex, false);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kUnknown;
        }
    }
}
