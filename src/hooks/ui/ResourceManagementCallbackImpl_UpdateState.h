#pragma once

// One hook on the iDroid Resources menu step, shared by the iDroid voice lines and the mod vehicle rows.
// Each user acquires it on install and releases it on uninstall; the hook goes in with the first and out with the last.
bool Acquire_ResourceManagementCallbackImpl_UpdateState_Hook();
void Release_ResourceManagementCallbackImpl_UpdateState_Hook();
