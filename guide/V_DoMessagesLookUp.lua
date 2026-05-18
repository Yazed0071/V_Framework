local this={}
local StrCode32=Fox.StrCode32

this.debugModule=false
this.str32ToString={}
this.unknownMessages={}

this.DEBUG_strCode32List={
  "GameObject",
  "Terminal",
  "Player",
  "HoldupCancelLookToPlayer",
  "CassettePlay",
  "CrawlSideRoll",
  "Start",
  "Rolling",
  "End",
}

this.CassettePlay={
  [0]="Speaker_off",
  [1]="Speaker_on",
}

this.signatureTypes={
  holdupCancelLookToPlayer={
    {argName="soldierId",argType="gameId"},
  },

  cassettePlay={
    {argName="IsSpeaker",argType="number"},
    {argName="TrackCountInAlbum",argType="number"},
    {argName="selectedTrackIndex",argType="number"},
  },

  CrawlSideRoll={
    {argName="playerIndex",argType="number"},
    {argName="state",argType="str32"},
    {argName="count",argType="number"},
    {argName="direction",argType="number"},
  },
}

this.messageSignatures={
  GameObject={
    HoldupCancelLookToPlayer=this.signatureTypes.holdupCancelLookToPlayer,
  },

  Player={
    CrawlSideRoll=this.signatureTypes.CrawlSideRoll,
  },

  Terminal={
    CassettePlay=this.signatureTypes.cassettePlay,
  },
}

this.lookups={
  str32=function(value)
    return this.StrCode32ToString(value,true)
  end,

  number=function(value)
    return value
  end,

  string=function(value)
    return value
  end,

  CassettePlay=this.CassettePlay,
}

function this.PostModuleReload(prevModule)
  this.str32ToString=prevModule.str32ToString or this.str32ToString or {}
  this.unknownMessages=prevModule.unknownMessages or this.unknownMessages or {}
  this.CassettePlay=prevModule.CassettePlay or this.CassettePlay

  this.RefreshLookups()
end

function this.AddToStr32StringLookup(list)
  if not list then
    return
  end

  this.str32ToString=this.str32ToString or {}

  if InfCore then
    InfCore.str32ToString=InfCore.str32ToString or {}
  end

  for i,name in ipairs(list)do
    if type(name)=="string" then
      local code=StrCode32(name)

      this.str32ToString[code]=name

      if InfCore and InfCore.str32ToString then
        InfCore.str32ToString[code]=name
      end
    end
  end
end

function this.StrCode32ToString(value,isStrCode)
  if value==nil then
    return "nil"
  end

  if type(value)~="number" then
    return tostring(value)
  end

  this.str32ToString=this.str32ToString or {}

  if InfCore then
    InfCore.str32ToString=InfCore.str32ToString or {}
    InfCore.unknownStr32=InfCore.unknownStr32 or {}
  end

  local result=nil

  if InfCore and InfCore.str32ToString then
    result=InfCore.str32ToString[value]
  end

  if result==nil then
    result=this.str32ToString[value]

    if result and InfCore and InfCore.str32ToString then
      InfCore.str32ToString[value]=result
    end
  end

  if result==nil and InfLookup and InfLookup.StrCode32ToString then
    result=InfLookup.StrCode32ToString(value,isStrCode)
  end

  if result==nil then
    if isStrCode and InfCore and InfCore.unknownStr32 then
      InfCore.unknownStr32[value]=true
    end

    return value
  end

  return result
end

function this.MergeTable(dst,src)
  if type(dst)~="table" or type(src)~="table" then
    return
  end

  for key,value in pairs(src)do
    dst[key]=value
  end
end

function this.MergeMessageSignatures(dst,src)
  if type(dst)~="table" or type(src)~="table" then
    return
  end

  for sender,senderMessages in pairs(src)do
    if type(senderMessages)=="table" then
      dst[sender]=dst[sender] or {}

      for messageName,signature in pairs(senderMessages)do
        dst[sender][messageName]=signature
      end
    end
  end
end

