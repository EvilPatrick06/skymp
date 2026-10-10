#include "TestUtils.hpp"
#include "WorldState.h"

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
