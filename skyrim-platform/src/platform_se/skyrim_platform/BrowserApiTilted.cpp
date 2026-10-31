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

namespace {
void SetFocusedImpl(bool newFocus)
{
  bool& v = CEFUtils::DInputHook::ChromeFocus();
  if (v != newFocus) {
    v = newFocus;

    auto ui = RE::UI::GetSingleton();
    if (!ui) {
      return;
    }

    // Thornswood #2384: the show or hide is queued, not done, so what has
    // been asked for is kept until the game's Cursor Menu event says it
    // happened (CursorFocus.h).
    Queue(g_cursorFocus.OnFocus(newFocus,
                                ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)));
  }
}
}

Napi::Value BrowserApiTilted::SetFocused(const Napi::CallbackInfo& info)
{
  SetFocusedImpl(NapiHelper::ExtractBoolean(info[0], "focused"));
  return info.Env().Undefined();
}

void BrowserApiTilted::ShowAtStart()
{
  // Thornswood: shown, not focused. Focus hands the page the game's mouse,
  // keyboard and Cursor Menu, and nothing here can follow the Cursor Menu
  // before the game's data has loaded (WatchMenus below). Focused from here,
  // the page held the input on 31 Oct 2026 with the Cursor Menu closed: no
  // cursor drawn, no mouse move reaching the page, the game itself deaf,
  // and a click still landing at the middle of the screen, where it picked
  // the character drawn there. Nothing on the page can act before a plugin
  // runs anyway; the plugin that shows the choice focuses it then.
  CEFUtils::DX11RenderHandler::Visible() = true;
}

namespace {
// Thornswood: the Cursor Menu and the Loading Menu reach CursorFocus and the
// page from the moment the game's data has loaded, whether or not a plugin
// listens for menuOpen or menuClose. EventHandler's own MenuOpenCloseEvent
// sink is only added once a plugin subscribes to one of them, so before this
// a show or hide of the cursor could land with CursorFocus never hearing of
// it, and a show it was waiting for stayed "on its way" for good.
class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    if (!event) {
      return RE::BSEventNotifyControl::kContinue;
    }
    if (event->menuName == RE::CursorMenu::MENU_NAME) {
      BrowserApiTilted::OnCursorMenu(event->opening);
    }
    if (event->menuName == RE::LoadingMenu::MENU_NAME) {
      BrowserApiTilted::OnLoadingMenu(event->opening);
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

MenuSink g_menuSink;
bool g_watchingMenus = false;
}

void BrowserApiTilted::WatchMenus()
{
  if (g_watchingMenus) {
    return;
  }
  auto ui = RE::UI::GetSingleton();
  if (!ui) {
    spdlog::error("browser: no UI to watch the Cursor and Loading Menus on");
    return;
  }
  // The same source EventHandler's sink is added to (EventUtils.h).
  RE::BSTEventSource<RE::MenuOpenCloseEvent>* source = ui;
  source->AddEventSink(&g_menuSink);
  g_watchingMenus = true;
  spdlog::info("browser: watching the Cursor and Loading Menus");
}

void BrowserApiTilted::OnCursorMenu(bool opening)
{
  // Thornswood #2384: called from WatchMenus' sink, on the game's thread,
  // when the Cursor Menu itself opens or closes.
  Queue(g_cursorFocus.OnCursorMenu(opening,
                                   CEFUtils::DInputHook::ChromeFocus()));
}

void BrowserApiTilted::OnLoadingMenu(bool opening)
{
  // Thornswood #3130: called from WatchMenus' sink, on the game's thread, as
  // the Loading Menu itself opens or closes, which is before and after the
  // load. The plugins hear of it only after the load (LoadingScreen.h). The page is drawn while it is open, and told.
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
