#include "HeldKeyRelease.h"

#include <cstring>

namespace {
constexpr size_t kOfs = 0;
constexpr size_t kData = 4;
constexpr size_t kMinStride = 8;
constexpr uint32_t kDown = 0x80;

uint32_t ReadU32(const uint8_t* p)
{
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}
}

size_t HeldKeyRelease::Filter(uint8_t* events, size_t stride, size_t count,
                              bool listening)
{
  if (!events || stride < kMinStride)
    return listening ? count : 0;

  size_t kept = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t* e = events + i * stride;
    const uint32_t code = ReadU32(e + kOfs);
    const bool down = (ReadU32(e + kData) & kDown) != 0;
    const bool known = code < held.size();

    bool pass;
    if (listening) {
      pass = true;
      if (known)
        held[code] = down;
    } else {
      // Only a key up for a key the game holds.
      pass = known && !down && held[code];
      if (pass)
        held[code] = false;
    }

    if (!pass)
      continue;
    if (kept != i)
      std::memmove(events + kept * stride, e, stride);
    ++kept;
  }
  return kept;
}

bool HeldKeyRelease::GameHolds(uint32_t code) const
{
  return code < held.size() && held[code];
}

size_t HeldKeyRelease::HeldCount() const
{
  size_t n = 0;
  for (bool h : held)
    n += h ? 1 : 0;
  return n;
}
