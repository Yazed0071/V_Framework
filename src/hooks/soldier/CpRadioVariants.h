#pragma once

#include <cstdint>

void CpRadioVariants_OnRadioOnAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker);
void CpRadioVariants_OnRadioOffAir(std::uint32_t cp, std::uint8_t radioType, std::uint16_t speaker);   // StepRadio left the call
void CpRadioVariants_OnCpIdle(std::uint32_t cp, std::uint8_t* liveService);   // a mortar call the queue pushed out is asked again
void CpRadioVariants_ClearForNewMission();
bool CpRadioVariants_SpeakSoldierHalf(std::uint32_t priority, std::uint32_t event, std::uint32_t voiceType, std::uint32_t label);
std::uint32_t CpRadioVariants_LabelForCall(std::uint32_t owner, std::uint8_t radioType, std::uint16_t member);   // CallWithRadioType plays only its type's label: 0 = keep it
std::uint32_t CpRadioVariants_SurveyLabel(std::uint32_t owner, std::uint8_t radioType, std::uint16_t member);   // after the lost-hostage label had its say
enum class FinderWait : std::uint8_t { Vanilla, Hold, Release };
bool CpRadioVariants_HoldFinder(std::uint32_t soldierIndex);   // the CP told him to maintain watch: once, as his find call is done
FinderWait CpRadioVariants_FinderWait(std::uint32_t soldierIndex);   // each update of his downed-comrade Wait step
bool CpRadioVariants_IsWaker(std::uint32_t soldierIndex);   // the team's man sent to see to the downed comrade before the sweep
void CpRadioVariants_OnDownComradeNode(std::uint32_t soldierIndex, bool awake);
void CpRadioVariants_OnUavDown(std::uint32_t cp, std::uint8_t* cpController, const float spot[3]);   // user: CPR0510, the nearest soldier goes to check
bool CpRadioVariants_AssignRadioInCombat(std::uint32_t cp, std::uint32_t soldierIndex, std::uint32_t label, bool noPose);   // soldierIndex 0xFFFFFFFF: the CP picks its radio man
bool CpRadioVariants_IsWatchingFinder(std::uint32_t soldierIndex);   // told to maintain watch, or about to be: leave him at his spot
bool CpRadioVariants_TakesVehicleBlast(std::uint32_t soldierIndex);   // his blast report brings the survey team: vanilla files no point for it
void CpRadioVariants_VehicleBlastReported(std::uint32_t soldierIndex);   // his radio step has left
std::uint64_t CpRadioVariants_CpCalmedMs(std::uint32_t cp);   // tick the CP last dropped back to sneak (or the mission began)
bool CpRadioVariants_SurveyMemberOf(std::uint16_t soldierIndex, std::uint32_t& cp, std::uint16_t& member);   // he's on a survey team that's out
void CpRadioVariants_CallOffSurvey(std::uint32_t cp);   // its CP heard "All clear" from one of them
void CpRadioVariants_OnSitrepUnanswered(std::uint32_t cp, const std::uint32_t* teams, std::uint32_t count);   // CpRadioResponse's SITREP groups nobody answered; the CP went to caution
#ifdef _DEBUG
bool CpRadioVariants_ProbeSurveyCp(std::uint32_t soldierIndex, std::uint32_t& cp);   // TEMP probe: he's on a survey team that's out
#endif
std::uint64_t CpRadioVariants_RadioHeardMs(std::uint32_t soldierIndex);   // last tick a CP call with him in it was on air, 0 if never
void CpRadioVariants_OnCpChannelVoice(void* soundController, std::uint32_t owner, std::uint32_t voiceType, std::uint32_t label);   // every SoundControllerImpl::CallVoice
bool CpRadioVariants_HoldChaffJam(std::uint32_t cp, std::uint32_t soldierIndex);
void CpRadioVariants_OnSoldierDamages(std::uint32_t soldierIndex, const void* damages, std::uint32_t count);   // the support heli hitting a soldier

bool Install_CpRadioVariants_Hooks();
bool Uninstall_CpRadioVariants_Hooks();
