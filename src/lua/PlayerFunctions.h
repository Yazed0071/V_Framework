#pragma once

struct lua_State;

int __cdecl l_SetPlayerVoiceFpkPathForType(lua_State* L);
int __cdecl l_ClearPlayerVoiceFpkPathForType(lua_State* L);
int __cdecl l_ClearAllPlayerVoiceFpkOverrides(lua_State* L);

int __cdecl l_SetPlayerVoiceTypeForType(lua_State* L);
int __cdecl l_ClearPlayerVoiceTypeForType(lua_State* L);
int __cdecl l_ClearAllPlayerVoiceTypeOverrides(lua_State* L);

int __cdecl l_IsBarrierActive(lua_State* L);
int __cdecl l_GetPlayerLife(lua_State* L);
int __cdecl l_SetPlayerLife(lua_State* L);
int __cdecl l_GetLifeRecoveryPerSecond(lua_State* L);
int __cdecl l_SetLifeRecoveryPerSecond(lua_State* L);
int __cdecl l_GetLifeRecoveryCooldownTimer(lua_State* L);
int __cdecl l_SetLifeRecoveryCooldownTimer(lua_State* L);
int __cdecl l_GetFultonCount(lua_State* L);
int __cdecl l_SetFultonCount(lua_State* L);
int __cdecl l_ResupplyFulton(lua_State* L);
int __cdecl l_GetItemBattery(lua_State* L);
int __cdecl l_SetItemBattery(lua_State* L);
int __cdecl l_SetItemOn(lua_State* L);
int __cdecl l_IsItemOn(lua_State* L);
int __cdecl l_GetItemCount(lua_State* L);
int __cdecl l_SetItemCount(lua_State* L);
int __cdecl l_StartSkill(lua_State* L);
int __cdecl l_StopSkill(lua_State* L);
int __cdecl l_IsSkillActive(lua_State* L);
int __cdecl l_GetSkillTime(lua_State* L);
int __cdecl l_RequestToSetTargetCqcStance(lua_State* L);
int __cdecl l_IsThereEnoughSpaceAroundPlayer(lua_State* L);
int __cdecl l_RequestToAttachInDemo(lua_State* L);
int __cdecl l_ClearAttachInDemo(lua_State* L);

int __cdecl l_SetQuietHoldCqc(lua_State* L);
int __cdecl l_SetQuietInterrogate(lua_State* L);
