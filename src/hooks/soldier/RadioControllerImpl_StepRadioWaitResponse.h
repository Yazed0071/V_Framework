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
    bool          HasMemberRadio(std::uint16_t soldierId);   // his call is still queued, or posted and not over
    void          OnSoldierDown(std::uint16_t soldierIndex);   // knocked out, asleep or dead: a line he's saying gets cut
    bool          CpOpensCallTo(std::uint16_t soldierIndex);   // the call he's on was asked by his CP and starts with the CP talking
    bool          CpPhaseOf(std::uint16_t soldierIndex, std::uint32_t& cp, std::int32_t& phase);   // his CP and the phase it is in

    enum class SoldierNode : std::uint8_t
    {
        Check,   // checking the power, a camera, a noise or something half-seen, he reports back once done
        Held,    // CQC hold or hold-up
    };
    void          OnSoldierNode(std::uint16_t soldierIndex, SoldierNode node, bool awake);
    bool          IsHeld(std::uint16_t soldierIndex, bool& known);   // CQC hold or hold-up; known is false without the AI node watch

    void          Clear();
}