function this.ImportLookupModule(module)
  if type(module)~="table" then
    return
  end

  if module.DEBUG_strCode32List then
    this.AddToStr32StringLookup(module.DEBUG_strCode32List)

    if InfLookup and InfLookup.AddToStr32StringLookup then
      InfLookup.AddToStr32StringLookup(module.DEBUG_strCode32List)
    end
  end

  if module.lookups then
    this.MergeTable(this.lookups,module.lookups)

    if InfLookup and InfLookup.lookups then
      this.MergeTable(InfLookup.lookups,module.lookups)
    end
  end

  if module.signatureTypes then
    this.MergeTable(this.signatureTypes,module.signatureTypes)

    if InfLookup and InfLookup.signatureTypes then
      this.MergeTable(InfLookup.signatureTypes,module.signatureTypes)
    end
  end

  if module.messageSignatures then
    this.MergeMessageSignatures(this.messageSignatures,module.messageSignatures)

    if InfLookup and InfLookup.messageSignatures then
      this.MergeMessageSignatures(InfLookup.messageSignatures,module.messageSignatures)
    end
  end
end

function this.ImportLookupModules(missionTable)
  if Tpp and Tpp._requireList then
    for i,libName in ipairs(Tpp._requireList)do
      this.ImportLookupModule(_G[libName])
    end
  end

  if InfModules then
    for i,module in ipairs(InfModules)do
      this.ImportLookupModule(module)
    end
  end

  if missionTable then
    for name,module in pairs(missionTable)do
      this.ImportLookupModule(module)
    end
  end
end

function this.RefreshLookups()
  this.AddToStr32StringLookup(this.DEBUG_strCode32List)

  this.lookups.CassettePlay=this.CassettePlay

  if not InfLookup then
    return
  end

  InfLookup.lookups=InfLookup.lookups or {}
  InfLookup.signatureTypes=InfLookup.signatureTypes or {}
  InfLookup.messageSignatures=InfLookup.messageSignatures or {}

  InfLookup.lookups.CassettePlay=this.CassettePlay

  InfLookup.signatureTypes.CrawlSideRoll=this.signatureTypes.CrawlSideRoll
  InfLookup.signatureTypes.cassettePlay=this.signatureTypes.cassettePlay
  InfLookup.signatureTypes.holdupCancelLookToPlayer=this.signatureTypes.holdupCancelLookToPlayer

  InfLookup.messageSignatures.GameObject=InfLookup.messageSignatures.GameObject or {}
  InfLookup.messageSignatures.Player=InfLookup.messageSignatures.Player or {}
  InfLookup.messageSignatures.Terminal=InfLookup.messageSignatures.Terminal or {}

  InfLookup.messageSignatures.GameObject.HoldupCancelLookToPlayer=this.signatureTypes.holdupCancelLookToPlayer
  InfLookup.messageSignatures.Player.CrawlSideRoll=this.signatureTypes.CrawlSideRoll
  InfLookup.messageSignatures.Terminal.CassettePlay=this.signatureTypes.cassettePlay
end

function this.GetMessageSignature(sender,messageId)
  local senderSignatures=this.messageSignatures[sender]

  if senderSignatures and senderSignatures[messageId] then
    return senderSignatures[messageId]
  end

  if InfLookup and InfLookup.messageSignatures then
    senderSignatures=InfLookup.messageSignatures[sender]

    if senderSignatures then
      return senderSignatures[messageId]
    end
  end

  return nil
end

function this.Lookup(lookupType,value)
  if value==nil then
    return nil
  end

  local lookup=this.lookups[lookupType]

  if type(lookup)=="function" then
    return lookup(value)
  elseif type(lookup)=="table" then
    return lookup[value]
  end

  if InfLookup and InfLookup.Lookup then
    return InfLookup.Lookup(lookupType,value)
  end

  return nil
end

function this.StringifyArg(value,argDef)
  if not argDef then
    return tostring(value)
  end

  if value==nil then
    return "nil"
  end

  local lookupValue=this.Lookup(argDef.argType,value)
  local lookupText="~"

  if lookupValue~=nil and tostring(lookupValue)~=tostring(value) then
    lookupText=tostring(lookupValue)
  end

  return "--[["
    ..tostring(argDef.argName or "")
    ..":"
    ..tostring(argDef.argType or "")
    ..":"
    ..lookupText
    .."]] "
    ..tostring(value)
end

