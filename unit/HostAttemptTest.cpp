#include "ActionListener.h"
#include "HostMessage.h"
#include "HostStartMessage.h"
#include "TestUtils.hpp"

PartOne& GetPartOne();
extern espm::Loader& GetEspmLoader();

TEST_CASE("Hosted creature ownership ends with its host session",
          "[actor-runtime-identity][HostAttempt][espm]")
{
  for (const std::string change : {"disconnect", "detach", "overwrite", "reload", "replace"}) {
    CAPTURE(change);
    PartOne p;
    p.AttachEspm(&GetEspmLoader());
    auto time = std::chrono::system_clock::now();
    p.worldState.SetTimerClock([&] { return time; });
    constexpr uint32_t host = 0xff000abc;
    constexpr uint32_t remoteId = 0xff000abd;
    DoConnect(p, 0);
    p.CreateActor(host, {1, 1, 1}, 0, 0x3c, 42);
    p.SetUserActor(0, host);
    auto creature = std::make_unique<MpActor>(
      LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()},
      p.CreateFormCallbacks(), 0x1e7a4);
    p.worldState.AddForm(std::move(creature), remoteId);
    auto& remote = p.worldState.GetFormAt<MpActor>(remoteId);
    ActionListener listener(p);
    RawMessageData raw;
    raw.userId = 0;
    HostMessage msg;
    msg.remoteId = remoteId;
    listener.OnHostAttempt(raw, msg);
    remote.SetRespawnTime(1);
    remote.Kill();
    REQUIRE(remote.IsRespawning());
    const auto runtime = remote.GetRuntimeIdentity();
    const auto body = remote.GetRuntimeLifeIdentity();
    struct DisconnectListener : FakeListener
    {
      std::function<void()> check;
      bool called = false;
      void OnDisconnect(Networking::UserId) override { called = true; check(); }
    };
    auto onDisconnect = std::make_shared<DisconnectListener>();
    onDisconnect->check = [&] {
      REQUIRE(remote.GetRuntimeIdentity() != runtime);
      REQUIRE(remote.GetRuntimeLifeIdentity() != body);
      REQUIRE(p.worldState.hosters.count(remoteId) == 0);
    };
    p.AddListener(onDisconnect);

    if (change == "disconnect") {
      DoDisconnect(p, 0);
      REQUIRE(onDisconnect->called);
    }
    else if (change == "detach") p.SetUserActor(0, 0);
    else if (change == "overwrite") DoConnect(p, 0);
    else if (change == "replace") p.DestroyActor(host);
    else {
      auto& actor = p.worldState.GetFormAt<MpActor>(host);
      actor.ApplyChangeForm(actor.GetChangeForm());
    }
    // The absent owner's old timer must be inert even before somebody rehosts.
    remote.SetRespawnTime(10);
    Inventory inventory;
    inventory.entries.emplace_back(0xf, 321);
    remote.SetInventory(inventory);
    time += std::chrono::seconds(2);
    p.worldState.Tick();
    REQUIRE(remote.IsDead());
    REQUIRE(remote.GetInventory().ToJson() == inventory.ToJson());
    REQUIRE(remote.GetRuntimeIdentity() != runtime);
    REQUIRE(remote.GetRuntimeLifeIdentity() != body);
    REQUIRE(p.worldState.hosters.count(remoteId) == 0);

    if (change == "disconnect") DoConnect(p, 0);
    if (change == "replace") p.CreateActor(host, {1, 1, 1}, 0, 0x3c, 42);
    p.SetUserActor(0, host);
    // The same human form ID represents a new authority session.
    listener.OnHostAttempt(raw, msg);
    REQUIRE(p.worldState.hosters.at(remoteId) == host);
    REQUIRE(remote.IsRespawning());
    time += std::chrono::seconds(2);
    p.worldState.Tick();
    REQUIRE(remote.IsDead());
    REQUIRE(remote.GetInventory().ToJson() == inventory.ToJson());
    time += std::chrono::seconds(10);
    p.worldState.Tick();
    REQUIRE_FALSE(remote.IsDead());
  }
}

