#pragma once

struct lua_State;

namespace EquipMotionData
{
    // Lua: V_FrameWork.AddEquipMotionDataTable({
    //     { equipId, "/Assets/tpp/motion/mtar/.../file.mtar" },
    //     ...
    // })
    //
    // Rows are queued in the DLL and appended to the vanilla MotionDataTable
    // inside the next EquipMotionDataTableImpl::ReloadEquipMotionData call.
    // Rows whose equipId is already present in the live Lua table are
    // skipped so repeat reloads stay idempotent.
    // Params: L
    int __cdecl Lua_AddEquipMotionDataTable(lua_State* L);

    // Installs the ReloadEquipMotionData hook.
    // Params: none
    bool Install_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook();

    // Removes the ReloadEquipMotionData hook.
    // Params: none
    bool Uninstall_EquipMotionDataTableImpl_ReloadEquipMotionData_Hook();
}
