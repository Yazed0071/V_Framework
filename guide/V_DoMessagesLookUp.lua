-- V_DoMessagesLookUp.lua
-- tex V_FrameWork custom message lookup / printing functions
-- structured like InfLookup.lua, but standalone.
-- uses InfLookup as the parent lookup/signature system.
-- does not patch InfLookup.

local this={}
local StrCode32=Fox.StrCode32

this.debugModule=false

this.DEBUG_strCode32List={
  "GameObject",
  "Terminal",
  "ShalenSearchModeChange",
  "HoldupCancelLookToPlayer",
  "CassettePlay",
}

this.str32ToString={}
this.unknownMessages={}
this.messageExecTable=nil
this.registered=false

this.shalenSearchMode={
  [-1]="NO_MODE",
  [0]="Init",
  [1]="Scream",
  [2]="SearchNormal",
  [3]="SearchEvasion",
  [4]="MODE_4",
  [5]="Alert",
}

this.CassettePlay={
  [0]="Speaker_off",
  [1]="Speaker_on",
}

function this.PostModuleReload(prevModule)
  this.str32ToString=prevModule.str32ToString or this.str32ToString or {}
  this.unknownMessages=prevModule.unknownMessages or this.unknownMessages or {}
  this.shalenSearchMode=prevModule.shalenSearchMode or this.shalenSearchMode
  this.CassettePlay=prevModule.CassettePlay or this.CassettePlay
  this.registered=prevModule.registered or false

  this.RefreshInfLookupLinks()
end

function this.PostAllModulesLoad()
  this.RefreshInfLookupLinks()
  this.AddToStr32StringLookup(this.DEBUG_strCode32List)

  if this.debugModule then
    InfCore.PrintInspect(this.lookups,"V_DoMessagesLookUp.lookups")
    InfCore.PrintInspect(this.messageSignatures,"V_DoMessagesLookUp.messageSignatures")
  end
end

function this.OnInitializeTop(missionTable)
  this.RefreshInfLookupLinks()
  this.AddToStr32StringLookup(this.DEBUG_strCode32List)

  if InfLookup and InfLookup.AddToStr32StringLookup then
    InfLookup.AddToStr32StringLookup(this.DEBUG_strCode32List)
  end

  if this.debugModule then
    InfCore.Log("V_DoMessagesLookUp.OnInitializeTop")
  end
end

function this.Init(missionTable)
  this.RefreshInfLookupLinks()
  this.RegisterMessages()

  if this.debugModule then
    InfCore.Log("V_DoMessagesLookUp.Init")
  end
end

function this.OnReload(missionTable)
  this.RefreshInfLookupLinks()
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

--TABLESETUP
function this.AddToStr32StringLookup(strCode32List)
  if strCode32List==nil then
    return
  end

  this.str32ToString=this.str32ToString or {}

  if InfCore then
    InfCore.str32ToString=InfCore.str32ToString or {}
  end

  for i,someString in ipairs(strCode32List)do
    if type(someString)=="string" then
      this.str32ToString[StrCode32(someString)]=someString
    end
  end
end

function this.StrCode32ToString(strCode,isStrCode)
  if strCode==nil then
    return "nil"
  end

  if type(strCode)=="number" then
    this.str32ToString=this.str32ToString or {}

    if InfCore then
      InfCore.str32ToString=InfCore.str32ToString or {}
      InfCore.unknownStr32=InfCore.unknownStr32 or {}
    end

    local returnString=nil

    if InfCore and InfCore.str32ToString then
      returnString=InfCore.str32ToString[strCode]
    end

    if returnString==nil then
      returnString=this.str32ToString[strCode]

      if returnString and InfCore and InfCore.str32ToString then
        InfCore.str32ToString[strCode]=returnString
      end
    end

    if returnString==nil and InfLookup and InfLookup.StrCode32ToString then
      returnString=InfLookup.StrCode32ToString(strCode,isStrCode)
    end

    if isStrCode and returnString==nil and InfCore and InfCore.unknownStr32 then
      InfCore.unknownStr32[strCode]=true
    end

    if returnString==nil then
      return strCode
    end

    if type(returnString)=="number" then
      InfCore.Log("WARNING: V_DoMessagesLookUp.StrCode32ToString: returnString for strCode:"..strCode.." is a number: "..returnString)
    end

    return returnString
  else
    InfCore.Log("WARNING: V_DoMessagesLookUp.StrCode32ToString: strCode:"..tostring(strCode).." is not a number.")
  end

  return tostring(strCode)
end--StrCode32ToString

