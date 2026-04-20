#pragma once

// Live diagnostic logger for fox::sd::ConvertParameterID (trampoline at
// 0x14032adf0). Every unique name→hash pair the game resolves at runtime is
// written to the V_FrameWork log exactly once so the log stays readable.
//
// Output format (one line per unique name, after the first call for each):
//   [ParamID] "<name>" -> 0x<hash>
//
// All Wwise RTPCs, Switches, and States the game ever references pass through
// this function, so after a play session the log contains an authoritative
// list of every parameter name the banks expose to game code. Use those names
// verbatim with V_FrameWork.SetSoldierRtpc / SetGlobalRtpc — no example names
// are hard-coded here on purpose (we don't want to imply any specific name
// exists in the banks; the real names come from the log at runtime).

// Installs the fox::sd::ConvertParameterID logging passthrough hook.
// Params: none
bool Install_ConvertParameterIdLogger_Hook();

// Removes the hook.
// Params: none
bool Uninstall_ConvertParameterIdLogger_Hook();
