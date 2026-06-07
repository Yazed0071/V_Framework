local this = {}


function this.SetCountRaw(locatorIndex, countRaw)
    if type(locatorIndex) ~= "number" then
        V_FrameWork.Log("V_TppPickable.SetCountRaw: locatorIndex is not a number.")
        return
    end
    if type(countRaw) ~= "number" then
        V_FrameWork.Log("V_TppPickable.SetCountRaw: countRaw is not a number.")
        return
    end
    return V_FrameWork.SetPickableCountRawByIndex(locatorIndex, countRaw)
end

function this.GetCountRaw(locatorIndex)
    if type(locatorIndex) ~= "number" then
        V_FrameWork.Log("V_TppPickable.GetCountRaw: locatorIndex is not a number.")
        return
    end
    return V_FrameWork.GetPickableCountRawByIndex(locatorIndex)
end

return this
