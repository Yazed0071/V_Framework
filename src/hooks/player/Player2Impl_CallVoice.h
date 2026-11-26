#pragma once

#include <cstdint>

bool Install_Player2Impl_CallVoice_Hook();
void Uninstall_Player2Impl_CallVoice_Hook();
std::uint16_t SwapDamageAttack(std::uint16_t attackId);   // the attack being applied right now, for the death line; returns the previous one
bool SayPlayerLine(std::uint32_t line);
bool SayEitherPlayerLine(std::uint32_t first, std::uint32_t second);
bool SayMinorPlayerLine(std::uint32_t line);                                   // false while he talks; any game line cuts it off
bool SayEitherMinorPlayerLine(std::uint32_t first, std::uint32_t second);
void HoldGamePlayerLines(std::uint32_t ms);                                    // the game's own player lines stay quiet for a while, ours still play
void SayHorseGaitLine(std::uint8_t state);                 // D-Horse BasicAction2Impl state just entered
void SayHorsePatLine();
void SayHorseDungLine();                                   // D-Horse just finished dropping its dung
void SayCatchBreathAfterSprint();                          // once a frame: Liquid's long sprint that just ended
void NotePlayerCqcCommand();                               // a CQC command just went out: the effort lines right after it are punches and kicks
void Player2Impl_CallVoice_ClearForReload();
