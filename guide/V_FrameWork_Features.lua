local this = {}

local PAGE        = "V_FrameWork_Features"
local PAGE_LAYOUT = "VFrameWorkFeatures"
local PAGE_FILE   = "/Assets/tpp/ui/LayoutAsset/sys_pause/UI_sys_pus_tips.uilb"
local PAGE_MODEL  = "UI_sys_pus_tips"
local PAGE_ON     = "UI_sys_pus_tips_on"
local KEY_HELP    = "UI_sys_pus_key_help"
local PAGE_Y      = 32
local BODY_X      = -15
local PLATE_Y     = 2.78
local PLATE_MOVE  = 15.1
local TURN_SOUND  = 42

local ID = {
  HEADER_TEXT = 0xF6AE19C0,
  TITLE_TEXT  = 0x3AD1256D,
  BODY_TEXT   = 0xCE018BFE,
  PLATE       = 0x97B13C03,
}

local CATEGORIES = {
  {
    name            = "V_Features_Gameplay",
    labelLang       = "V_features_gameplay",
    descriptionLang = "V_features_gameplay_help",
    icon            = "MBM_CUSTOM",
    rows = {
      {labelLang = "V_features_laser_switch", pagesLang = {"V_features_laser_switch_1"}},
      {labelLang = "V_features_call_em", pagesLang = {"V_features_call_em_1"}},
      {labelLang = "V_features_lie_to_them", pagesLang = {"V_features_lie_to_them_1", "V_features_lie_to_them_2"}},
      {labelLang = "V_features_d_horse_pat", pagesLang = {"V_features_d_horse_pat_1"}},
      {labelLang = "name_buddy_bg", pagesLang = {"tips_info_073_00", "tips_info_073_01"}, debugBuildOnly = true},
      {labelLang = "V_features_ocelot_and_quiet", pagesLang = {"V_features_ocelot_and_quiet_1"}},
      {labelLang = "V_features_quiet_s_cqc", pagesLang = {"V_features_quiet_s_cqc_1"}},
      {labelLang = "V_features_pequod_rockets", pagesLang = {"V_features_pequod_rockets_1"}},
      {labelLang = "V_features_pequod_lz", pagesLang = {"V_features_pequod_lz_1"}},
      {labelLang = "V_features_acc_idroid", pagesLang = {"V_features_acc_idroid_1"}},
      {labelLang = "V_features_volume_sliders", pagesLang = {"V_features_volume_sliders_1"}},
    },
  },
  {
    name            = "V_Features_EnemyBehavior",
    labelLang       = "V_features_enemybehavior",
    descriptionLang = "V_features_enemybehavior_help",
    icon            = "MBM_BASE_SECURITY",
    rows = {
      {labelLang = "V_features_survey_teams", pagesLang = {"V_features_survey_teams_1", "V_features_survey_teams_2"}},
      {labelLang = "V_features_backup_requests", pagesLang = {"V_features_backup_requests_1"}},
      {labelLang = "V_features_downed_uavs", pagesLang = {"V_features_downed_uavs_1"}},
      {labelLang = "V_features_hold_up_self_release", pagesLang = {"V_features_hold_up_self_release_1", "V_features_hold_up_self_release_2"}},
      {labelLang = "V_features_vip_soldiers", pagesLang = {"V_features_vip_soldiers_1"}},
      {labelLang = "V_features_radio_call_signs", pagesLang = {"V_features_radio_call_signs_1"}},
      {labelLang = "V_features_dying_enemies", pagesLang = {"V_features_dying_enemies_1"}},
      {labelLang = "V_features_long_barrel_radio", pagesLang = {"V_features_long_barrel_radio_1"}},
    },
  },
  {
    name            = "V_Features_PlayerLines",
    labelLang       = "V_features_playerlines",
    descriptionLang = "V_features_playerlines_help",
    icon            = "MBM_PFRATING",
    rows = {
      {labelLang = "V_features_about_these_lines", pagesLang = {"V_features_about_these_lines_1"}},
      {labelLang = "V_features_player_death_lines", pagesLang = {"V_features_player_death_lines_1", "V_features_player_death_lines_2"}},
      {labelLang = "V_features_player_pain_lines", pagesLang = {"V_features_player_pain_lines_1", "V_features_player_pain_lines_2"}},
      {labelLang = "V_features_snake_only_player_cqc", pagesLang = {"V_features_snake_only_player_cqc_1"}},
      {labelLang = "V_features_snake_only_player_idroid_lines", pagesLang = {"V_features_snake_only_player_idroid_lines_1", "V_features_snake_only_player_idroid_lines_2"}},
      {labelLang = "V_features_pin_up_posters", pagesLang = {"V_features_pin_up_posters_1"}},
      {labelLang = "V_features_blood_runs_deep", pagesLang = {"V_features_blood_runs_deep_1"}},
      {labelLang = "V_features_snake_only_hand_of_jehuty", pagesLang = {"V_features_snake_only_hand_of_jehuty_1"}},
      {labelLang = "V_features_player_buddy_lines", pagesLang = {"V_features_player_buddy_lines_1"}},
      {labelLang = "name_buddy_dd", pagesLang = {"V_features_buddy_dd_1", "V_features_buddy_dd_2", "V_features_buddy_dd_3", "V_features_buddy_dd_4", "V_features_buddy_dd_5"}},
      {labelLang = "name_buddy_q", pagesLang = {"V_features_buddy_q_1", "V_features_buddy_q_2", "V_features_buddy_q_3"}},
      {labelLang = "name_buddy_dw", pagesLang = {"V_features_buddy_dw_1"}},
      {labelLang = "name_buddy_dh", pagesLang = {"V_features_buddy_dh_1", "V_features_buddy_dh_2"}},
    },
  },
  {
    name            = "V_Features_NpcLines",
    labelLang       = "V_features_npclines",
    descriptionLang = "V_features_npclines_help",
    icon            = "MSN_LOG",
    rows = {
      {labelLang = "V_features_ocelot_binocular_comments", pagesLang = {"V_features_ocelot_binocular_comments_1", "V_features_ocelot_binocular_comments_2"}},
      {labelLang = "V_features_ocelot_fob_radio", pagesLang = {"V_features_ocelot_fob_radio_1", "V_features_ocelot_fob_radio_2"}},
      {labelLang = "V_features_miller_chopper_radio", pagesLang = {"V_features_miller_chopper_radio_1"}},
      {labelLang = "V_features_pequod", pagesLang = {"V_features_pequod_1", "V_features_pequod_2", "V_features_pequod_3"}},
      {labelLang = "V_features_soldiers", pagesLang = {"V_features_soldiers_1", "V_features_soldiers_2"}},
      {labelLang = "V_features_soldier_radio_calls", pagesLang = {"V_features_soldier_radio_calls_1", "V_features_soldier_radio_calls_2", "V_features_soldier_radio_calls_3"}},
      {labelLang = "V_features_soldier_conversations", pagesLang = {"V_features_soldier_conversations_1", "V_features_soldier_conversations_2"}},
      {labelLang = "V_features_mother_base_staff", pagesLang = {"V_features_mother_base_staff_1", "V_features_mother_base_staff_2"}},
      {labelLang = "V_features_captives", pagesLang = {"V_features_captives_1", "V_features_captives_2"}},
    },
  },
  {
    name            = "V_Features_Fixes",
    labelLang       = "V_features_fixes",
    descriptionLang = "V_features_fixes_help",
    icon            = "MBM_RESOURCE",
    rows = {
      {labelLang = "V_features_female_soldiers_hair", pagesLang = {"V_features_female_soldiers_hair_1"}},
      {labelLang = "V_features_custom_sounds", pagesLang = {"V_features_custom_sounds_1"}},
    },
  },
}

