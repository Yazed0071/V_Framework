-- V_TppPickable — pickable item count overrides at locator indices.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}

function this.SetCountRaw(locatorName, countRaw)
    return V_FrameWork.SetPickableCountRawByIndex(locatorName, countRaw)
end

function this.GetCountRaw(locatorName)
    return V_FrameWork.GetPickableCountRawByIndex(locatorName)
end

return this
