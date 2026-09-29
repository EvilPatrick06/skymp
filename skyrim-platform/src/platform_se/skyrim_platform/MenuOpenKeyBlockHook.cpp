#include "MenuOpenKeyBlockHook.h"
#include "MenuOpenKeyBlocks.h"

/*
  THORNSWOOD PATCH. The game's menu opener does not see a key on the list
  (Thornswood #1016).

  MenuControls hands every input event to each of its handlers, asking
  CanProcess first and calling ProcessButton only on a yes. MenuOpenHandler is
  the one that opens the Journal Menu on "Journal" (and the other menus a key
  opens straight from the world). Both of its entries are wrapped in its
  vtable: a press on the list is a no to CanProcess and not handled by
  ProcessButton, so the game never asks for the menu and nothing is drawn.
  Everything else goes to the game's own functions unchanged, and the handler
  keeps its place among the others, unlike the screenshot handler's wrapper in
  DevApi.cpp, which is added again at the end.

  Skyrim Platform's own key events do not come through MenuControls, so
  plugins still hear the key in buttonEvent.
*/
namespace {
bool Blocked(RE::InputEvent* e)
{
  if (!e || e->eventType != RE::INPUT_EVENT_TYPE::kButton) {
    return false;
  }
  auto* button = e->AsButtonEvent();
  if (!button) {
    return false;
  }
  const char* userEvent = button->QUserEvent().c_str();
  return MenuOpenKeyBlocks::GetSingleton().ShouldBlock(
    userEvent ? userEvent : "",
    button->GetDevice() == RE::INPUT_DEVICE::kKeyboard, button->GetIDCode(),
    [](const std::string& menuName) {
      auto ui = RE::UI::GetSingleton();
      return ui && ui->IsMenuOpen(menuName);
    });
}

bool CanProcess(RE::MenuOpenHandler* self, RE::InputEvent* e);
bool ProcessButton(RE::MenuOpenHandler* self, RE::ButtonEvent* e);

decltype(&CanProcess) _CanProcess = nullptr;
decltype(&ProcessButton) _ProcessButton = nullptr;

bool CanProcess(RE::MenuOpenHandler* self, RE::InputEvent* e)
{
  if (Blocked(e)) {
    return false;
  }
  return _CanProcess(self, e);
}

bool ProcessButton(RE::MenuOpenHandler* self, RE::ButtonEvent* e)
{
  if (Blocked(e)) {
    return false;
  }
  return _ProcessButton(self, e);
}
}

void MenuOpenKeyBlockHook::Install()
{
  REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MenuOpenHandler[0] };
  _CanProcess = reinterpret_cast<decltype(_CanProcess)>(
    vtbl.write_vfunc(0x1, CanProcess));
  _ProcessButton = reinterpret_cast<decltype(_ProcessButton)>(
    vtbl.write_vfunc(0x5, ProcessButton));
  logger::info("MenuOpenHandler hooked for the menu open key list");
}
