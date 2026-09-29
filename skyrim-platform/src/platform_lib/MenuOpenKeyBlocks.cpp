#include "MenuOpenKeyBlocks.h"
#include <algorithm>

// THORNSWOOD PATCH (Thornswood #1016). See MenuOpenKeyBlocks.h.

MenuOpenKeyBlocks& MenuOpenKeyBlocks::GetSingleton()
{
  static MenuOpenKeyBlocks instance;
  return instance;
}

void MenuOpenKeyBlocks::Set(const std::string& userEvent, uint32_t code,
                            bool blocked, std::vector<std::string> exceptMenus)
{
  std::lock_guard l(m);
  auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) {
    return e.userEvent == userEvent && e.code == code;
  });
  if (!blocked) {
    if (it != entries.end()) {
      entries.erase(it);
    }
    return;
  }
  if (it != entries.end()) {
    it->exceptMenus = std::move(exceptMenus);
    return;
  }
  entries.push_back({ userEvent, code, std::move(exceptMenus) });
}

bool MenuOpenKeyBlocks::ShouldBlock(std::string_view userEvent,
                                    bool isKeyboard, uint32_t code,
                                    const IsMenuOpen& isMenuOpen) const
{
  if (!isKeyboard || userEvent.empty()) {
    return false;
  }
  std::lock_guard l(m);
  for (const Entry& e : entries) {
    if (e.code != code || e.userEvent != userEvent) {
      continue;
    }
    for (const std::string& menu : e.exceptMenus) {
      if (isMenuOpen && isMenuOpen(menu)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

size_t MenuOpenKeyBlocks::Size() const
{
  std::lock_guard l(m);
  return entries.size();
}

void MenuOpenKeyBlocks::Clear()
{
  std::lock_guard l(m);
  entries.clear();
}
