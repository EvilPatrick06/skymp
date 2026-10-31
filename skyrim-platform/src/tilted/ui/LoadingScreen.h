#pragma once
#include <string>

// Thornswood #3130: the page is told at once when the game's Loading Menu
// opens and closes, and is drawn while it is open.
//
// Skyrim draws its own loading screen (a model and a hint) for every load,
// and the page only heard of it through the plugins' menuOpen and menuClose,
// which are sent from the next update. No update runs while the game loads,
// so on dev on 22 Oct 2026 both arrived in one burst after the load was over
// (a door at 17:54:39.290, Loading Menu open and close at 40.915 and 40.950).
// The browser is drawn from the swap chain's Present, which the game keeps
// calling through a load (the K panel was seen over a loading screen on
// 20 Sep), so a page told from the menu's own event, on the game's thread,
// can cover the load from its first frame.
//
// Script is what the browser runs: it sets window.skyrimPlatformLoading and
// dispatches the "skyrimPlatform:loading" event with detail.loading. A page
// that loads while the menu is open is told again when it has loaded
// (MyLoadHandler::OnLoadEnd), so it never misses a load in progress.
namespace LoadingScreen {

inline constexpr const char* kEventName = "skyrimPlatform:loading";

inline std::string Script(bool loading)
{
  const std::string v = loading ? "true" : "false";
  return "try{window.skyrimPlatformLoading=" + v +
    ";window.dispatchEvent(new CustomEvent('" + kEventName +
    "',{detail:{loading:" + v + "}}));}catch(e){}";
}

// Thornswood #3130, 31 Oct 2026: the game's own picture is cleared while the
// Loading Menu is open, to the page's dark (#0b0907, the background of
// game/ui/thornswood-loadscreen.js), before the page is drawn on it.
//
// Telling the page is not enough on its own. Script runs in the browser's
// other process, which then lays the page out, paints it on its own threads
// and hands the picture back (OnPaint), and only the Present after that
// draws it. Until then the page's last picture is the one from before the
// load, the see-through HUD, so for those first frames Skyrim's model and
// hint showed through it. Patrick saw them flash at the loads on dev on
// 31 Oct; that session's logs (dev 20261031-143003-dev, 3440x1440) have the
// Loading Menu open at 15:00:12.110 in skyrim-platform.log and the page's
// own "up at" clock at 15:00:12.313, so the page only began to change 203 ms
// after the game's screen was up, and was then painted and drawn. Clearing
// the frame at each Present while the menu is open means nothing of Skyrim's
// loading screen is ever on the screen, however long the page takes: the
// screen is the page's dark until the page's own picture, the loop and the
// hint, is drawn over it.
inline constexpr float kCover[4] = { 11.f / 255.f, 9.f / 255.f, 7.f / 255.f,
                                     1.f };

// The game's picture is cleared only while the game loads. Outside a load
// the page is drawn over the game, never instead of it.
inline bool ShouldCover(bool loading)
{
  return loading;
}

// The page is drawn when a plugin made it visible, and while the game loads
// whatever a plugin said: a plugin cannot run during a load to show it.
inline bool ShouldDraw(bool visible, bool loading)
{
  return visible || loading;
}

}
