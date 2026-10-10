#include "TestUtils.hpp"
#include "WorldState.h"
#include "gamemode_events/GameModeEvent.h"

extern espm::Loader& GetEspmLoader();

TEST_CASE("Runtime actor identity is not reused by another instance",
          "[actor-runtime-identity]")
{
  MpActor first(LocationalData(), FormCallbacks::DoNothing());
  MpActor second(LocationalData(), FormCallbacks::DoNothing());
  REQUIRE(first.GetRuntimeIdentity() != 0);
  REQUIRE(first.GetRuntimeIdentity() != second.GetRuntimeIdentity());
  const auto identity = first.GetRuntimeIdentity();
  REQUIRE(first.GetRuntimeIdentity() == identity);
}

TEST_CASE("Runtime actor identity follows connection ownership",
          "[actor-runtime-identity]")
{
  MpActor actor(LocationalData(), FormCallbacks::DoNothing());
  ActorsMap map;
  const auto initial = actor.GetRuntimeIdentity();
  map.Set(0, &actor);
  const auto connected = actor.GetRuntimeIdentity();
  REQUIRE(connected != initial);
  map.Erase(static_cast<Networking::UserId>(0));
  const auto detached = actor.GetRuntimeIdentity();
  REQUIRE(detached != connected);
  map.Set(0, &actor);
  const auto reconnected = actor.GetRuntimeIdentity();
  REQUIRE(reconnected != detached);
  REQUIRE(reconnected != connected);
  map.Set(1, &actor);
  REQUIRE(actor.GetRuntimeIdentity() != reconnected);
}

TEST_CASE("A replaced connection cannot retain the old actor identity",
          "[actor-runtime-identity]")
{
  ServerState server;
  MpActor actor(LocationalData(), FormCallbacks::DoNothing());
  server.Connect(0, "first");
  server.actorsMap.Set(0, &actor);
  const auto before = actor.GetRuntimeIdentity();
  server.Connect(0, "second");
  REQUIRE(actor.GetRuntimeIdentity() != before);
  REQUIRE(server.ActorByUser(0) == nullptr);
}

TEST_CASE("Human respawn and reload cannot restore runtime identity",
          "[actor-runtime-identity][espm]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  constexpr uint32_t id = 0xff000abc;
  server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
  auto& actor = server.worldState.GetFormAt<MpActor>(id);
  const auto life = actor.GetDynamicFields().GetValueDump("_skympNpcLifeGeneration");
  const auto before = actor.GetRuntimeIdentity();
  actor.Kill();
  const auto dead = actor.GetRuntimeIdentity();
  REQUIRE(dead != before);
  actor.Respawn(false);
  const auto alive = actor.GetRuntimeIdentity();
  REQUIRE(alive != dead);
  REQUIRE(alive != before);
  REQUIRE(actor.GetDynamicFields().GetValueDump("_skympNpcLifeGeneration") == life);
  const auto saved = actor.GetChangeForm();
  actor.ApplyChangeForm(saved);
  REQUIRE(actor.GetRuntimeIdentity() != alive);
  const auto reloaded = actor.GetRuntimeIdentity();
  actor.SetInventory(actor.GetInventory());
  REQUIRE(actor.GetRuntimeIdentity() == reloaded);
  server.DestroyActor(id);
  server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
  REQUIRE(server.worldState.GetFormAt<MpActor>(id).GetRuntimeIdentity() != reloaded);
}

TEST_CASE("Disconnect invalidates identity before gamemode listeners run",
          "[actor-runtime-identity][espm]")
{
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  DoConnect(server, 0);
  constexpr uint32_t id = 0xff000abc;
  server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
  server.SetUserActor(0, id);
  auto& actor = server.worldState.GetFormAt<MpActor>(id);
  struct Listener : FakeListener
  {
    MpActor* actor = nullptr;
    uint64_t expected = 0;
    bool called = false;
    void OnDisconnect(Networking::UserId) override
    {
      called = true;
      REQUIRE(actor->GetRuntimeIdentity() != expected);
    }
  };
  auto listener = std::make_shared<Listener>();
  listener->actor = &actor;
  listener->expected = actor.GetRuntimeIdentity();
  server.AddListener(listener);
  DoDisconnect(server, 0);
  REQUIRE(listener->called);
}

