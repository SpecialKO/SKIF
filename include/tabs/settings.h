#pragma once



void
SKIF_UI_Tab_DrawSettings (void);

// Cancels a keybinding dialog of the Settings tab that was not shown this frame (call once per frame, after all UI)
void
SKIF_UI_Settings_CancelHiddenKeybindDialogs (void);