function this.PrintMessageSignature(sender,messageId,signature,...)
  local args={...}
  local text=tostring(sender).."."..tostring(messageId).."("

  for i=1,4 do
    text=text..this.StringifyArg(args[i],signature and signature[i] or nil)

    if i<4 then
      text=text..", "
    end
  end

  text=text..")"

  InfCore.Log("OnMessage[]: "..text)

  return tostring(sender).."."..tostring(messageId),text
end

function this.PrintOnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
  this.RefreshLookups()

  local senderStr=sender
  local messageIdStr=messageId

  if type(senderStr)=="number" then
    senderStr=this.StrCode32ToString(senderStr,true)
  end

  if type(messageIdStr)=="number" then
    messageIdStr=this.StrCode32ToString(messageIdStr,true)
  end

  local signature=this.GetMessageSignature(senderStr,messageIdStr)

  if not signature then
    this.unknownMessages[senderStr]=this.unknownMessages[senderStr] or {}
    this.unknownMessages[senderStr][messageIdStr]=true
  end

  return this.PrintMessageSignature(senderStr,messageIdStr,signature,arg0,arg1,arg2,arg3)
end

function this.GetListenerName(listener)
  if not listener then
    return nil
  end

  if listener.name then
    return tostring(listener.name)
  end

  if InfModules then
    for i,module in ipairs(InfModules)do
      if module==listener then
        return tostring(module.name or ("InfModule"..tostring(i)))
      end
    end
  end

  return tostring(listener)
end

function this.IsArrayIndex(value)
  return type(value)=="number"
    and value>=1
    and value<=512
    and math.floor(value)==value
end

function this.TokenMatches(a,b)
  if a==nil or b==nil then
    return false
  end

  if a==b then
    return true
  end

  if type(a)=="string" and type(b)=="number" then
    return StrCode32(a)==b
  end

  if type(a)=="number" and type(b)=="string" then
    return a==StrCode32(b)
  end

  return false
end

function this.TableContainsMessage(node,messageId,depth)
  if type(node)~="table" then
    return false
  end

  depth=depth or 0

  if depth>12 then
    return false
  end

  if node.msg and this.TokenMatches(node.msg,messageId) then
    return true
  end

  if node.message and this.TokenMatches(node.message,messageId) then
    return true
  end

  if node.messageId and this.TokenMatches(node.messageId,messageId) then
    return true
  end

  for key,value in pairs(node)do
    if not this.IsArrayIndex(key) and this.TokenMatches(key,messageId) then
      return true
    end

    if type(value)=="table" and this.TableContainsMessage(value,messageId,depth+1) then
      return true
    end
  end

  return false
end

function this.TableContainsSenderAndMessage(node,sender,messageId,depth)
  if type(node)~="table" then
    return false
  end

  depth=depth or 0

  if depth>12 then
    return false
  end

  for senderKey,senderEntries in pairs(node)do
    if not this.IsArrayIndex(senderKey)
      and this.TokenMatches(senderKey,sender)
      and this.TableContainsMessage(senderEntries,messageId,depth+1) then
      return true
    end

    if type(senderEntries)=="table" then
      if senderEntries.sender
        and this.TokenMatches(senderEntries.sender,sender)
        and this.TableContainsMessage(senderEntries,messageId,depth+1) then
        return true
      end

      if senderEntries.senderName
        and this.TokenMatches(senderEntries.senderName,sender)
        and this.TableContainsMessage(senderEntries,messageId,depth+1) then
        return true
      end

      if this.TableContainsSenderAndMessage(senderEntries,sender,messageId,depth+1) then
        return true
      end
    end
  end

  return false
end

function this.ListenerHasMessage(listener,sender,messageId)
  if not listener or listener==this then
    return false
  end

  if listener.Messages then
    local ok,messages=pcall(listener.Messages)

    if ok
      and type(messages)=="table"
      and this.TableContainsSenderAndMessage(messages,sender,messageId,0) then
      return true
    end
  end

  if listener.messageExecTable then
    if this.TableContainsMessage(listener.messageExecTable,messageId,0)
      or this.TableContainsSenderAndMessage(listener.messageExecTable,sender,messageId,0) then
      return true
    end
  end

  if listener._messageExecTable then
    if this.TableContainsMessage(listener._messageExecTable,messageId,0)
      or this.TableContainsSenderAndMessage(listener._messageExecTable,sender,messageId,0) then
      return true
    end
  end

  return false
