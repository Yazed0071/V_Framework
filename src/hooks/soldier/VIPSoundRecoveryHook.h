#pragma once

// Installs the sound-recovery hook. Intercepts
// NoticeActionImpl::State_StandEnterRecoverySleepFaintHoldupComradeBySound and
// swaps the vanilla voice-notice reaction hash for the custom reaction hash
// shared with the Touch/Kick sleep-faint hook and the Holdup hook, so the Lua
// layer handles every comrade-recovery path uniformly regardless of whether
// the sleeper/held-up soldier was woken by touch, kick, or sound.
// Params: none
bool Install_VIPSoundRecovery_Hook();

// Removes the sound-recovery hook.
// Params: none
bool Uninstall_VIPSoundRecovery_Hook();
