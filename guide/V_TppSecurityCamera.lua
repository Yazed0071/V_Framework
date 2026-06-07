local this = {}
local IsTypeString = Tpp.IsTypeString

-- variant: 0 = normal camera, 1 = gun camera. fv2Path: path to a .fv2 file.
function this.SetSecurityCameraFova(variant, fv2Path)
    if not IsTypeString(fv2Path) then
        V_FrameWork.Log("V_TppSecurityCamera.SetSecurityCameraFova: fv2Path must be a string.")
        return
    end
    if not fv2Path:match("%.fv2$") then
        fv2Path = fv2Path .. ".fv2"
    end
    return GameObject.SendCommand({ type = "TppSecurityCamera2" }, { id = "SetSecurityCameraFova", variant = variant, fova = fv2Path })
end

function this.ClearSecurityCameraFova(variant)
    return V_SecurityCamera.ClearSecurityCameraFova(variant)
end

function this.ClearAllSecurityCameraFovas()
    return V_SecurityCamera.ClearAllSecurityCameraFovas()
end

return this
