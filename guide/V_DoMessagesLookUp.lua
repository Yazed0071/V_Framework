-- V_DoMessagesLookUp.lua
-- tex V_FrameWork custom message lookup / printing functions
-- structured like InfLookup.lua, but standalone.
-- does not patch InfLookup.

local this={}
this.debugModule=false

this.DEBUG_strCode32List={
  "GameObject",
  "ShalenSearchModeChange",
  "HoldupCancelLookToPlayer",
}

this.unknownMessages={}
this.messageExecTable=nil

this.shalenSearchMode={
  [-1]="NO_MODE",
  [0]="Init",
  [1]="Scream",
  [2]="SearchNormal",
  [3]="SearchEvasion",
  [4]="MODE_4",
  [5]="Alert",
}

function this.PostModuleReload(prevModule)
  this.unknownMessages=prevModule.unknownMessages
  this.shalenSearchMode=prevModule.shalenSearchMode

  --tex generated lookups need to be restored too else they'll point to the empty ones from the newly loaded module
  this.lookups.shalenSearchMode=this.shalenSearchMode
end

function this.PostAllModulesLoad()
  if this.debugModule then
    InfCore.PrintInspect(this.lookups,"V_DoMessagesLookUp.lookups")
    InfCore.PrintInspect(this.messageSignatures,"V_DoMessagesLookUp.messageSignatures")
  end
end

function this.OnInitializeTop(missionTable)
  if InfLookup and InfLookup.AddToStr32StringLookup then
    InfLookup.AddToStr32StringLookup(this.DEBUG_strCode32List)
  end

  if this.debugModule then
    InfCore.Log("V_DoMessagesLookUp.OnInitializeTop")
  end
end

function this.Init(missionTable)
  this.RegisterMessages()

  if this.debugModule then
    InfCore.Log("V_DoMessagesLookUp.Init")
  end
end

function this.OnReload(missionTable)
  this.RegisterMessages()
end

function this.Save()
end

function this.SafeToString(value)
  if value==nil then
    return "nil"
  end

  return tostring(value)
end

--tex simplified lookup name to lookup table or function
this.lookups={
  number=function(value)
    return value
  end,

  string=function(value)
    return value
  end,

  boolAsNumber={
    [0]="false",
    [1]="true",
  },

  shalenSearchMode=this.shalenSearchMode,
}

function this.Lookup(lookupType,value)
  --tex Handled elsewhere
  if lookupType=="guess" then
    return nil
  end

  if lookupType==nil then
    InfCore.Log("WARNING: V_DoMessagesLookUp.Lookup: lookupType was nil for "..tostring(value))
    return nil
  end

  if value==nil then
    return nil
  end

  local lookedupValue=nil
  local lookup=this.lookups[lookupType]

  if type(lookup)=="function" then
    lookedupValue=lookup(value)
  elseif type(lookup)=="table" then
    lookedupValue=lookup[value]
  elseif lookupType~="number" then
    InfCore.Log("WARNING: V_DoMessagesLookUp.Lookup: no lookup of type "..tostring(lookupType))
  end

  return lookedupValue
end

this.alertFuncs={
  any=function()
    return true
  end,

  notNil=function(x)
    return x~=nil
  end,

  notZero=function(x)
    return x~=0
  end,

  truthy=function(x)
    if x==0 then
      return false
    else
      return x
    end
  end,
}

this.signatureTypes={
  none={},

  shalenMode={
    {argName="mode",argType="shalenSearchMode"},
  },

  gameId={
    {argName="gameId",argType="number"},
  },
}

