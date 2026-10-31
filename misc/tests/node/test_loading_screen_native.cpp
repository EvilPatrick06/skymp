// Thornswood #3130. LoadingScreen.h, what SkyrimPlatform tells the page when
// the game's Loading Menu opens and closes (BrowserApiTilted::OnLoadingMenu,
// MyLoadHandler::OnLoadEnd) and when it draws the page (DX11RenderHandler).
// Prints the two scripts for test_loading_screen_native.js to run in a
// stand-in window, one per line.
#include <cassert>
#include <iostream>
#include "LoadingScreen.h"

int main()
{
  // Drawn when a plugin made the page visible, and while the game loads.
  assert(!LoadingScreen::ShouldDraw(false, false));
  assert(LoadingScreen::ShouldDraw(true, false));
  assert(LoadingScreen::ShouldDraw(false, true));
  assert(LoadingScreen::ShouldDraw(true, true));

  // The game's own picture is cleared only while the game loads, to the
  // page's dark, opaque (#0b0907, thornswood-loadscreen.js).
  assert(!LoadingScreen::ShouldCover(false));
  assert(LoadingScreen::ShouldCover(true));
  assert(LoadingScreen::kCover[0] == 11.f / 255.f);
  assert(LoadingScreen::kCover[1] == 9.f / 255.f);
  assert(LoadingScreen::kCover[2] == 7.f / 255.f);
  assert(LoadingScreen::kCover[3] == 1.f);

  const std::string open = LoadingScreen::Script(true);
  const std::string closed = LoadingScreen::Script(false);
  assert(open != closed);
  assert(open.find('\n') == std::string::npos);
  assert(closed.find('\n') == std::string::npos);
  std::cout << open << "\n" << closed << "\n";
  return 0;
}
