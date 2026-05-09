-- V_TppPickable — pickable item count overrides at locator indices.
-- See guide/V_FrameWork_API_Reference.txt for parameter specs and examples.

local this = {}


function this.SetCountRaw(locatorName, countRaw)
    if locatorName == nil then
        V_FrameWork.Log("V_TppPickable.SetCountRaw: locatorName is nil.")
        return
    end
    if type(countRaw) ~= "number" then
        V_FrameWork.Log("V_TppPickable.SetCountRaw: countRaw is not a number.")
        return
    end
    return V_FrameWork.SetPickableCountRawByIndex(locatorName, countRaw)
end

function this.GetCountRaw(locatorName)
    if locatorName == nil then
        V_FrameWork.Log("V_TppPickable.GetCountRaw: locatorName is nil.")
        return
    end
    return V_FrameWork.GetPickableCountRawByIndex(locatorName)
end

return this