--tex message signatures for PrintOnMessage style output
-- {
--  MESSAGE CLASS={
--    MSG={
--      [arg(n+1)]={argName=<arg name>,argType=<lookupType>},
-- lookupType for this.lookups table.
this.messageSignatures={
  GameObject={
    ShalenSearchModeChange=this.signatureTypes.shalenMode,
    HoldupCancelLookToPlayer=this.signatureTypes.gameId,
  },
}

function this.GetMessageSignature(sender,messageId)
  if sender==nil or messageId==nil then
    return nil
  end

  local senderSignatures=this.messageSignatures[sender]
  if senderSignatures==nil then
    return nil
  end

  return senderSignatures[messageId]
end

function this.GetArgLookupString(argType,argValue)
  local lookedupValue=this.Lookup(argType,argValue)

  if lookedupValue==nil then
    return "~"
  end

  return this.SafeToString(lookedupValue)
end

function this.BuildArgString(argInfo,argValue)
  if argInfo==nil then
    return this.SafeToString(argValue)
  end

  if argValue==nil then
    return "nil"
  end

  local argName=argInfo.argName or "unk"
  local argType=argInfo.argType or "~"
  local lookupString=this.GetArgLookupString(argType,argValue)

  return "--[["..argName..":"..argType..":"..lookupString.."]] "..this.SafeToString(argValue)
end

--tex matches InfLookup-style output:
-- OnMessage[]: Sender.Message(--[[argName:argType:lookup]] value, nil, nil, nil)
function this.PrintMessageSignature(sender,messageId,arg0,arg1,arg2,arg3)
  local identity=this.SafeToString(sender).."."..this.SafeToString(messageId)
  local signature=this.GetMessageSignature(sender,messageId)
  local args={arg0,arg1,arg2,arg3}
  local argStrings={}
  local postComments={}
  local noSignature=false

  if signature==nil then
    noSignature=true
    this.unknownMessages[sender]=this.unknownMessages[sender] or {}
    this.unknownMessages[sender][messageId]=true
  end

  for i=1,4 do
    local argValue=args[i]
    local argInfo=nil

    if signature then
      argInfo=signature[i]
    end

    if argInfo then
      table.insert(argStrings,this.BuildArgString(argInfo,argValue))
    else
      table.insert(argStrings,this.SafeToString(argValue))
    end
  end

  if noSignature then
    table.insert(postComments,"/!\\ new identity")
  end

  local postComment=""
  if #postComments>0 then
    postComment=" --[[ "..table.concat(postComments,"; ").." ]]"
  end

  local messageInfoString=identity.."("..table.concat(argStrings,", ")..")"..postComment
  local badge=""

  if string.find(messageInfoString,"/!\\") then
    badge="/!\\"
  elseif string.find(messageInfoString,"/?\\") then
    badge="/?\\"
  end

  InfCore.Log("OnMessage["..badge.."]: "..messageInfoString)

  return identity,messageInfoString
end--PrintMessageSignature

function this.PrintOnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  return this.PrintMessageSignature(sender,messageId,arg0,arg1,arg2,arg3)
end

function this.Messages()
  return Tpp.StrCode32Table{
    GameObject={
      {
        msg="ShalenSearchModeChange",
        func=function(mode)
          this.PrintOnMessage("GameObject","ShalenSearchModeChange",mode,nil,nil,nil)
        end,
      },

      {
        msg="HoldupCancelLookToPlayer",
        func=function(gameId)
          this.PrintOnMessage("GameObject","HoldupCancelLookToPlayer",gameId,nil,nil,nil)
        end,
      },
    },
  }
end

function this.RegisterMessages()
  if not V_FrameWork then
    InfCore.Log("V_DoMessagesLookUp.RegisterMessages: V_FrameWork is nil")
    return
  end

  if not V_FrameWork.RegisterListener then
    InfCore.Log("V_DoMessagesLookUp.RegisterMessages: V_FrameWork.RegisterListener is nil")
    return
  end

  this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
  V_FrameWork.RegisterListener(this)

  if this.debugModule then
    InfCore.Log("V_DoMessagesLookUp.RegisterMessages: registered")
  end
end

function this.OnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  if not this.messageExecTable then
    this.messageExecTable=Tpp.MakeMessageExecTable(this.Messages())
  end

  Tpp.DoMessage(
    this.messageExecTable,
    TppMission.CheckMessageOption,
    sender,
    messageId,
    arg0,
    arg1,
    arg2,
    arg3,
    strLogText
  )
end

return this