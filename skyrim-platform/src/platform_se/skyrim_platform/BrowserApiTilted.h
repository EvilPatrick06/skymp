#pragma once
#include "Settings.h"

#include "NapiHelper.h"

namespace BrowserApiTilted {
Napi::Value SetVisible(const Napi::CallbackInfo& info);
Napi::Value IsVisibleJS(const Napi::CallbackInfo& info);
bool IsVisible();
Napi::Value SetFocused(const Napi::CallbackInfo& info);
// Thornswood #3129: shown and focused the way setVisible(true) and
// setFocused(true) do it, before any plugin can call them.
void ShowAtStart();
Napi::Value IsFocused(const Napi::CallbackInfo& info);
Napi::Value LoadUrl(const Napi::CallbackInfo& info);
Napi::Value ExecuteJavaScript(const Napi::CallbackInfo& info);
// Thornswood #2384: the game's Cursor Menu opened or closed.
void OnCursorMenu(bool opening);
}
