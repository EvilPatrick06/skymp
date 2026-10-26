#include <DX11RenderHandler.h>
#include <LoadingScreen.h>
#include <MyLoadHandler.h>

namespace CEFUtils {
void MyLoadHandler::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                TransitionType transition_type)
{
  if (frame->IsMain() && transition_type == TT_EXPLICIT) {
    m_ready = false;
  }
}

void MyLoadHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame, int httpStatusCode)
{
  if (frame->IsMain()) {
    m_ready = true;
    // Thornswood #3130: a page that loaded while the game is loading has not
    // heard the Loading Menu open, so it is told now.
    if (DX11RenderHandler::Loading()) {
      frame->ExecuteJavaScript(LoadingScreen::Script(true), frame->GetURL(),
                               0);
    }
  }
}

bool MyLoadHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefLoadHandler::ErrorCode errorCode,
                                const CefString& failedUrl,
                                CefString& errorText)
{
  return false;
}
}