TEST_CASE("Hosting an actor before its first movement records the timestamp",
          "[HostAttempt][PartOne]")
{
  auto& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c);
  auto& remote = p.worldState.GetFormAt<MpActor>(0xff000001);
  auto remoteIdx = remote.GetIdx();

  SECTION("No actor has sent movement yet")
  {
    REQUIRE(p.worldState.lastMovUpdateByIdx.empty());
  }
  SECTION("Only an earlier actor has sent movement")
  {
    REQUIRE(remoteIdx > 0);
    p.worldState.lastMovUpdateByIdx.resize(remoteIdx);
    p.worldState.lastMovUpdateByIdx[0] =
      std::chrono::system_clock::now() - std::chrono::seconds(10);
  }
  SECTION("The remote already has an allocated timestamp slot")
  {
    p.worldState.lastMovUpdateByIdx.resize(remoteIdx + 1);
  }

  const auto timestampsBefore = p.worldState.lastMovUpdateByIdx;
  const auto beforeHosting = std::chrono::system_clock::now();
  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  HostMessage msg;
  msg.remoteId = remote.GetFormId();
  ActionListener listener(p);
  const auto identityBefore = remote.GetRuntimeIdentity();
  const auto bodyBefore = remote.GetRuntimeLifeIdentity();

  listener.OnHostAttempt(rawMsgData, msg);

  REQUIRE(remote.GetRuntimeIdentity() != identityBefore);
  REQUIRE(remote.GetRuntimeLifeIdentity() != bodyBefore);

  REQUIRE(p.worldState.lastMovUpdateByIdx.size() > remoteIdx);
  REQUIRE(p.worldState.lastMovUpdateByIdx[remoteIdx].has_value());
  REQUIRE(*p.worldState.lastMovUpdateByIdx[remoteIdx] >= beforeHosting);
  REQUIRE(p.worldState.hosters.at(remote.GetFormId()) == 0xff000000);
  for (size_t i = 0; i < remoteIdx && i < timestampsBefore.size(); ++i) {
    REQUIRE(p.worldState.lastMovUpdateByIdx[i] == timestampsBefore[i]);
  }
  bool hostStarted = false;
  for (const auto& sent : p.Messages()) {
    if (auto start = dynamic_cast<HostStartMessage*>(sent.message.get())) {
      hostStarted = hostStarted || start->target == remote.GetFormId();
    }
  }
  REQUIRE(hostStarted);

  const auto timestamp = p.worldState.lastMovUpdateByIdx[remoteIdx];
  const auto messageCount = p.Messages().size();
  const auto sameOwner = remote.GetRuntimeIdentity();
  listener.OnHostAttempt(rawMsgData, msg);
  REQUIRE(p.worldState.lastMovUpdateByIdx[remoteIdx] == timestamp);
  REQUIRE(p.Messages().size() == messageCount);
  REQUIRE(remote.GetRuntimeIdentity() == sameOwner);
  p.worldState.lastMovUpdateByIdx[remoteIdx] =
    std::chrono::system_clock::now() - std::chrono::seconds(3);
  listener.OnHostAttempt(rawMsgData, msg);
  REQUIRE(remote.GetRuntimeIdentity() == sameOwner);
  const auto sameBody = remote.GetRuntimeLifeIdentity();

  DoConnect(p, 1);
  p.CreateActor(0xff000002, {0, 0, 0}, 0, 0x3c);
  p.SetUserActor(1, 0xff000002);
  p.worldState.lastMovUpdateByIdx[remoteIdx] =
    std::chrono::system_clock::now() - std::chrono::seconds(3);
  rawMsgData.userId = 1;
  listener.OnHostAttempt(rawMsgData, msg);
  REQUIRE(p.worldState.hosters.at(remote.GetFormId()) == 0xff000002);
  REQUIRE(remote.GetRuntimeIdentity() != sameOwner);
  REQUIRE(remote.GetRuntimeLifeIdentity() != sameBody);
}
