#pragma once
#include "Settings.h"

#include "NapiHelper.h"

namespace BrowserApi {
enum class Backend
{
  kOff = 0,
  kTilted = 1,
  kNirnLab = 2,
};
Backend GetBackend();
bool IsVisible();
// Thornswood #3129: [Browser] ShowAtStart, true unless the ini says false.
bool ShowAtStart();
void Register(Napi::Env env, Napi::Object& exports);
}
