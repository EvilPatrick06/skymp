#include "gamemode_events/DyingEvent.h"
#include "ChangeValuesMessage.h"
#include "TestUtils.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

PartOne& GetPartOne();

// Thornswood #1626: the engine asks onDying before a health write kills, and
// a refusal leaves the actor alive with nothing about a death sent.

namespace {
// Stands in for a gamemode's onDying handler: refuses or allows, and may
// write the kept health the way dyingHealth does (the events stack).
class DyingListener : public FakeListener
{
public:
  DyingListener(WorldState& worldState_, bool refuse_,
                std::optional<float> keep_)
    : worldState(worldState_)
    , refuse(refuse_)
    , keep(keep_)
  {
  }

  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    const std::string name = event.GetName();
    names.push_back(name);
    if (name != "onDying") {
      return true;
    }
    arguments = event.GetArgumentsJsonArray();
    if (keep) {
      auto& stack = worldState.currentGameModeEventsStack;
      if (auto dying = dynamic_cast<DyingEvent*>(stack.back())) {
        dying->SetKeptHealthPercentage(*keep);
      }
    }
    return !refuse;
  }

  std::vector<std::string> names;
  std::string arguments;

private:
  WorldState& worldState;
  bool refuse = false;
  std::optional<float> keep;
};

struct Setup
{
  Setup(bool refuse, std::optional<float> keep)
    : p(GetPartOne())
  {
    listener = std::make_shared<DyingListener>(p.worldState, refuse, keep);
    p.AddListener(listener);
    DoConnect(p, 0);
    p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
    p.SetUserActor(0, 0xff000000);
    ac = &p.worldState.GetFormAt<MpActor>(0xff000000);
    ac->SetPercentages({ 0.4f, 1.f, 1.f });
    p.Messages().clear();
    listener->names.clear();
  }

  ~Setup()
  {
    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
    auto& ls = p.worldState.listeners;
    ls.erase(std::remove(ls.begin(), ls.end(), listener), ls.end());
  }

  bool AnyDeathSent() const
  {
    for (auto& m : p.Messages()) {
      if (m.j["t"] == MsgType::DeathStateContainer)
        return true;
    }
    return false;
  }

  PartOne& p;
  std::shared_ptr<DyingListener> listener;
  MpActor* ac = nullptr;
};

void HitToNothing(MpActor& ac)
{
  ActorValues values = ac.GetChangeForm().actorValues;
  values.healthPercentage = 0.f;
  ac.NetSetPercentages(
    values, nullptr,
    std::vector<espm::ActorValue>{ espm::ActorValue::Health });
}
}

TEST_CASE("onDying is asked before a blow kills, and an allowed death goes "
          "ahead",
          "[Dying]")
{
  Setup s(false, std::nullopt);
  HitToNothing(*s.ac);

  REQUIRE(s.ac->IsDead());
  REQUIRE(s.AnyDeathSent());
  auto dying =
    std::find(s.listener->names.begin(), s.listener->names.end(), "onDying");
  auto death =
    std::find(s.listener->names.begin(), s.listener->names.end(), "onDeath");
  REQUIRE(dying != s.listener->names.end());
  REQUIRE(death != s.listener->names.end());
  REQUIRE(dying < death);
  REQUIRE(s.listener->arguments.rfind("[4278190080,0,0.4", 0) == 0);
}

TEST_CASE("A refused death keeps the health the listener wrote, and sends "
          "nothing of a death",
          "[Dying]")
{
  Setup s(true, 0.01f);
  HitToNothing(*s.ac);

  REQUIRE(!s.ac->IsDead());
  REQUIRE(s.ac->GetChangeForm().actorValues.healthPercentage == 0.01f);
  REQUIRE(!s.AnyDeathSent());
  REQUIRE(std::count(s.listener->names.begin(), s.listener->names.end(),
                     "onDeath") == 0);
  REQUIRE(s.p.Messages().size() == 1);
  REQUIRE(s.p.Messages()[0].j["t"] == MsgType::ChangeValues);
  REQUIRE(s.p.Messages()[0].j["data"]["health"] == 0.01f);
}

TEST_CASE("A refused death with no health written keeps the health before "
          "the blow",
          "[Dying]")
{
  Setup s(true, std::nullopt);
  HitToNothing(*s.ac);

  REQUIRE(!s.ac->IsDead());
  REQUIRE(s.ac->GetChangeForm().actorValues.healthPercentage == 0.4f);
  REQUIRE(!s.AnyDeathSent());
}

TEST_CASE("A kept health of nothing or past the most is not taken", "[Dying]")
{
  DyingEvent dying(nullptr, nullptr, 0.4f);
  REQUIRE(!dying.SetKeptHealthPercentage(0.f));
  REQUIRE(!dying.SetKeptHealthPercentage(-0.5f));
  REQUIRE(!dying.SetKeptHealthPercentage(1.5f));
  REQUIRE(dying.GetKeptHealthPercentage() == 0.4f);
  REQUIRE(dying.SetKeptHealthPercentage(1.f));
  REQUIRE(dying.GetKeptHealthPercentage() == 1.f);
}

TEST_CASE("A zero the actor's own machine reports asks too, and a refusal "
          "tells that machine the health kept",
          "[Dying]")
{
  Setup s(true, 0.05f);

  RawMessageData msgData;
  msgData.userId = 0;
  ChangeValuesMessage msg;
  msg.idx = std::nullopt;
  msg.data.health = 0.f;
  s.p.GetActionListener().OnChangeValues(msgData, msg);

  REQUIRE(!s.ac->IsDead());
  REQUIRE(s.ac->GetChangeForm().actorValues.healthPercentage == 0.05f);
  REQUIRE(!s.AnyDeathSent());
  REQUIRE(s.p.Messages().size() == 1);
  REQUIRE(s.p.Messages()[0].j["t"] == MsgType::ChangeValues);
  REQUIRE(s.p.Messages()[0].j["data"]["health"] == 0.05f);
}
