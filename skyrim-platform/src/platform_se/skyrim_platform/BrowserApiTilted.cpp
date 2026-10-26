#include <NirnLabUIPlatformAPI/API.h>

#include "BrowserApiTilted.h"
#include "CursorFocus.h"
#include "NullPointerException.h"
#include "TPOverlayService.h"
#include <ui/LoadingScreen.h>

namespace {

// Thornswood #2384: replaces g_cursorIsOpenByFocus, which was set when a show
// was queued rather than when the menu opened, and was thread_local while the
// Cursor Menu event arrives on the game's thread.
CursorFocus g_cursorFocus;

void Queue(CursorFocus::Msg msg)
{
  if (msg == CursorFocus::Msg::None) {
    return;
  }
  auto msgQ = RE::UIMessageQueue::GetSingleton();
  if (!msgQ) {
    return;
  }
  msgQ->AddMessage(RE::CursorMenu::MENU_NAME,
                   msg == CursorFocus::Msg::Show
                     ? RE::UI_MESSAGE_TYPE::kShow
                     : RE::UI_MESSAGE_TYPE::kHide,
                   NULL);
}

inline CEFUtils::MyChromiumApp& GetApp()
{
  auto overlayService = OverlayService::GetInstance();
  if (!overlayService) {
    throw NullPointerException("overlayService");
  }

  auto app = overlayService->GetMyChromiumApp();
  if (!app) {
    throw NullPointerException("app");
  }

  return *app;
}
}

Napi::Value BrowserApiTilted::SetVisible(const Napi::CallbackInfo& info)
{
  bool& v = CEFUtils::DX11RenderHandler::Visible();
  v = NapiHelper::ExtractBoolean(info[0], "visible");
  return info.Env().Undefined();
}

Napi::Value BrowserApiTilted::IsVisibleJS(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), IsVisible());
}

bool BrowserApiTilted::IsVisible()
{
  return CEFUtils::DX11RenderHandler::Visible();
}

Napi::Value BrowserApiTilted::SetFocused(const Napi::CallbackInfo& info)
{
  bool& v = CEFUtils::DInputHook::ChromeFocus();
  bool newFocus = NapiHelper::ExtractBoolean(info[0], "focused");
  if (v != newFocus) {
    v = newFocus;

    auto ui = RE::UI::GetSingleton();
    if (!ui) {
      return info.Env().Undefined();
    }

    // Thornswood #2384: the show or hide is queued, not done, so what has
    // been asked for is kept until the game's Cursor Menu event says it
    // happened (CursorFocus.h).
    Queue(g_cursorFocus.OnFocus(newFocus,
                                ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)));
  }
  return info.Env().Undefined();
}

void BrowserApiTilted::OnCursorMenu(bool opening)
{
  // Thornswood #2384: called from EventHandler's MenuOpenCloseEvent sink, on
  // the game's thread, when the Cursor Menu itself opens or closes.
  Queue(g_cursorFocus.OnCursorMenu(opening,
                                   CEFUtils::DInputHook::ChromeFocus()));
}

void BrowserApiTilted::OnLoadingMenu(bool opening)
{
  // Thornswood #3130: called from EventHandler's MenuOpenCloseEvent sink, on
  // the game's thread, as the Loading Menu itself opens or closes, which is
  // before and after the load. The plugins hear of it only after the load
  // (LoadingScreen.h). The page is drawn while it is open, and told.
  CEFUtils::DX11RenderHandler::Loading() = opening;
  spdlog::info(opening ? "Loading Menu opened, the page is drawn over it"
                       : "Loading Menu closed");
  auto overlayService = OverlayService::GetInstance();
  if (!overlayService) {
    return;
  }
  auto app = overlayService->GetMyChromiumApp();
  if (!app) {
    return;
  }
  app->ExecuteJavaScript(LoadingScreen::Script(opening));
}

Napi::Value BrowserApiTilted::IsFocused(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), CEFUtils::DInputHook::ChromeFocus());
}

Napi::Value BrowserApiTilted::LoadUrl(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "url");
  return Napi::Boolean::New(info.Env(), GetApp().LoadUrl(str.data()));
}

Napi::Value BrowserApiTilted::ExecuteJavaScript(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "src");
  GetApp().ExecuteJavaScript(str);
  return info.Env().Undefined();
}
