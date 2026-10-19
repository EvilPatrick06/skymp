#include "TestUtils.hpp"
#include "gamemode_events/SpellCastEvent.h"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <fmt/format.h>
#include <limits>

PartOne& GetPartOne();

// Thornswood #2848: how long a cast's effects hold on the machines nearby is
// the onSpellCast listeners' answer, never the caster's word.

namespace {
// Stands in for a gamemode's onSpellCast handler setting
// spellCastDurationMult, which reaches the event the same way (the events
// stack).
class DurationListener : public FakeListener
{
public:
  DurationListener(WorldState& worldState_, float mult_)
    : worldState(worldState_)
    , mult(mult_)
  {
  }

  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (std::string(event.GetName()) == "onSpellCast") {
      ++heard;
      args = event.GetArgumentsJsonArray();
      auto& stack = worldState.currentGameModeEventsStack;
      if (!stack.empty() && mult > 0.f) {
        if (auto cast = dynamic_cast<SpellCastEvent*>(stack.back())) {
          cast->SetDurationMult(mult);
        }
      }
    }
    return true;
  }

  int heard = 0;
  std::string args;

private:
  WorldState& worldState;
  float mult = 0.f;
};

nlohmann::json CastMessage(std::optional<float> durationMult,
                           bool interruptCast = false)
{
  nlohmann::json data = {
    { "caster", 0x14 },
    { "target", 0 },
    { "spell", 0x12FCD },
    { "isDualCasting", false },
    { "interruptCast", interruptCast },
    { "castingSource", 1 },
    { "aimAngle", 0.f },
    { "aimHeading", 0.f },
    { "actorAnimationVariables",
      { { "booleans", nlohmann::json::array() },
        { "floats", nlohmann::json::array() },
        { "integers", nlohmann::json::array() } } }
  };
  if (durationMult) {
    data["durationMult"] = *durationMult;
  }
  return nlohmann::json{ { "t", MsgType::SpellCast }, { "data", data } };
}

struct Heard
{
  int casts = 0;
  std::string args;
  // What the neighbour got: -1 for no cast at all, 0 for a cast with no
  // durationMult, otherwise its durationMult.
  float neighbourMult = -1.f;
};

// Caster on user 0 with the spell in hand, a neighbour on user 1 who sees
// them, and a listener that answers mult (0: does not touch it).
Heard Cast(float listenerMult, std::optional<float> asked,
           bool interruptCast = false)
{
  PartOne& partOne = GetPartOne();
  auto listener =
    std::make_shared<DurationListener>(partOne.worldState, listenerMult);
  partOne.AddListener(listener);

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  auto& caster = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  GiveStoredAppearance(caster);
  partOne.SetUserActor(0, 0xff000ABC);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000FFF));
  partOne.SetUserActor(1, 0xff000FFF);

  Equipment eq = caster.GetEquipment();
  eq.rightSpell = 0x12FCD;
  caster.SetEquipment(eq);

  partOne.Messages().clear();
  DoMessage(partOne, 0, CastMessage(asked, interruptCast));

  Heard heard;
  heard.casts = listener->heard;
  heard.args = listener->args;
  for (auto& m : partOne.Messages()) {
    if (m.userId != 1 || m.j["t"] != MsgType::SpellCast) {
      continue;
    }
    auto& data = m.j["data"];
    heard.neighbourMult = data.contains("durationMult") &&
        !data["durationMult"].is_null()
      ? data["durationMult"].get<float>()
      : 0.f;
  }

  partOne.DestroyActor(0xff000ABC);
  partOne.DestroyActor(0xff000FFF);
  DoDisconnect(partOne, 0);
  DoDisconnect(partOne, 1);
  auto& ls = partOne.worldState.listeners;
  ls.erase(std::remove(ls.begin(), ls.end(), listener), ls.end());
  return heard;
}
}

TEST_CASE("onSpellCast hears a cast before it goes out, with what was asked",
          "[SpellCastDuration]")
{
  auto heard = Cast(0.f, 2.f);
  REQUIRE(heard.casts == 1);
  REQUIRE(heard.args == fmt::format("[{},{},{}]", 0xff000ABC, 0x12FCD, 2));
}

TEST_CASE("A cast holds for its usual time nearby unless a listener agrees",
          "[SpellCastDuration]")
{
  // Asked for twice as long, and nobody agreed.
  REQUIRE(Cast(0.f, 2.f).neighbourMult == 1.f);
  // Asked for nothing.
  REQUIRE(Cast(0.f, std::nullopt).neighbourMult == 1.f);
}

TEST_CASE("A listener's answer is what the neighbours get",
          "[SpellCastDuration]")
{
  REQUIRE(Cast(2.f, 2.f).neighbourMult == 2.f);
  // The answer, not the ask.
  REQUIRE(Cast(2.f, 1.f).neighbourMult == 2.f);
  // Held to ten times.
  REQUIRE(Cast(50.f, 1.f).neighbourMult == 10.f);
}

TEST_CASE("An interrupted cast asks nobody and carries no duration",
          "[SpellCastDuration]")
{
  auto heard = Cast(2.f, 2.f, true);
  REQUIRE(heard.casts == 0);
  REQUIRE(heard.neighbourMult == 0.f);
}

TEST_CASE("A duration answer that is not a number is the game's own",
          "[SpellCastDuration]")
{
  SpellCastEvent event(nullptr, 0x12FCD, 2.f);
  REQUIRE(event.GetDurationMult() == 1.f);
  event.SetDurationMult(std::numeric_limits<float>::quiet_NaN());
  REQUIRE(event.GetDurationMult() == 1.f);
  event.SetDurationMult(-3.f);
  REQUIRE(event.GetDurationMult() == 1.f);
  event.SetDurationMult(0.5f);
  REQUIRE(event.GetDurationMult() == 0.5f);
}
