#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/*
  THORNSWOOD PATCH. Keys the game's own menu opener does not see (Thornswood
  #1016).

  J is Skyrim's Journal key and Thornswood's journal key. The game's
  MenuOpenHandler (inside MenuControls) opens its Journal Menu on the same
  press before any plugin hears it, so the book was drawn for a frame or two
  and then shut by the plugin: a flash. A plugin cannot keep a key from the
  game; this list is how the client does it.

  An entry is a keyboard key (DirectX scan code) and the name the game gives
  that press (its user event, "Journal" for J). While the entry is in, the
  MenuOpenHandler does not see that key as that user event, unless one of the
  entry's except menus is open, in which case the game has the key as before.
  The same key going down as another user event is untouched, so a rebind on
  either side keeps working: somebody who moved the game's journal to another
  key keeps it there.

  Nothing here knows the game: the hook in MenuOpenKeyBlockHook.cpp asks
  ShouldBlock with the game's answer to "is this menu open". That is what lets
  unit\platform_lib_tests test it.
*/
class MenuOpenKeyBlocks
{
public:
  using IsMenuOpen = std::function<bool(const std::string& menuName)>;

  static MenuOpenKeyBlocks& GetSingleton();

  // blocked=false removes the entry for this user event and code.
  // Setting an entry again replaces its except menus.
  void Set(const std::string& userEvent, uint32_t code, bool blocked,
           std::vector<std::string> exceptMenus = {});

  // True when the game's menu opener must not see this press.
  bool ShouldBlock(std::string_view userEvent, bool isKeyboard,
                   uint32_t code, const IsMenuOpen& isMenuOpen) const;

  size_t Size() const;
  void Clear();

private:
  struct Entry
  {
    std::string userEvent;
    uint32_t code = 0;
    std::vector<std::string> exceptMenus;
  };

  mutable std::mutex m;
  std::vector<Entry> entries;
};
