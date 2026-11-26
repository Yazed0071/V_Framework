#pragma once

#include <cstdint>

constexpr std::uint32_t kCpInvestigationMost = 64;

struct CpInvestigationRequest   // GameObject.SendCommand(cpId, {id="RequestInvestigation", ...})
{
    float         spot[3]                        = {};
    std::uint32_t memberCount                    = 0;    // 0: the CP's SetInvestigationMemberCount, else 4
    std::uint32_t soldiers[kCpInvestigationMost] = {};   // soldier indices the modder named, instead of whoever is free
    std::uint32_t soldierCount                   = 0;
    std::uint32_t endLabel                       = 0;    // their end report speaks this instead of CPR0070
    bool          silent                         = false;
    bool          takeOver                       = false;
};

std::uint32_t CpInvestigation_Request(std::uint32_t cp, const CpInvestigationRequest& request, std::uint32_t* pickedSoldiers);   // Lua thread; the CP sends them on its next idle tick
void          CpInvestigation_SetMemberCount(std::uint32_t cp, std::uint32_t count);
void          CpInvestigation_SetEveryMemberCount(std::uint32_t count);
void          CpInvestigation_OnCpIdle(std::uint32_t cp, std::uint8_t* service);
bool          CpInvestigation_EndCall(std::uint32_t cp, std::uint8_t radioType, std::uint16_t member, std::uint32_t& label);   // one of ours ends: label to speak, 0 = silent
void          CpInvestigation_ClearForNewMission();
