#pragma once

#include <cstdint>


enum class CpPhaseTimer : std::uint8_t
{
    Caution,
    Evasion,
    Alert,
};


bool Install_CautionStepNormalTimerHook();


bool Uninstall_CautionStepNormalTimerHook();


void Set_CpPhaseDurationSeconds(CpPhaseTimer phase, float seconds);


void Unset_CpPhaseDurationSeconds(CpPhaseTimer phase);


float Get_CpPhaseDurationSeconds(CpPhaseTimer phase);


void Set_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex, float seconds);


void Unset_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex);


float Get_CpPhaseDurationSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex);


float Get_CpPhaseRemainingSecondsForCp(CpPhaseTimer phase, std::uint32_t cpIndex);


void Clear_AllCpPhaseDurationOverrides();


void Arm_CpIndexCapture();


std::uint32_t Take_CapturedCpIndex();
