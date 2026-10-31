#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

// THORNSWOOD (Thornswood #1649). The game-free half of createMadePotion
// (InventoryApi.cpp): which requests it builds, and what each effect costs.
//
// A potion somebody brewed is a form the brewing game made while it ran, so
// no other session has it. The server keeps the bottle by its contents
// (Inventory::Made in skymp5-server) under an id worked out from them, in
// the top half of the runtime range, and the client builds a potion with
// that very id when one arrives. This decides whether a request to build
// one is well formed, before the game is touched.
namespace MadePotion {

// Inventory::kMadeIdFirst on the server. A game session gives out runtime
// ids from 0xff000800 upwards, one at a time, so it never reaches these.
constexpr uint32_t kMadeIdFirst = 0xff800000;

// Skyrim's own bottles for brews: every brewed potion looks like a Potion
// of Minor Healing, every brewed poison like a Weak Stamina Poison.
constexpr uint32_t kPotionTemplate = 0x3eadd;
constexpr uint32_t kPoisonTemplate = 0x65a66;

// The most effects anything the lab makes can hold, with room to spare
// (vanilla brews hold at most four).
constexpr size_t kMaxEffects = 8;

inline bool IsMadeId(uint32_t formId)
{
  return formId >= kMadeIdFirst;
}

struct Request
{
  uint32_t formId = 0;
  std::vector<uint32_t> effectIds;
  std::vector<double> magnitudes;
  std::vector<double> areas;
  std::vector<double> durations;
};

// Well formed: an id in the made range, one to kMaxEffects effects, the four
// lists the same length, every effect id set, every number finite, not
// negative, and area and duration whole.
inline bool IsWellFormed(const Request& r)
{
  const size_t n = r.effectIds.size();
  if (!IsMadeId(r.formId) || n == 0 || n > kMaxEffects ||
      r.magnitudes.size() != n || r.areas.size() != n ||
      r.durations.size() != n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (r.effectIds[i] == 0) {
      return false;
    }
    for (double v : { r.magnitudes[i], r.areas[i], r.durations[i] }) {
      if (!std::isfinite(v) || v < 0.0 || v > 4294967295.0) {
        return false;
      }
    }
    if (std::floor(r.areas[i]) != r.areas[i] ||
        std::floor(r.durations[i]) != r.durations[i]) {
      return false;
    }
  }
  return true;
}

// The game's cost of one effect, which is what the bottle's value is made
// of: base cost x magnitude^1.1 x (duration / 10)^1.1, a magnitude under 1
// counting as 1 and a duration of 0 as 10 (UESP, Skyrim:Magic Overview).
inline float Cost(float baseCost, float magnitude, uint32_t duration)
{
  const double m = magnitude < 1.f ? 1.0 : static_cast<double>(magnitude);
  const double d = duration == 0 ? 1.0 : static_cast<double>(duration) / 10.0;
  return static_cast<float>(static_cast<double>(baseCost) *
                            std::pow(m, 1.1) *
                            std::pow(d < 1.0 ? 1.0 : d, 1.1));
}
}
