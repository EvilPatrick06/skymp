#pragma once
#include "Settings.h"

#include "NapiHelper.h"

namespace BrowserApiTilted {
Napi::Value SetVisible(const Napi::CallbackInfo& info);
Napi::Value IsVisibleJS(const Napi::CallbackInfo& info);
bool IsVisible();
Napi::Value SetFocused(const Napi::CallbackInfo& info);
// Thornswood #3129: shown the way setVisible(true) does it, before any plugin
// can call it. Not focused: the plugin that shows the choice focuses it.
void ShowAtStart();
// From kDataLoaded: the Cursor Menu and the Loading Menu reach OnCursorMenu
// and OnLoadingMenu whether or not a plugin listens for menus.
void WatchMenus();
Napi::Value IsFocused(const Napi::CallbackInfo& info);
Napi::Value LoadUrl(const Napi::CallbackInfo& info);
Napi::Value ExecuteJavaScript(const Napi::CallbackInfo& info);
// Thornswood #2384: the game's Cursor Menu opened or closed (WatchMenus).
void OnCursorMenu(bool opening);
// Thornswood #3130: the game's Loading Menu opened or closed (WatchMenus).
void OnLoadingMenu(bool opening);
}
