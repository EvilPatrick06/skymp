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

// The page is drawn when a plugin made it visible, and while the game loads
// whatever a plugin said: a plugin cannot run during a load to show it.
inline bool ShouldDraw(bool visible, bool loading)
{
  return visible || loading;
}

}