local categoryOfItem = {}
local shownCategory, shownSheet
local headerText, titleText, bodyText
local headerUnits, titleUnits, bodyUnits
local pageOn

function this.Messages()
  return Tpp.StrCode32Table{
    Timer = {
      {
        msg = "Finish",
        sender = "VFrameWorkFeaturesOpen",
        func = function()
          V_UixUtility.OpenIDroidPage(PAGE)
        end,
      },
    },
    UI = {
      {
        msg = "IDroidMenuItemSelected",
        func = function(itemId)
          if categoryOfItem[itemId] == nil then return end
          shownCategory = categoryOfItem[itemId]
          shownSheet    = 1
          if V_UixUtility.FindWindow(PAGE) == nil then
            V_UixUtility.CreateWindow{
              name      = PAGE,
              pauseMask = 0,
              layouts   = {
                {path = PAGE_FILE, id = PAGE_LAYOUT, drawPriority = "STRONG_PAUSE_BG"},
              },
            }
            return
          end
          V_UixUtility.OpenIDroidPage(PAGE)
        end,
      },
    },
  }
end

function this.ShowPage()
  local sheetCount = #shownCategory.sheets
  local sheet  = shownCategory.sheets[shownSheet]
  local header = shownCategory.label
  if shownCategory.labelLang then header = V_UixUtility.GetLangText(shownCategory.labelLang) or shownCategory.labelLang end
  local title = sheet.label
  if sheet.labelLang then title = V_UixUtility.GetLangText(sheet.labelLang) or sheet.labelLang end
  local body = sheet.body
  if sheet.bodyLang then body = V_UixUtility.GetLangText(sheet.bodyLang) or sheet.bodyLang end
  if sheetCount > 1 then
    if AssetConfiguration.GetDefaultCategory"Language" == "ara" then
      header = V_UixUtility.GetLangText("manual_key_page") .. "  <I=G=PAD_LEFT> <I=G=PAD_RIGHT> " .. header
    else
      header = header .. "  <I=G=PAD_LEFT> <I=G=PAD_RIGHT> " .. V_UixUtility.GetLangText("manual_key_page")
    end
  end
  if sheet.parts > 1 then
    if AssetConfiguration.GetDefaultCategory"Language" == "ara" then
      title = " (" ..  sheet.parts .. "/" .. sheet.part .. ")" .. title
    else
      title = title .. " (" .. sheet.part .. "/" .. sheet.parts .. ")"
    end
  end
  V_UixUtility.CreateBoxText(headerText, headerUnits, 6, header, false, true)
  V_UixUtility.CreateBoxText(titleText, titleUnits, 5, title, false, true)
  V_UixUtility.CreateBoxText(bodyText, bodyUnits, 25, body, true, true)
  V_UixUtility.StartAnimation(pageOn)