TEST_CASE("Lifecycle handlers cannot continue into another ownership session",
          "[actor-runtime-identity][espm]")
{
  for (const std::string event : {"onDeath", "onRespawn"}) {
    for (const bool recreate : {false, true}) {
      CAPTURE(event, recreate);
      PartOne server;
      server.AttachEspm(&GetEspmLoader());
      DoConnect(server, 0);
      constexpr uint32_t id = 0xff000abc;
      server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
      server.SetUserActor(0, id);
      auto& actor = server.worldState.GetFormAt<MpActor>(id);
      if (event == "onRespawn") actor.Kill();
      struct Listener : FakeListener
      {
        std::string event;
        std::function<void()> callback;
        bool called = false;
        bool OnMpApiEvent(const GameModeEvent& current) override
        {
          if (!called && event == current.GetName()) {
            called = true;
            callback();
          }
          return true;
        }
      };
      auto listener = std::make_shared<Listener>();
      listener->event = event;
      listener->callback = [&] {
        server.SetUserActor(0, 0);
        if (recreate) {
          server.DestroyActor(id);
          server.CreateActor(id, {1, 1, 1}, 0, 0x3c, 42);
        }
        server.SetUserActor(0, id);
      };
      server.AddListener(listener);
      if (event == "onDeath") actor.Kill();
      else actor.Respawn(false);
      REQUIRE(listener->called);
      auto& current = server.worldState.GetFormAt<MpActor>(id);
      REQUIRE(current.IsDead() == !recreate);
      if (!recreate) REQUIRE(current.IsRespawning());
    }
  }
}

TEST_CASE("Old respawn timers cannot reset a new life or restored session",
          "[actor-runtime-identity][espm]")
{
  for (const std::string change : {"resurrect", "reconnect", "reload"}) {
    CAPTURE(change);
    PartOne server;
    server.AttachEspm(&GetEspmLoader());
    auto time = std::chrono::system_clock::now();
    server.worldState.SetTimerClock([&] { return time; });
    constexpr uint32_t id = 0xff000abc;
    auto wolf = std::make_unique<MpActor>(
      LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()},
      server.CreateFormCallbacks(), 0xe1672);
    server.worldState.AddForm(std::move(wolf), id);
    auto& actor = server.worldState.GetFormAt<MpActor>(id);
    auto saved = actor.GetChangeForm();
    saved.profileId = 42;
    actor.ApplyChangeForm(saved);
    DoConnect(server, 0);
    server.SetUserActor(0, id);
    actor.SetInventory(Inventory());
    actor.SetRespawnTime(1);
    actor.Kill();
    REQUIRE_FALSE(actor.GetInventory().entries.empty());
    REQUIRE(actor.IsRespawning());
    if (change == "resurrect") actor.Respawn(false);
    else if (change == "reconnect") {
      server.SetUserActor(0, 0);
      actor.SetRespawnTime(10);
      server.SetUserActor(0, id);
    } else {
      saved = actor.GetChangeForm();
      saved.spawnDelay = 10;
      actor.ApplyChangeForm(saved);
    }
    Inventory inventory;
    inventory.entries.emplace_back(0xf, 321);
    actor.SetInventory(inventory);
    time += std::chrono::seconds(2);
    server.worldState.Tick();
    REQUIRE(actor.GetInventory().ToJson() == inventory.ToJson());
    REQUIRE(actor.IsDead() == (change != "resurrect"));
    if (change != "resurrect") {
      time += std::chrono::seconds(10);
      server.worldState.Tick();
      REQUIRE_FALSE(actor.IsDead());
    }
  }
}
