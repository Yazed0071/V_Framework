#pragma once

#include <cstdint>


// HUD popup with literal body text.
//   titleLabel : LangId label name (DLL hashes via StrCode64).
//   body       : LITERAL body text (passed straight to SetPopupText).
//   popupType  : button layout (1..6). Default 1.
//                  1 = no buttons
//                  2 = broken — silently folded to 1
//                  3 = OK only
//                  4 = Cancel only
//                  5 = OK and Cancel
//                  6 = OK and Cancel (synonym of 5)
//                Bad values (0, 2, >=7) are folded to 1.
bool Show_HudPopup(const char* titleLabel,
                   const char* body,
                   std::uint32_t popupType);


// HUD popup with both title and body resolved via lang manager.
//   titleLabel : LangId label name for title.
//   bodyLabel  : LangId label name for body.
//   popupType  : 1..6, default 1.
// Both labels hashed via Fox StrCode64. Body label is resolved through
// the cached lang manager (requires the iDroid hook to have captured it
// — i.e. player has opened the iDroid menu at least once).
bool Show_HudPopupLangId(const char* titleLabel,
                         const char* bodyLabel,
                         std::uint32_t popupType);


// HUD error popup — numeric error code, no custom text.
bool Show_HudErrorPopup(std::uint32_t errorParam,
                        std::uint32_t popupType);