end

function this.OnPageSetup(window)
  if V_UixUtility.OpenIDroidPage(PAGE) then return end
  GkEventTimerManager.Start("VFrameWorkFeaturesOpen", 0.2)
end

function this.OnPageStart(window)
  if headerUnits == nil then
    headerUnits = V_UixUtility.CreateTextUnit(6)
    titleUnits  = V_UixUtility.CreateTextUnit(5)
    bodyUnits   = V_UixUtility.CreateTextUnit(25)
  end

  local layout = V_UixUtility.GetLayout(window, PAGE_LAYOUT)
  local model  = V_UixUtility.GetModel(layout, PAGE_MODEL)
  headerText   = V_UixUtility.GetModelNodeText(model, ID.HEADER_TEXT)
  titleText    = V_UixUtility.GetModelNodeText(model, ID.TITLE_TEXT)
  bodyText     = V_UixUtility.GetModelNodeText(model, ID.BODY_TEXT)
  pageOn       = V_UixUtility.GetAnimation(model, PAGE_ON)

  local plate = V_UixUtility.GetModelNodeMesh(model, ID.PLATE)
  V_UixUtility.SetScale(plate, 1, PLATE_Y, 1)
  V_UixUtility.SetTranslationY(plate, PLATE_MOVE)
  V_UixUtility.SetAlpha(plate, 1)

  V_UixUtility.SetVisible(V_UixUtility.GetLayout(layout, KEY_HELP), false)
  V_UixUtility.SetTranslationY(model, PAGE_Y)
  if AssetConfiguration.GetDefaultCategory"Language" == "ara" then
    V_UixUtility.SetTranslationX(titleText, 16) -- make 0
    V_UixUtility.SetTextAlign(headerText, 2) -- make 0
    V_UixUtility.SetTextAlign(titleText, 2) -- make 0
    V_UixUtility.SetTextAlign(bodyText, 2) -- make 0
  else
    V_FrameWork.Log("V_UixUtility.GetTranslation(headerText) == "..V_UixUtility.GetTranslation(headerText)) -- -53.099998474121
    V_UixUtility.SetTranslationX(headerText, -53.2)
    V_UixUtility.SetTranslationX(titleText, -53.2)
    V_UixUtility.SetTranslationX(bodyText, 0)
    V_UixUtility.SetTextAlign(headerText, 0)
    V_UixUtility.SetTextAlign(titleText, 0)
    V_UixUtility.SetTextAlign(bodyText, 0)
  end
  this.ShowPage()