end

function this.AddReceiver(listener)
  if not V_FrameWork then
    return
  end

  local context=V_FrameWork._V_DoMessagesLookUp_CurrentContext

  if not context or not listener or listener==this then
    return
  end

  local name=this.GetListenerName(listener)

  if not name or context.receiverSet[name] then
    return
  end

  context.receiverSet[name]=true
  context.receivers[#context.receivers+1]=name
end

function this.GetReceiverString(sender,messageId,context)
  local receivers={}
  local receiverSet={}

  local function add(name)
    if name and not receiverSet[name] then
      receiverSet[name]=true
      receivers[#receivers+1]=name
    end
  end

  if context and context.receivers then
    for i,name in ipairs(context.receivers)do
      add(name)
    end
  end

  if V_FrameWork and V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder then
    for i,listener in ipairs(V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder)do
      if this.ListenerHasMessage(listener,sender,messageId) then
        add(this.GetListenerName(listener))
      end
    end
  end

  if InfModules then
    for i,module in ipairs(InfModules)do
      if module and module.OnMessage and this.ListenerHasMessage(module,sender,messageId) then
        add(this.GetListenerName(module))
      end
    end
  end

  if #receivers==0 then
    return "None"
  end

  return table.concat(receivers,", ")
end

function this.EnsureTrackingTables()
  if not V_FrameWork then
    return
  end

  V_FrameWork._V_DoMessagesLookUp_RegisteredListeners=
    V_FrameWork._V_DoMessagesLookUp_RegisteredListeners or {}

  V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder=
    V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder or {}
end

function this.WrapListener(listener)
  if not listener
    or listener==this
    or type(listener.OnMessage)~="function"
    or listener._V_DoMessagesLookUp_OnMessageWrapped then
    return
  end

  listener._V_DoMessagesLookUp_OnMessageWrapped=true

  local OnMessage=listener.OnMessage

  listener.OnMessage=function(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
    local previous=nil

    if V_FrameWork then
      previous=V_FrameWork._V_DoMessagesLookUp_CurrentListener
      V_FrameWork._V_DoMessagesLookUp_CurrentListener=listener
    end

    local r1,r2,r3,r4,r5,r6,r7,r8=
      OnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)

    if V_FrameWork then
      V_FrameWork._V_DoMessagesLookUp_CurrentListener=previous
    end

    return r1,r2,r3,r4,r5,r6,r7,r8
  end
end

function this.TrackListener(listener)
  if not V_FrameWork
    or not listener
    or type(listener)~="table"
    or not listener.OnMessage then
    return false
  end

  this.EnsureTrackingTables()
  this.WrapListener(listener)

  if V_FrameWork._V_DoMessagesLookUp_RegisteredListeners[listener] then
    return false
  end

  V_FrameWork._V_DoMessagesLookUp_RegisteredListeners[listener]=true
  V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder[
    #V_FrameWork._V_DoMessagesLookUp_RegisteredListenersOrder+1
  ]=listener

  listener._V_FrameWorkRegistered=true

  return true
end

function this.ImportExistingListeners()
  if not V_FrameWork then
    return
  end

  local names={
    "listeners",
    "_listeners",
    "listenerList",
    "messageListeners",
    "registeredListeners",
  }

  for i,listName in ipairs(names)do
    local list=V_FrameWork[listName]

    if type(list)=="table" then
      for key,value in pairs(list)do
        if type(key)=="table" and key.OnMessage then
          this.TrackListener(key)
        end

        if type(value)=="table" and value.OnMessage then
          this.TrackListener(value)
        end
      end
    end
  end
end

function this.RegisterInfModuleListeners()
  if not V_FrameWork or not V_FrameWork.RegisterListener or not InfModules then
    return
  end

  for i,module in ipairs(InfModules)do
    if module and module.OnMessage then
      if not module._V_FrameWorkRegistered then
        V_FrameWork.RegisterListener(module)
      else
        this.TrackListener(module)
      end
    end
  end
end

function this.LoadLibraries()
  if not V_FrameWork or not V_FrameWork.RegisterListener then
    return
  end

  this.EnsureTrackingTables()

  if not V_FrameWork._V_DoMessagesLookUp_RegisterListenerHooked then
    V_FrameWork._V_DoMessagesLookUp_RegisterListenerHooked=true

    local RegisterListener=V_FrameWork.RegisterListener

    V_FrameWork.RegisterListener=function(listener)
      if not listener then
        return
      end

      if V_FrameWork._V_DoMessagesLookUp_RegisteredListeners[listener] then
        return
      end

      this.TrackListener(listener)

      return RegisterListener(listener)
    end
  end

  if Tpp and Tpp.DoMessage and not Tpp._V_DoMessagesLookUp_DoMessageHooked then
    Tpp._V_DoMessagesLookUp_DoMessageHooked=true

    local DoMessage=Tpp.DoMessage

    Tpp.DoMessage=function(messageExecTable,checkMessageOption,sender,messageId,arg0,arg1,arg2,arg3,strLogText)
      local context=V_FrameWork and V_FrameWork._V_DoMessagesLookUp_CurrentContext
      local listener=V_FrameWork and V_FrameWork._V_DoMessagesLookUp_CurrentListener

      if context
        and listener
        and listener~=this
        and this.TokenMatches(sender,context.sender)
        and this.TokenMatches(messageId,context.messageId) then

        if this.TableContainsMessage(messageExecTable,messageId,0)
          or this.TableContainsSenderAndMessage(messageExecTable,sender,messageId,0) then
          this.AddReceiver(listener)
        end
      end

      return DoMessage(
        messageExecTable,
        checkMessageOption,
        sender,
        messageId,
        arg0,
        arg1,
        arg2,
        arg3,
        strLogText
      )
    end
  end

  if V_FrameWork.BroadcastMessage and not V_FrameWork._V_DoMessagesLookUp_BroadcastMessageHooked then
    V_FrameWork._V_DoMessagesLookUp_BroadcastMessageHooked=true

    local BroadcastMessage=V_FrameWork.BroadcastMessage

    V_FrameWork.BroadcastMessage=function(sender,messageId,arg0,arg1,arg2,arg3,strLogText)
      this.RefreshLookups()
      this.ImportLookupModules(nil)
      this.ImportExistingListeners()
      this.RegisterInfModuleListeners()

      local previousContext=V_FrameWork._V_DoMessagesLookUp_CurrentContext

      local context={
        sender=sender,
        messageId=messageId,
        receivers={},
        receiverSet={},
      }

      V_FrameWork._V_DoMessagesLookUp_CurrentContext=context

      local identity=this.PrintOnMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)

      BroadcastMessage(sender,messageId,arg0,arg1,arg2,arg3,strLogText)

      V_FrameWork._V_DoMessagesLookUp_CurrentContext=previousContext

      if identity then
        InfCore.Log(
          "/OnMessage: "
          ..tostring(identity)
          .." recievers: "
          ..this.GetReceiverString(sender,messageId,context)
        )
      end
    end
  end

  if TppMain and TppMain.SetMessageFunction and not TppMain._V_DoMessagesLookUp_SetMessageFunctionHooked then
    TppMain._V_DoMessagesLookUp_SetMessageFunctionHooked=true

    local SetMessageFunction=TppMain.SetMessageFunction

    TppMain.SetMessageFunction=function(missionTable)
      SetMessageFunction(missionTable)
      this.ImportLookupModules(missionTable)
      this.RegisterInfModuleListeners()
    end
  end

  this.ImportLookupModules(nil)
  this.ImportExistingListeners()
  this.RegisterInfModuleListeners()
end

function this.PostAllModulesLoad()
  this.RefreshLookups()
  this.ImportLookupModules(nil)
end

function this.OnInitializeTop(missionTable)
  this.RefreshLookups()
  this.ImportLookupModules(missionTable)
end

function this.Init(missionTable)
  this.RefreshLookups()
  this.ImportLookupModules(missionTable)
end

function this.OnReload(missionTable)
  this.RefreshLookups()
  this.ImportLookupModules(missionTable)
end

function this.Save()
end

return this