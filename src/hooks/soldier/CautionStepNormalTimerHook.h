#pragma once

#include <cstdint>


bool Install_CautionStepNormalTimerHook();


bool Uninstall_CautionStepNormalTimerHook();


void Set_CautionStepNormalDurationSeconds(float seconds);


float Get_CautionStepNormalDurationSeconds();


void Unset_CautionStepNormalDurationSeconds();


void Set_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex, float seconds);


void Unset_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex);


void Clear_AllCautionStepNormalDurationOverrides();


void Set_PendingCautionDurationForCp(float seconds);


float Get_CautionStepNormalRemainingSeconds();


void Arm_CautionCpCapture();


std::uint32_t Take_CautionCpIndex();


float Get_CautionStepNormalDurationSecondsForCp(std::uint32_t cpIndex);


float Get_CautionStepNormalRemainingSecondsForCp(std::uint32_t cpIndex);