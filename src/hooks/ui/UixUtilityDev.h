#pragma once

#include <cstddef>

namespace UixUtilityControl
{
    namespace dev   // a component overload sits at its node one + 8
    {
        constexpr std::size_t SendTrigger = 0x1D0, SendTriggerToParent = 0x1D8, IsTriggerReceived = 0x1E0, GetTriggerPool = 0x1F0;
        constexpr std::size_t SendVisibleWindowMessage = 0x1F8, SendConnectWindowMessage = 0x200, GetChildWindowInterface = 0x208;
        constexpr std::size_t GetLayoutOfLayout = 0x210, GetLayoutOfWindow = 0x220, IsHaveLayout = 0x228, GetModel = 0x230;
        constexpr std::size_t GetAnimationOfModel = 0x238, GetAnimationOfLayout = 0x240, IsHaveModelNodeCommon = 0x248;
        constexpr std::size_t GetModelNodeCommon = 0x250, GetModelNodeText = 0x258, GetModelNodeMesh = 0x260, GetModelNodeLine = 0x268;
        constexpr std::size_t GetOrthogonalProjectionScale = 0x270;
        constexpr std::size_t SetVisible = 0x278, IsVisible = 0x288, SetAlpha = 0x298, GetAlpha = 0x2A8, SetColorRGB = 0x2D8;
        constexpr std::size_t SetColorGroup = 0x2E8, GetColorGroup = 0x2F8, GetTranslation = 0x300, SetTranslation = 0x310;
        constexpr std::size_t SetTranslationX = 0x320, SetTranslationY = 0x330, SetTranslationZ = 0x340;
        constexpr std::size_t GetRotation = 0x350, GetRotationDegree = 0x360, SetRotation = 0x370, SetRotationDegree = 0x380;
        constexpr std::size_t SetRotationDegreeX = 0x390, SetRotationDegreeY = 0x3A0, SetRotationDegreeZ = 0x3B0, SetQuaternion = 0x3C0;
        constexpr std::size_t SetScaleFloat = 0x3D0, SetScaleVector = 0x3E0, GetScale = 0x3F0;
        constexpr std::size_t SetStatusLine = 0x400, SetStatusText = 0x408, ResetStatusLine = 0x420, ResetStatusText = 0x428;
        constexpr std::size_t SetVerticalAlign = 0x440, SetTextAlign = 0x448, SetFontSize = 0x450;
        constexpr std::size_t GetLineWidth = 0x458, GetLineHeight = 0x460, SetLineWidth = 0x468, SetLineHeight = 0x470;
        constexpr std::size_t ConnectComponentByName = 0x480, ConnectComponentToNode = 0x488;
        constexpr std::size_t GetTraversalTranslation = 0x490, GetTraversalScale = 0x498;
        constexpr std::size_t SetTextureNameLevel = 0x4B8, SetTextureName = 0x4C0, CheckAndSetTextureNameLevel = 0x4D8, CheckAndSetTextureName = 0x4E0;
        constexpr std::size_t CreatePrefetchTexture = 0x500, RemovePrefetchTexture = 0x508, IsReadyPrefetchTexture = 0x510;
        constexpr std::size_t RegisterLoadingMeshTexture = 0x518, UnregisterLoadingMeshTexture = 0x520, SetLoadingTexture = 0x528;
        constexpr std::size_t IsDispLoadingTexture = 0x530, IsTextureLoaded = 0x538, SetTextureLevel = 0x540;
        constexpr std::size_t SetShaderParameter = 0x548, SetShaderTechnique = 0x550;
        constexpr std::size_t SetShaderBaseTexUvShift = 0x558, SetShaderMaskTexUvShift = 0x560, SetShaderScreenTexUvShift = 0x568;
        constexpr std::size_t SetShaderBaseTexUvRepeat = 0x570, SetShaderMaskTexUvRepeat = 0x578, SetShaderScreenTexUvRepeat = 0x580;
        constexpr std::size_t SetVertexTranslate = 0x588, FindWindow = 0x590;
        constexpr std::size_t EnableAllInheritance = 0x598, DisableAllInheritance = 0x5C8;
        constexpr std::size_t PostShowAndStartMessage = 0x600, PostHideAndStopMessage = 0x610, PostWakeLayoutMessage = 0x618, PostSleepLayoutMessage = 0x620;
        constexpr std::size_t StartAnimation = 0x628, StartInstantAnimation = 0x630, StartLoopAnimation = 0x638, StartAnimationWithRatio = 0x640;
        constexpr std::size_t StartAnimationWithStartRatio = 0x648, RecoverForAnimationWithRatio = 0x650, PauseAnimation = 0x658;
        constexpr std::size_t PauseInitAnimation = 0x660, PauseEndAnimation = 0x668, IsAnimationRunning = 0x670;
        constexpr std::size_t SetAnimationStopTime = 0x678, SetAnimationSpeed = 0x680, IsAnimationPastTime = 0x688;
        constexpr std::size_t SetTextForModelNodeText = 0x6A0, SetScrollTextForModelNodeText = 0x6A8, SetupTextUnits = 0x6C0;
        constexpr std::size_t SetTextUnitsForModelNodeText = 0x6C8, CreateBoxText = 0x6D0, CreateScrollBoxText = 0x6D8, GetLangText = 0x6E0;
        constexpr std::size_t StartTextScroll = 0x6F0, StopTextScroll = 0x6F8, GetTextTotalHeightInTextNode = 0x700, GetTextMaxWidthInTextNode = 0x708;
        constexpr std::size_t GetTextNodeWidth = 0x710, GetTextNodeHeight = 0x718, SetTextNodeWidth = 0x720, SetTextNodeHeight = 0x728;
        constexpr std::size_t SetModelNodePriority = 0x788, UpdateModelDrawOrder = 0x790, SetModelStencil = 0x798, SetModelNodeBillboard = 0x7A0;
        constexpr std::size_t GetDrawPriority = 0x7A8, SetDrawPriority = 0x7B0, GetDrawPriorityFromTable = 0x7B8;
        constexpr std::size_t DisableShaderResetMesh = 0x7C0, DisableShaderResetModel = 0x7C8, DisableShaderResetLayout = 0x7D0;
        constexpr std::size_t GetGameFrameRawDeltaTime = 0x7D8, GetGameFrameElapsedTimeSinceStartup = 0x7E0;
        constexpr std::size_t RotQuatToEuler = 0x7E8, RotEulerToQuat = 0x7F0, RotEularRadianToDegree = 0x7F8, RotEularDegreeToRadian = 0x800;
        constexpr std::size_t GetPathIdFromChar = 0x808, GetPaletteColor = 0x810;
        constexpr std::size_t GetButtonNumber = 0x018, IsButtonPress = 0x028, IsButtonPressByName = 0x030;
        constexpr std::size_t IsButtonPressing = 0x038, IsButtonPressingByName = 0x040, IsButtonRelease = 0x048, IsButtonReleaseByName = 0x050;
        constexpr std::size_t IsButtonRepeat = 0x068, IsButtonRepeatByName = 0x070, IsButtonLongPress = 0x078, IsButtonLongPressByName = 0x080;
        constexpr std::size_t GetButtonLongPressResultByName = 0x088, GetAnalogStickAxis = 0x090, IsUseKeyboard = 0x0B0, ButtonNameToKeyCode = 0x0E0;
        constexpr std::size_t SetDisablePad = 0x098, ResetDisablePad = 0x0A0, IsDisablePad = 0x0A8;
        constexpr std::size_t IsKeyboardPress = 0x0E8, IsKeyboardPressByName = 0x0F0, IsKeyboardPressing = 0x0F8, IsKeyboardPressingByName = 0x100;
        constexpr std::size_t IsKeyboardRelease = 0x108, IsKeyboardReleaseByName = 0x110;
        constexpr std::size_t GetMouseMoveXY = 0x158, GetMouseWheel = 0x168, IsMouseButtonDown = 0x170, IsMouseButtonDownByName = 0x178;
        constexpr std::size_t IsMouseButtonPressing = 0x180, IsMouseButtonPressingByName = 0x188, IsMouseButtonUp = 0x190, IsMouseButtonUpByName = 0x198;
    }
}
