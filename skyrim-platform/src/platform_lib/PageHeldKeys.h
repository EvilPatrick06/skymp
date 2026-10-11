#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

/*
  THORNSWOOD PATCH. A key the page was told is down comes up in the page,
  whoever has the keyboard when it is let go (Thornswood #1920).

  MyInputListener (platform_se/skyrim_platform/main.cpp) hands the page each
  key going down and coming up, with the key's character, while the page has
  the keyboard, and once a key has been held for half a second it hands the
  page that character again on every read of the keyboard. It used to drop
  every key change the moment the page lost the keyboard, key ups included,
  and the repeat never asked who had the keyboard.

  A panel that closes on its own key gives the keyboard back to the game a
  moment after that key goes down: J on the journal, the help and maneuver
  keys, Escape on any panel. A normal tap is let go after that, so the page
  was told the key went down and never that it came up, and the repeat sent
  its character into the page on every read from then on, to be typed into
  the chat box the next time it opened.

  So this is the page's side of HeldKeyRelease. While the page has the
  keyboard every change goes to it, and the keys it is told are down are
  remembered with the moment they went down. While it does not, the only
  change that goes to it is the key up for a key it holds, once, and nothing
  repeats. HeldKeyRelease does the same for the game, so a key that crosses a
  change of focus comes up in whichever of the two saw it go down, and a key
  that went down in one is never repeated or let go in the other.

  Codes are whatever the listener indexes keys by (virtual key codes there),
  and times are whatever clock it reads. Nothing here knows Windows or the
  browser, which is what lets unit\platform_lib_tests run it.
*/
class PageHeldKeys
{
public:
  // A key went down or came up. focused is whether the page has the keyboard
  // right now. Returns whether the page is to be handed this change.
  bool Change(uint32_t code, bool down, bool focused, int64_t now);

  // Whether the page has the keyboard and has held code for longer than
  // delay, so its character is repeated into the page now.
  bool Repeats(uint32_t code, bool focused, int64_t now, int64_t delay) const;

  // Whether the page has been told this key is down and not yet told it is up.
  bool PageHolds(uint32_t code) const;

  size_t HeldCount() const;

private:
  std::array<bool, 256> held{};
  std::array<int64_t, 256> downAt{};
};
