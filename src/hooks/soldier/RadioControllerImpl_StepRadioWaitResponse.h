#pragma once
#include <cstdint>

bool Install_RadioControllerImpl_StepRadioWaitResponse_Hooks();
void Uninstall_RadioControllerImpl_StepRadioWaitResponse_Hooks();

namespace CpRadioResponse
{
    struct MemberRadio
    {
        std::uint32_t label = 0;   // what the soldier says
        std::uint32_t reply = 0;   // what his CP says back once he's been heard, 0 for nothing
    };

    bool          IsAvailable();
    bool          IsWaitingOn(std::uint16_t soldierId, bool& sitrep);   // his CP is calling him back, or asks for a SITREP
    std::uint32_t OrderFalseResponse(std::uint16_t soldierId);    // 0 when the CP isn't waiting on him
    void          LetSoldierAnswer(std::uint32_t order);          // Snake has finished giving the order
    bool          RequestMemberRadio(std::uint16_t soldierId, const MemberRadio& radio);
    void          OnSoldierDown(std::uint16_t soldierIndex);   // knocked out, asleep or dead: a line he's saying gets cut
    void          Clear();
}
