#include "BrowserApi.h"
#include "BrowserApiNirnLab.h"
#include "BrowserApiTilted.h"

namespace BrowserApi {
Backend GetBackend()
{
  static std::optional<Backend> g_backend;
  if (g_backend) {
    return *g_backend;
  }

  auto settings = Settings::GetPlatformSettings();

  std::string backendName =
    settings->GetString("Browser", "BackendName", "auto");
  if (backendName == "off") {
    logger::info("browser backend: config value is off, browser disabled");
    g_backend = Backend::kOff;
  } else if (backendName == "auto") {
    logger::info("browser backend: config value is auto. Auto mode not "
                 "implemented yet, falling back to Tilted UI (legacy)");
    g_backend = Backend::kTilted;
  } else if (backendName == "tilted") {
    logger::info(
      "browser backend: config value is tilted, using Tilted UI (legacy)");
    g_backend = Backend::kTilted;
  } else if (backendName == "nirnlab") {
    logger::info(
      "browser backend: config value is nirnlab, using NirnLab UI Platform");
    g_backend = Backend::kNirnLab;
  } else {
    throw std::runtime_error("invalid BackendName in SkyrimPlatform.ini: must "
                             "be auto/tilted/nirnlab");
  }
  return *g_backend;
}

/*
  THORNSWOOD #3129. Whether the browser is shown and focused the moment it
  exists, rather than when a plugin first asks for it.

  The browser loads Data/Platform/UI/index.html when the game's renderer
  starts, but plugins only run from the first tick after the game's data has
  loaded (EventHandler, kDataLoaded), which on Thornswood's load order is
  about fifteen seconds later. A page that needs no plugin to draw, like the
  character choice at the main menu, sat drawn and hidden all that time while
  the game's own menu picture was the only thing on screen. A plugin that
  wants it hidden at the menu still says so on its first tick, as before.
*/
bool ShowAtStart()
{
  static std::optional<bool> g_showAtStart;
  if (!g_showAtStart) {
    auto settings = Settings::GetPlatformSettings();
    g_showAtStart = settings->GetBool("Browser", "ShowAtStart", true);
    logger::info("browser: ShowAtStart is {}", *g_showAtStart);
  }
  return *g_showAtStart;
}

bool IsVisible()
{
  switch (GetBackend()) {
    case Backend::kOff:
      return false;
    case Backend::kTilted:
      return BrowserApiTilted::IsVisible();
    case Backend::kNirnLab:
      return BrowserApiNirnLab::GetInstance().IsVisible();
  }
  // can't reach this place (exception would've thrown), yet the compiler
  // complains
  return false;
}

void Register(Napi::Env env, Napi::Object& exports)
{
  logger::info("registering browser api");

  auto browser = Napi::Object::New(env);
  switch (GetBackend()) {
    case Backend::kOff:
      browser.Set(
        "getBackend",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            auto result = Napi::Object::New(info.Env());
            result.Set("name", Napi::String::New(info.Env(), "off"));
            return result;
          })));
      browser.Set("setVisible",
                  Napi::Function::New(env, [](const Napi::CallbackInfo&) {}));
      browser.Set("isVisible",
                  Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                    return Napi::Boolean::New(info.Env(), false);
                  }));
      browser.Set("setFocused",
                  Napi::Function::New(env, [](const Napi::CallbackInfo&) {}));
      browser.Set("isFocused",
                  Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                    return Napi::Boolean::New(info.Env(), false);
                  }));
      browser.Set("loadUrl",
                  Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                    return Napi::Boolean::New(info.Env(), false);
                  }));
      browser.Set("executeJavaScript",
                  Napi::Function::New(env, [](const Napi::CallbackInfo&) {}));
      // THORNSWOOD. This break was missing, so "off" fell straight through
      // into the tilted bindings and overwrote every stub it had just set.
      // BackendName = off therefore reported itself as tilted and, worse,
      // setFocused still raised ChromeFocus and blanked the keyboard, with no
      // browser on screen to explain why.
      break;
    case Backend::kTilted:
      browser.Set(
        "getBackend",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            auto result = Napi::Object::New(info.Env());
            result.Set("name", Napi::String::New(info.Env(), "tilted"));
            return result;
          })));
      browser.Set(
        "setVisible",
        Napi::Function::New(
          env, NapiHelper::WrapCppExceptions(BrowserApiTilted::SetVisible)));
      browser.Set(
        "isVisible",
        Napi::Function::New(
          env, NapiHelper::WrapCppExceptions(BrowserApiTilted::IsVisibleJS)));
      browser.Set(
        "setFocused",
        Napi::Function::New(
          env, NapiHelper::WrapCppExceptions(BrowserApiTilted::SetFocused)));
      browser.Set(
        "isFocused",
        Napi::Function::New(
          env, NapiHelper::WrapCppExceptions(BrowserApiTilted::IsFocused)));
      browser.Set(
        "loadUrl",
        Napi::Function::New(
          env, NapiHelper::WrapCppExceptions(BrowserApiTilted::LoadUrl)));
      browser.Set("executeJavaScript",
                  Napi::Function::New(env,
                                      NapiHelper::WrapCppExceptions(
                                        BrowserApiTilted::ExecuteJavaScript)));
      break;
    case Backend::kNirnLab:
      browser.Set(
        "getBackend",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            auto result = Napi::Object::New(info.Env());
            result.Set("name", Napi::String::New(info.Env(), "nirnlab"));
            return result;
          })));
      browser.Set(
        "setVisible",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().SetVisible(info);
          })));
      browser.Set(
        "isVisible",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().IsVisible(info);
          })));
      browser.Set(
        "setFocused",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().SetFocused(info);
          })));
      browser.Set(
        "isFocused",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().IsFocused(info);
          })));
      browser.Set(
        "loadUrl",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().LoadUrl(info);
          })));
      browser.Set(
        "executeJavaScript",
        Napi::Function::New(
          env,
          NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) {
            return BrowserApiNirnLab::GetInstance().ExecuteJavaScript(info);
          })));
      break;
  }
  exports.Set("browser", browser);
}
} // namespace BrowserApi