function this.ObjectNameForGameId(gameId)
  if gameId==nil then
    return nil
  end

  if InfLookup and InfLookup.ObjectNameForGameId then
    return InfLookup.ObjectNameForGameId(gameId)
  end

  if InfLookup and InfLookup.Lookup then
    return InfLookup.Lookup("gameId",gameId)
  end

  return nil
end

function this.RefreshInfLookupLinks()
  this.str32ToString=this.str32ToString or {}

  if InfCore then
    InfCore.str32ToString=InfCore.str32ToString or {}
    InfCore.unknownStr32=InfCore.unknownStr32 or {}
  end

  --tex generated lookup refs need to be restored too else they'll point to old tables
  this.lookups.str32=this.StrCode32ToString
  this.lookups.gameId=this.ObjectNameForGameId
  this.lookups.shalenSearchMode=this.shalenSearchMode
  this.lookups.CassettePlay=this.CassettePlay

  --tex "add everything from InfLookup" without copying its giant table.
  --If this.lookups does not have a lookup, Lua falls through to InfLookup.lookups.
  if InfLookup and InfLookup.lookups then
    setmetatable(this.lookups,{__index=InfLookup.lookups})
  end

  if InfLookup and InfLookup.alertFuncs then
    setmetatable(this.alertFuncs,{__index=InfLookup.alertFuncs})
  end

  if InfLookup and InfLookup.signatureTypes then
    setmetatable(this.signatureTypes,{__index=InfLookup.signatureTypes})
  end
end

--tex local lookups first, then InfLookup.
this.lookups={
  str32=this.StrCode32ToString,

  gameId=this.ObjectNameForGameId,

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
  CassettePlay=this.CassettePlay,
}

function this.Lookup(lookupType,value)
  --tex Handled elsewhere
  if lookupType=="guess"then
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

  --tex First try our local lookup table.
  local lookup=this.lookups[lookupType]

  if type(lookup)=="function" then
    lookedupValue=lookup(value)
  elseif type(lookup)=="table" then
    lookedupValue=lookup[value]
  end

  if lookedupValue~=nil then
    return lookedupValue
  end

  --tex Then try InfLookup's full Lookup system.
  --This gives us dictionaries, gameId, str32, attackId, equipId, phase, etc.
  if InfLookup and InfLookup.Lookup then
    lookedupValue=InfLookup.Lookup(lookupType,value)
    if lookedupValue~=nil then
      return lookedupValue
    end
  end

  if lookup==nil and lookupType~="number" then
    InfCore.Log("WARNING: V_DoMessagesLookUp.Lookup: no lookup of type "..tostring(lookupType))
  end

  return nil
end--Lookup

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

  holdupCancelLookToPlayer={
    {argName="gameId",argType="gameId"},
  },

  cassettePlay={
    {argName="IsSpeaker",argType="CassettePlay"},
    {argName="TrackCountInAlbum",argType="number"},
    {argName="selectedTrackIndex",argType="number"},
  },
}

--tex local custom signatures.
--tex Any missing sender/message can fall through to InfLookup.messageSignatures.
this.messageSignatures={
  GameObject={
    ShalenSearchModeChange=this.signatureTypes.shalenMode,
    HoldupCancelLookToPlayer=this.signatureTypes.holdupCancelLookToPlayer,
  },

  Terminal={
    CassettePlay=this.signatureTypes.cassettePlay,
  },
}

function this.GetMessageSignature(sender,messageId)
  if sender==nil or messageId==nil then
    return nil
  end

  --tex First custom signatures.
  local senderSignatures=this.messageSignatures[sender]
  if senderSignatures then
    local signature=senderSignatures[messageId]
    if signature then
      return signature
    end
  end

  --tex Then InfLookup's complete signature database.
  if InfLookup and InfLookup.messageSignatures then
    senderSignatures=InfLookup.messageSignatures[sender]
    if senderSignatures then
      return senderSignatures[messageId]
    end
  end

  return nil
end

function this.GetArgLookupString(argType,argValue)
  local lookedupValue=this.Lookup(argType,argValue)

  if lookedupValue==nil then
    return "~"
  end

  return this.SafeToString(lookedupValue)
end

