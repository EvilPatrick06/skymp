#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

/*
  THORNSWOOD PATCH. A key the game holds is let go even while a panel has the
  keyboard (Thornswood #898).

  While the browser has focus, or the game is minimised or behind another
  program, DInputHook::GetDeviceData reads the keyboard's buffered events and
  used to hand the game none of them (*outDataLen = 0). A key that was down
  when that started, Shift and P from typing a name, K for the skills panel,
  the emote wheel's key, then came up inside that time, and its key up was
  thrown away with everything else. The game went on holding the key for the
  rest of the session: its keyboard device makes a button event for every held
  key every frame, which is the 1800 events every five seconds in the front
  plugin's input line, with Tab and the mouse wheel stuck behind it.

  So the buffer is filtered, not emptied. While the game is listening every
  event goes through and this remembers which keys the game has been told are
  down. While it is not, the only events that go through are key ups for keys
  the game holds, so it sees each of those keys come up exactly once and sees
  nothing else: no key down, and no key up for a key it never saw go down.

  The events are DirectInput's DIDEVICEOBJECTDATA (or the shorter DX3 form):
  dwOfs, the DirectX scan code for a keyboard, then dwData, whose 0x80 bit is
  set while the key is down. Both forms begin with those two DWORDs, which is
  all this reads, so the caller passes the stride the game asked for and this
  needs nothing from dinput.h. That is what lets unit\platform_lib_tests run
  it.
*/
class HeldKeyRelease
{
public:
  // Filters count events of stride bytes each in place and returns how many
  // are left, packed at the front. listening is whether the game may hear the
  // keyboard right now.
  size_t Filter(uint8_t* events, size_t stride, size_t count, bool listening);

  // Whether the game has been told this key is down and not yet told it is up.
  bool GameHolds(uint32_t code) const;

  size_t HeldCount() const;

private:
  std::array<bool, 256> held{};
};
