local this = {}

function this.Messages()
  return Tpp.StrCode32Table{
    UI = {
      {
        msg = "Customize_Start",
        func = function()
          V_Helicopter.KeepSupportHeliPicks()
        end,
        option = { isExecMissionClear = true },
      },
      {
        msg = "Customize_Abort",
        func = function()
          V_Helicopter.RestoreSupportHeliPicks()
        end,
        option = { isExecMissionClear = true },
      },
      {
        msg = "EndFadeIn",
        sender = "OnWeaponCustomizeEnd",
        func = function()
          if not V_Helicopter.IsSupportHeliRebuildNeeded() then return end
          TppSave.ReserveVarRestoreForContinue()
          TppPlayer.ResetInitialPosition()
          TppSequence.ReserveNextSequence("Seq_Game_MainGame")
          TppMission.Reload{
            missionPackLabelName = "default",
            showLoadingTips      = false,
          }
        end,
        option = { isExecMissionClear = true },
      },
    },
  }
end

function this.Init(missionTable)
  this.messageExecTable = nil
  if not TppMission.IsHelicopterSpace(vars.missionCode) then return end
  this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnReload(missionTable)
  this.Init(missionTable)
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
  if this.messageExecTable == nil then return end
  Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

return this