function this.StringifyArg(arg,argDef)
  --tex Prefer InfLookup's exact arg stringifier if available.
  --tex This gives us the real InfLookup style:
  --tex --[[argName:argType:lookup]] value
  if InfLookup and InfLookup.StringifyArg then
    return InfLookup.StringifyArg(arg,argDef)
  end

  local argDat=tostring(arg)
  local argTypeDat=""
  local postComments={}

  if argDef then
    local lookupValue=this.Lookup(argDef.argType,arg)
    argTypeDat=argTypeDat.."--[["

    if argDef.argAlert and argDef.argAlert(arg) then
      argTypeDat=argTypeDat.." /!\\ "
    end

    if argDef.argName then
      argTypeDat=argTypeDat..tostring(argDef.argName)
    end

    argTypeDat=argTypeDat..":"

    if argDef.argType then
      argTypeDat=argTypeDat..((argDef.argName~=argDef.argType) and tostring(argDef.argType) or "~")
      if argDef.argCanBeNil then
        argTypeDat=argTypeDat.."?"
      end
    end

    argTypeDat=argTypeDat..":"

    if lookupValue then
      argTypeDat=argTypeDat..((tostring(lookupValue)~=tostring(arg)) and tostring(lookupValue) or "~")
    end

    argTypeDat=argTypeDat.."]] "
  end

  if (argDef and argDef.argType=="guess") or (argDef==nil) then
    local guess=this.GuessArgType(arg)
    if guess~="nil" then
      table.insert(postComments,"typeguess: "..guess)
    end
  end

  return argTypeDat..argDat,postComments
end

function this.GuessArgType(arg)
  --tex Prefer InfLookup's full guesser.
  if InfLookup and InfLookup.GuessArgType then
    return InfLookup.GuessArgType(arg)
  end

  if arg==nil then
    return "nil"
  end

  if type(arg)=="number" then
    local lookupReturns={}
    lookupReturns[#lookupReturns+1]=arg

    for lookupType,_ in pairs(this.lookups)do
      local lookupReturn=this.Lookup(lookupType,arg)
      if lookupReturn then
        lookupReturns[#lookupReturns+1]=lookupType..":"..lookupReturn
      end
    end

    local argValue=""
    local addSeparator=#lookupReturns>1

    for lookupReturnIndex,lookupReturn in ipairs(lookupReturns)do
      if addSeparator and lookupReturnIndex>1 then
        argValue=argValue.."||"
      end
      argValue=argValue..lookupReturn
    end

    return argValue
  end

  return tostring(arg)
end

--tex matches InfLookup-style output:
-- OnMessage[]: Sender.Message(--[[argName:argType:lookup]] value, nil, nil, nil)
function this.PrintMessageSignature(sender,messageId,signature,...)
  local args={...}
  local messageInfoString=tostring(sender).."."..tostring(messageId).."("
  local postComments={}

  for i=1,4 do
    local arg=args[i]
    local argDef=nil

    if signature then
      argDef=signature[i]
    end

    local argString,argPostComments=this.StringifyArg(arg,argDef)

    messageInfoString=messageInfoString..argString

    if i<4 then
      messageInfoString=messageInfoString..", "
    end

    if argPostComments then
      for n,postComment in ipairs(argPostComments)do
        table.insert(postComments,postComment)
      end
    end
  end

  messageInfoString=messageInfoString..")"

  if #postComments>0 then
    messageInfoString=messageInfoString.." --[[ "..table.concat(postComments,", ").." ]]"
  end

  local badge=""

  if string.find(messageInfoString,"/!\\") then
    badge="/!\\"
  elseif string.find(messageInfoString,"/?\\") then
    badge="/?\\"
  end

  InfCore.Log("OnMessage["..badge.."]: "..messageInfoString)

  return sender.."."..messageId,messageInfoString
end--PrintMessageSignature

function this.PrintOnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  this.RefreshInfLookupLinks()

  local senderStr=sender
  local messageIdStr=messageId

  --tex Our V_FrameWork broadcaster already sends strings.
  --tex But if someone passes str32 numbers, decode them like InfLookup.
  if type(senderStr)=="number" then
    senderStr=this.StrCode32ToString(senderStr,true)
  end

  if type(messageIdStr)=="number" then
    messageIdStr=this.StrCode32ToString(messageIdStr,true)
  end

  local signature=this.GetMessageSignature(senderStr,messageIdStr)

  if signature==nil then
    this.unknownMessages[senderStr]=this.unknownMessages[senderStr] or {}
    this.unknownMessages[senderStr][messageIdStr]=true
  end

  return this.PrintMessageSignature(senderStr,messageIdStr,signature,arg0,arg1,arg2,arg3)
end--PrintOnMessage

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

    Terminal={
      {
        msg="CassettePlay",
        func=function(Speaker,albumIndex,selectedTrackIndex)
          this.PrintOnMessage("Terminal","CassettePlay",Speaker,albumIndex,selectedTrackIndex,nil)
        end,
      },
    },
  }
end

function this.RegisterMessages()
  if this.registered then
    return
  end

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
  this.registered=true

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