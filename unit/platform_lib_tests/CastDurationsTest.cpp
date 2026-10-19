#include <catch2/catch_all.hpp>

#include "CastDurations.h"
#include <limits>
#include <vector>

// THORNSWOOD PATCH (Thornswood #2848): how long the effects of each cast hold
// on this machine. 0x14 is the person's own character.

namespace {
constexpr uint32_t kPlayer = 0x14;
constexpr uint32_t kOther = 0xff000a2c;
constexpr uint32_t kOakflesh = 0x0005ad5c;
constexpr uint32_t kCandlelight = 0x00043324;
}

TEST_CASE("A caster with no rule holds for the game's own time",
          "[CastDurations]")
{
  CastDurations d;
  for (int i = 0; i < 10; ++i) {
    REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 1.f);
  }
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 1, 60.f) == 1.f);
  REQUIRE_FALSE(d.HasRule(kPlayer));
}

TEST_CASE("Every fifth cast that holds holds twice as long",
          "[CastDurations]")
{
  CastDurations d;
  d.SetEvery(kPlayer, 5, 2.f);
  std::vector<float> got;
  for (int i = 0; i < 10; ++i) {
    got.push_back(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr));
  }
  REQUIRE(got ==
          std::vector<float>{ 1, 1, 1, 1, 2, 1, 1, 1, 1, 2 });
}

TEST_CASE("A cast that does not hold is not counted", "[CastDurations]")
{
  CastDurations d;
  d.SetEvery(kPlayer, 5, 2.f);
  for (int i = 0; i < 4; ++i) {
    REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 1.f);
  }
  // A fireball between them counts for nothing and holds for nothing more.
  REQUIRE(d.OnOwnCast(kPlayer, 0x1c789, false, nullptr) == 1.f);
  REQUIRE(d.OnOwnCast(kPlayer, kCandlelight, true, nullptr) == 2.f);
}

TEST_CASE("An effect takes what the latest cast of its spell holds for",
          "[CastDurations]")
{
  CastDurations d;
  d.SetEvery(kPlayer, 2, 2.f);
  d.OnOwnCast(kPlayer, kOakflesh, true, nullptr);
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 1, 60.f) == 1.f);
  d.OnOwnCast(kPlayer, kOakflesh, true, nullptr);
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 2, 60.f) == 2.f);
  // Another spell of the same caster keeps its own.
  REQUIRE(d.OnEffectStart(kPlayer, kCandlelight, kPlayer, 3, 60.f) == 1.f);
}

TEST_CASE("A played cast takes the server's answer, for that caster only",
          "[CastDurations]")
{
  CastDurations d;
  d.OnPlayedCast(kOther, kOakflesh, 2.f);
  REQUIRE(d.OnEffectStart(kOther, kOakflesh, kPlayer, 1, 30.f) == 2.f);
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 2, 30.f) == 1.f);
  d.OnPlayedCast(kOther, kOakflesh, 1.f);
  REQUIRE(d.OnEffectStart(kOther, kOakflesh, kPlayer, 3, 30.f) == 1.f);
  // A caster that is not this machine's own raises no count.
  REQUIRE_FALSE(d.HasRule(kOther));
}

TEST_CASE("Effects that began before the cast event are handed back once",
          "[CastDurations]")
{
  CastDurations d;
  d.SetEvery(kPlayer, 1, 2.f);
  // The game starts a spell on yourself, then raises the cast.
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 7, 60.f) == 1.f);
  std::vector<CastDurations::Started> started;
  REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, &started) == 2.f);
  REQUIRE(started.size() == 1);
  REQUIRE(started[0].targetFormId == kPlayer);
  REQUIRE(started[0].uniqueId == 7);
  REQUIRE(started[0].gameDuration == 60.f);
  // Handed back once: the next cast hands back nothing.
  d.OnOwnCast(kPlayer, kOakflesh, true, &started);
  REQUIRE(started.empty());
}

TEST_CASE("Taking the rule off forgets its count and its casts",
          "[CastDurations]")
{
  CastDurations d;
  d.SetEvery(kPlayer, 2, 2.f);
  d.OnOwnCast(kPlayer, kOakflesh, true, nullptr);
  REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 2.f);
  d.SetEvery(kPlayer, 0, 1.f);
  REQUIRE_FALSE(d.HasRule(kPlayer));
  REQUIRE(d.OnEffectStart(kPlayer, kOakflesh, kPlayer, 1, 60.f) == 1.f);
  d.SetEvery(kPlayer, 2, 2.f);
  REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 1.f);
  REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 2.f);
}

TEST_CASE("A multiplier that is not a number is the game's own",
          "[CastDurations]")
{
  CastDurations d;
  d.OnPlayedCast(kOther, kOakflesh, -1.f);
  REQUIRE(d.OnEffectStart(kOther, kOakflesh, kPlayer, 1, 30.f) == 1.f);
  d.SetEvery(kPlayer, 1, std::numeric_limits<float>::infinity());
  REQUIRE(d.OnOwnCast(kPlayer, kOakflesh, true, nullptr) == 1.f);
}
