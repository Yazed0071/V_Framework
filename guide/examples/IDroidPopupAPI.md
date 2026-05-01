# iDroid Menu Popup API

`V_FrameWork.ShowIDroidPopup` opens an `MbCommonPopupAct` free-text modal
popup **inside the iDroid menu**. This is the same UI element vanilla uses
for confirmation prompts when you're in the iDroid (e.g. "Are you sure you
want to abort?").

## Function

```lua
V_FrameWork.ShowIDroidPopup(text [, popupType]) -> boolean
V_TppUI.ShowIDroidPopup(text [, popupType])     -> boolean   -- alias
```

| Param | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | required | Body text to display. Free text — no LangProject hash needed. |
| `popupType` | number | 1 | Popup style / button layout, range `1..6`. Out-of-range falls back to 1. |

Returns `true` on success, `false` if:

- The build is unsupported (addresses not resolved).
- The popup `Window*` hasn't been captured yet — you have to open the iDroid
  menu at least once this session before the framework's `SetupOnce` hook
  catches the popup window. Open the iDroid → call this function.

## In-menu only

This popup is registered to the iDroid menu's UI window. It only renders
while that menu is open. Calling from open-world gameplay returns `false`
or no-ops.

If you need a popup **during gameplay**, the HUD popup pipeline is what
you want — but V_FrameWork doesn't currently expose it (use the game's
existing `TppUiCommand.SetPopupText` + `ShowPopup` Lua functions for that).

## Implementation

The C++ binding does real hook work:

1. **Hook `MbCommonPopupAct::SetupOnce`** at retail `0x140870B70`. The
   game calls this with `(this, popupWindow)` when the iDroid menu loads
   the popup widget. The framework's hook captures `popupWindow` into a
   static atomic. Source: [src/hooks/ui/MbCommonPopupHook.cpp](../../src/hooks/ui/MbCommonPopupHook.cpp).
2. **At Lua call time** (`l_ShowIDroidPopup` in
   [src/lua/SetLuaFunctions.cpp](../../src/lua/SetLuaFunctions.cpp)):
   - Validate text and popupType.
   - Call `MbCommonPopup::ShowPopup(text, popupType)` which:
     - Reads the captured `popupWindow` from the atomic.
     - Calls `FUN_145C3B320(window, 0, text, popupType, 0, 0, 0)` —
       the 7-arg popup driver. Internally it stashes the text via
       `UiCommonDataManager+0x868` and posts the MbCommonPopupAct
       triggers `SET_POPUP_TYPE_TRIGGER`, `SET_MESSAGE_TEXT_PTR_TRIGGER`,
       `SET_SELECTED_BUTTON_TRIGGER`, `RESET_DISABLE_CANCEL_TRIGGER`,
       and `OPEN_TRIGGER` to the popup window.

## Examples

```lua
V_FrameWork.ShowIDroidPopup("Mod initialized successfully")

V_FrameWork.ShowIDroidPopup(
    string.format("%d items loaded", #items))

V_FrameWork.ShowIDroidPopup("Apply changes?", 3)
```

## popupType values

`popupType` is forwarded to the popup window's `SET_POPUP_TYPE_TRIGGER`.
The popup layout reads it to decide button count / labels. Range 1..6.
Specific mapping varies by popup layout asset and you may need to
experiment to find the one matching your intent (info vs OK/Cancel vs
Yes/No).

## Limitations

- **Only works inside the iDroid menu** — by design.
- **SEH-guarded against stale window crashes.** If the captured
  popup window is from the wrong menu (e.g. title screen vs in-game
  iDroid) or is otherwise stale, the underlying helper raises an
  access violation. The framework catches it via SEH, returns false,
  and clears the captured window. Subsequent calls fail fast until
  the iDroid menu reopens and SetupOnce re-captures.
- **SetupOnce may fire for non-iDroid menus.** Title screen and main
  menu also use `MbCommonPopupAct`. The framework keeps the latest
  capture. To use this safely from a mod, watch the log for
  `[MbCommonPopup] SetupOnce captured popup window=...` AFTER the
  in-game iDroid menu has loaded, then call `ShowIDroidPopup` while
  that menu is still open.
- **No result polling yet.** Reading which button the user pressed
  requires hooking the popup-select trigger. Future work.
- **JP build TBD.** Japanese build addresses aren't resolved; calling on
  JP returns false.
- **First call before SetupOnce fires returns false.** The capture
  happens when the iDroid menu first loads, so a mod calling
  `ShowIDroidPopup` from `OnAllocate` (before any menu has loaded) will
  see false.

See [IDroidPopupSimple.lua](IDroidPopupSimple.lua) for a working example.
