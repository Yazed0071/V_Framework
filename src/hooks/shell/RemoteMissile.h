#pragma once

struct lua_State;

int __cdecl l_SetRemoteMissile(lua_State* L);
int __cdecl l_AbortRemoteMissile(lua_State* L);
int __cdecl l_IsRemoteMissileFlying(lua_State* L);

namespace shell
{
    void RemoteMissile_CaptureContext(void* playerContext);
    void RemoteMissile_Tick(float deltaSeconds);

    void RemoteMissile_Register(int equipId, int receiverId, int attackId,
                               bool hasAttackId,
                               double maxFlightSeconds, double maxRange,
                               double cameraDistance, double cameraHeight,
                               double minSpeed, double maxSpeed,
                               unsigned int fireVoiceId);
    void RemoteMissile_ArmShot(int playerIndex, int ownerEquipId);
    void RemoteMissile_DisarmShot();
    void RemoteMissile_Abort();
    bool RemoteMissile_IsFlying();

    bool Install_RemoteMissile_Hooks();
    void Uninstall_RemoteMissile_Hooks();
}
