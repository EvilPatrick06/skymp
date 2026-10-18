#include "ActionListener.h"
#include "OnEquipMessage.h"
#include "gamemode_events/EatItemEvent.h"
#include <algorithm>
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>

PartOne& GetPartOne();

TEST_CASE("Potions restore health", "[Restoration]")
{
  using namespace std::chrono_literals;
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);

  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  auto past = std::chrono::steady_clock::now() - 10s;
  ac.SetLastAttributesPercentagesUpdate(past);
  ac.AddItem(0x3EAE3, 1);
  // 0x3EAE3 restores 100 hp
  ac.SetPercentages({ 0.1f, 0.f, 0.f });

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;

  OnEquipMessage msg;
  msg.baseId = 0x3EAE3;
  p.GetActionListener().OnEquip(rawMsgData, msg);

  std::chrono::duration<float> timeDuration =
    ac.GetLastAttributesPercentagesUpdate() - std::chrono::steady_clock::now();

  REQUIRE(ac.GetChangeForm().actorValues.healthPercentage == 1.0f);
  REQUIRE(timeDuration.count() < 1.f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

namespace {
// Thornswood #1382: stands in for a gamemode's onEatItem handler setting
// eatItemScale, which reaches the event the same way (the events stack).
class ScaleEatListener : public FakeListener
{
public:
  ScaleEatListener(WorldState& worldState_, float scale_)
    : worldState(worldState_)
    , scale(scale_)
  {
  }

  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (std::string(event.GetName()) == "onEatItem") {
      auto& stack = worldState.currentGameModeEventsStack;
      if (!stack.empty()) {
        if (auto eat = dynamic_cast<EatItemEvent*>(stack.back())) {
          eat->SetEffectScale(scale);
        }
      }
    }
    return true;
  }

private:
  WorldState& worldState;
  float scale = 1.f;
};

float DrinkAtScale(float scale)
{
  using namespace std::chrono_literals;
  PartOne& p = GetPartOne();
  auto listener = std::make_shared<ScaleEatListener>(p.worldState, scale);
  p.AddListener(listener);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);

  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  ac.SetLastAttributesPercentagesUpdate(std::chrono::steady_clock::now() -
                                        10s);
  ac.AddItem(0x3EAE3, 1);
  ac.SetPercentages({ 0.1f, 0.f, 0.f });

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  OnEquipMessage msg;
  msg.baseId = 0x3EAE3;
  p.GetActionListener().OnEquip(rawMsgData, msg);

  float health = ac.GetChangeForm().actorValues.healthPercentage;

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
  auto& ls = p.worldState.listeners;
  ls.erase(std::remove(ls.begin(), ls.end(), listener), ls.end());
  return health;
}
}

TEST_CASE("An onEatItem listener scales a potion before it lands",
          "[Restoration]")
{
  // 0x3EAE3 restores 100 hp, which fills an actor at 10%.
  float full = DrinkAtScale(1.f);
  float part = DrinkAtScale(0.1f); // 10 hp: above where it started, short of full
  float none = DrinkAtScale(0.f);

  REQUIRE(full == 1.0f);
  REQUIRE(part > 0.1f);
  REQUIRE(part < full);
  REQUIRE(none == 0.1f);
}

namespace {
// Thornswood #1383: counts onEatItem and keeps the base id it was raised for.
class EatSeenListener : public FakeListener
{
public:
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (std::string(event.GetName()) == "onEatItem") {
      ++seen;
      args = event.GetArgumentsJsonArray();
    }
    return true;
  }

  int seen = 0;
  std::string args;
};
}

TEST_CASE("A brewed bottle the pack holds raises onEatItem and is taken",
          "[Restoration]")
{
  using namespace std::chrono_literals;
  PartOne& p = GetPartOne();
  auto listener = std::make_shared<EatSeenListener>();
  p.AddListener(listener);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);

  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  ac.SetLastAttributesPercentagesUpdate(std::chrono::steady_clock::now() -
                                        10s);
  constexpr uint32_t kBottle = 0xff000123; // a runtime form: a brew
  ac.AddItem(kBottle, 2);
  ac.SetPercentages({ 0.5f, 0.f, 0.f });

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  OnEquipMessage msg;
  msg.baseId = kBottle;
  p.GetActionListener().OnEquip(rawMsgData, msg);

  REQUIRE(listener->seen == 1);
  REQUIRE(listener->args ==
          "[" + std::to_string(0xff000000) + "," + std::to_string(kBottle) +
            "]");
  REQUIRE(ac.GetInventory().GetItemCount(kBottle) == 1);
  // No record, so the server applies nothing itself.
  REQUIRE(ac.GetChangeForm().actorValues.healthPercentage == 0.5f);

  // A runtime id the pack does not hold is not drunk.
  msg.baseId = 0xff000456;
  p.GetActionListener().OnEquip(rawMsgData, msg);
  REQUIRE(listener->seen == 1);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
  auto& ls = p.worldState.listeners;
  ls.erase(std::remove(ls.begin(), ls.end(), listener), ls.end());
}
