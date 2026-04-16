#include "pch.h"
#include "CustomEquipIdState.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace CustomEquipIdState
{
    bool ResolveOrCreatePersistentEquipId(
        const char* eqpName,
        const std::int32_t minimumEquipId,
        std::int32_t& outEquipId)
    {
        return V_FrameWorkState::ResolveOrCreateEquipId(eqpName, minimumEquipId, outEquipId);
    }

    void ResetForNewSession()
    {
        // The unified state handles its own lifecycle.
    }
}