end

function this.OnPageUpdate(window)
  if bodyText == nil or shownCategory == nil then return end
  if V_UixUtility.IsButtonPressByName("UI_RIGHT") and shownSheet < #shownCategory.sheets then
    shownSheet = shownSheet + 1
    V_UixUtility.StartSound(TURN_SOUND)
    this.ShowPage()
  elseif V_UixUtility.IsButtonPressByName("UI_LEFT") and shownSheet > 1 then
    shownSheet = shownSheet - 1
    V_UixUtility.StartSound(TURN_SOUND)
    this.ShowPage()
  end
end

function this.OnPageFinalize()
  headerText, titleText, bodyText, pageOn = nil, nil, nil, nil
end

function this.AddMenu()
  local menu = V_UixUtility.AddIDroidMenuItem{
    parent          = "MBM_DB",
    name            = "V_Features",
    labelLang       = "V_features_menu",
    descriptionLang = "V_features_menu_help",
    icon            = "/Assets/tpp/ui/texture/V_FrameWork/v_framework_logo.ftex",
    group           = true,
  }
  for _, category in ipairs(CATEGORIES) do
    local item = V_UixUtility.AddIDroidMenuItem{
      parent          = menu,
      name            = category.name,
      labelLang       = category.labelLang,
      descriptionLang = category.descriptionLang,
      icon            = category.icon,
    }
    category.sheets = {}
    for _, row in ipairs(category.rows) do
      if not row.debugBuildOnly or (V_TppBuddyService and V_TppBuddyService.GetBattleGearMainWeapon) then
        for part, page in ipairs(row.pagesLang) do
          category.sheets[#category.sheets + 1] = {labelLang = row.labelLang, part = part, parts = #row.pagesLang, bodyLang = page}
        end
      end
    end
    categoryOfItem[item] = category
  end
end

function this.Init(missionTable)
  if TppMission.IsFOBMission(vars.missionCode) then return end
  this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())

  V_UixUtility.SetWindowPage(PAGE, {
    OnSetup    = this.OnPageSetup,
    OnStart    = this.OnPageStart,
    OnUpdate   = this.OnPageUpdate,
    OnFinalize = this.OnPageFinalize,
  })
  this.AddMenu()
end

function this.OnReload(missionTable)
  if TppMission.IsFOBMission(vars.missionCode) then return end
  this.messageExecTable = Tpp.MakeMessageExecTable(this.Messages())
end

function this.OnMessage(sender, messageId, arg0, arg1, arg2, arg3, strLogText)
  if this.messageExecTable == nil then return end
  Tpp.DoMessage(this.messageExecTable, TppMission.CheckMessageOption, sender, messageId, arg0, arg1, arg2, arg3, strLogText)
end

return this
