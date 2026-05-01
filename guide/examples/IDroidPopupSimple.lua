function this.ShowDefaultPopup()
    V_FrameWork.ShowIDroidPopup("Hello from V_FrameWork!")
end


function this.ShowReserveId01()
    V_FrameWork.ShowIDroidPopup("Test on reserveId 0x01", 0x01)
end


function this.IterateReserveIds()
    for id = 0x01, 0x20 do
        if id ~= 0x0E then
            V_FrameWork.Log(string.format("[ProbePopup] firing reserveId=0x%02X", id))
            V_FrameWork.ShowIDroidPopup(string.format("Probe reserveId=0x%02X", id), id)
        end
    end
end


function this.ShowFromMissionStart()
    V_FrameWork.ShowIDroidPopup(
        string.format("Mod loaded - %d items registered", 42),
        0x01)
end
