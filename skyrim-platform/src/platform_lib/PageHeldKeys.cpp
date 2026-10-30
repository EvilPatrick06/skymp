#include "PageHeldKeys.h"

bool PageHeldKeys::Change(uint32_t code, bool down, bool focused, int64_t now)
{
  const bool known = code < held.size();

  if (focused) {
    if (known) {
      held[code] = down;
      downAt[code] = down ? now : 0;
    }
    return true;
  }

  // Only a key up for a key the page holds.
  if (!known || down || !held[code])
    return false;
  held[code] = false;
  downAt[code] = 0;
  return true;
}

bool PageHeldKeys::Repeats(uint32_t code, bool focused, int64_t now,
                           int64_t delay) const
{
  return focused && code < held.size() && held[code] &&
    now - downAt[code] > delay;
}

bool PageHeldKeys::PageHolds(uint32_t code) const
{
  return code < held.size() && held[code];
}

size_t PageHeldKeys::HeldCount() const
{
  size_t n = 0;
  for (bool h : held)
    n += h ? 1 : 0;
  return n;
}
