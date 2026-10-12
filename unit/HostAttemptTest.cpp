#include "ActionListener.h"
#include "ChangeValuesMessage.h"
#include "gamemode_events/GameModeEvent.h"
#include "HostMessage.h"
#include "HostStartMessage.h"
#include "HitMessage.h"
#include "TestUtils.hpp"

PartOne& GetPartOne();
extern espm::Loader& GetEspmLoader();

namespace {
struct DeathPublicationListener : FakeListener {
  size_t deaths = 0;
  std::string arguments;
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (std::string(event.GetName()) == "onDeath") {
      ++deaths;
      arguments = event.GetArgumentsJsonArray();
    }
    return true;
  }
};
}

TEST_CASE("Hosted creature ownership ends with its host session",
          "[actor-runtime-identity][HostAttempt][espm]")
{
  for (const std::string change : {"disconnect", "detach", "overwrite", "reload", "replace", "creature-reload", "incoming"}) {
    DYNAMIC_SECTION(change) {
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
    else if (change == "creature-reload") {
      p.SetUserActor(0, 0);
      remote.ApplyChangeForm(remote.GetChangeForm());
    }
    else if (change == "incoming") {
      constexpr uint32_t incoming=0xff000abe,probe=0xff000abf;
      p.CreateActor(incoming,{1,1,1},0,0x3c,42);
      bool called=false;
      auto callbacks=p.CreateFormCallbacks();
      callbacks.unsubscribe=[&](auto,auto){
        called=true;
        CHECK(remote.GetRuntimeIdentity()!=runtime);
        CHECK(remote.GetRuntimeLifeIdentity()!=body);
        CHECK(p.worldState.hosters.count(remoteId)==0);
      };
      auto emitter=std::make_unique<MpActor>(LocationalData{{1,1,1},{},FormDesc::Tamriel()},callbacks,0x1e7a4);
      p.worldState.AddForm(std::move(emitter),probe);
      MpObjectReference::Subscribe(&p.worldState.GetFormAt<MpActor>(probe),&p.worldState.GetFormAt<MpActor>(incoming));
      p.SetUserActor(0,incoming);
      REQUIRE(called);
    }
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
}

TEST_CASE("A running hosted return stops when its inventory callback releases the host",
          "[actor-runtime-identity][HostAttempt][espm]")
{
  const bool reload=GENERATE(false,true);
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  auto time=std::chrono::system_clock::now();
  p.worldState.SetTimerClock([&]{return time;});
  constexpr uint32_t host=0xff000abc,remoteId=0xff000abd;
  bool revoke=false,called=false;
  auto callbacks=p.CreateFormCallbacks();
  const auto send=callbacks.sendToUserDeferred;
  callbacks.sendToUserDeferred=[&](auto actor,auto& message,auto reliable,auto channel,auto overwrite){
    if(revoke && !called){
      called=true;
      if(reload){auto restored=actor->GetChangeForm();restored.baseContainerAdded=false;actor->ApplyChangeForm(restored);}
      else p.SetUserActor(0,0);
    }
    send(actor,message,reliable,channel,overwrite);
  };
  DoConnect(p,0);
  auto human=std::make_unique<MpActor>(LocationalData{{1,1,1},{},FormDesc::Tamriel()},callbacks,0x7);
  p.worldState.AddForm(std::move(human),host);
  p.worldState.GetFormAt<MpActor>(host).RegisterProfileId(42);
  p.SetUserActor(0,host);
  auto creature=std::make_unique<MpActor>(LocationalData{{1,1,1},{},FormDesc::Tamriel()},callbacks,0x3053f);
  p.worldState.AddForm(std::move(creature),remoteId);
  auto& remote=p.worldState.GetFormAt<MpActor>(remoteId);
  ActionListener listener(p);RawMessageData raw;raw.userId=0;HostMessage msg;msg.remoteId=remoteId;
  listener.OnHostAttempt(raw,msg);
  remote.SetRespawnTime(1);remote.Kill();
  const auto runtime=remote.GetRuntimeIdentity();
  revoke=true;time+=std::chrono::seconds(2);p.worldState.Tick();
  REQUIRE(called);
  REQUIRE(remote.GetRuntimeIdentity()!=runtime);
  if(reload){REQUIRE_FALSE(remote.GetChangeForm().baseContainerAdded);return;}
  REQUIRE(remote.IsDead());
  REQUIRE_FALSE(remote.IsRespawning());
  REQUIRE(p.worldState.hosters.count(remoteId)==0);
}

TEST_CASE("A hosted return cannot revive after synchronous death publication changes its owner",
          "[actor-runtime-identity][HostAttempt][espm]")
{
  const bool reload = GENERATE(false, true);
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  auto time = std::chrono::system_clock::now();
  p.worldState.SetTimerClock([&] { return time; });
  constexpr uint32_t host = 0xff000abc, remoteId = 0xff000abd;
  bool revoke = false, called = false;
  auto callbacks = p.CreateFormCallbacks();
  const auto send = callbacks.sendToUser;
  callbacks.sendToUser = [&](auto actor, auto& message, auto reliable) {
    if (revoke && !called) {
      called = true;
      auto& remote = p.worldState.GetFormAt<MpActor>(remoteId);
      if (reload) remote.ApplyChangeForm(remote.GetChangeForm());
      else p.SetUserActor(0, 0);
    }
    send(actor, message, reliable);
  };
  DoConnect(p, 0);
  auto human = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x7);
  p.worldState.AddForm(std::move(human), host);
  p.worldState.GetFormAt<MpActor>(host).RegisterProfileId(42);
  p.SetUserActor(0, host);
  auto creature = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x3053f);
  p.worldState.AddForm(std::move(creature), remoteId);
  auto& remote = p.worldState.GetFormAt<MpActor>(remoteId);
  ActionListener listener(p);
  RawMessageData raw; raw.userId = 0;
  HostMessage msg; msg.remoteId = remoteId;
  listener.OnHostAttempt(raw, msg);
  remote.SetRespawnTime(1);
  remote.Kill();
  REQUIRE(remote.IsDead());
  revoke = true;
  time += std::chrono::seconds(2);
  p.worldState.Tick();
  REQUIRE(called);
  REQUIRE(remote.IsDead());
  if (!reload) {
    REQUIRE_FALSE(remote.IsRespawning());
    REQUIRE(p.worldState.hosters.count(remoteId) == 0);
  }
}

TEST_CASE("Kill retains the actor during synchronous removal publication",
          "[actor-runtime-identity][HostAttempt][espm]")
{
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  constexpr uint32_t host = 0xff000abc, remoteId = 0xff000abd;
  bool armed = false, called = false;
  std::weak_ptr<MpForm> lifetime;
  auto callbacks = p.CreateFormCallbacks();
  const auto send = callbacks.sendToUser;
  callbacks.sendToUser = [&](auto actor, auto& message, auto reliable) {
    if (armed && !called) {
      called = true;
      p.DestroyActor(remoteId);
      REQUIRE_FALSE(lifetime.expired());
    }
    send(actor, message, reliable);
  };
  DoConnect(p, 0);
  auto human = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x7);
  p.worldState.AddForm(std::move(human), host);
  p.worldState.GetFormAt<MpActor>(host).RegisterProfileId(42);
  p.SetUserActor(0, host);
  auto creature = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x3053f);
  p.worldState.AddForm(std::move(creature), remoteId);
  lifetime = p.worldState.LookupFormById(remoteId);
  ActionListener listener(p);
  RawMessageData raw; raw.userId = 0;
  HostMessage msg; msg.remoteId = remoteId;
  listener.OnHostAttempt(raw, msg);
  armed = true;
  p.worldState.GetFormAt<MpActor>(remoteId).Kill();
  REQUIRE(called);
  REQUIRE(lifetime.expired());
}

TEST_CASE("Observer publication stops when the source runtime changes",
          "[actor-runtime-identity][observer-publication][espm]")
{
  struct PublicationActor : MpActor {
    using MpActor::MpActor;
    using MpObjectReference::SendMessageToActorListeners;
  };
  const bool generic = GENERATE(false, true);
  const std::string change = GENERATE("reload-source", "destroy-source", "reload-other", "destroy-other", "unsubscribe-other", "none");
  CAPTURE(generic, change);
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  constexpr uint32_t remoteId = 0xff000abd;
  bool armed = false;
  size_t deliveries = 0;
  auto callbacks = p.CreateFormCallbacks();
  const auto send = callbacks.sendToUser;
  callbacks.sendToUser = [&](auto actor, auto& message, auto reliable) {
    const bool relevant = generic ? dynamic_cast<const UpdatePropertyMessage*>(&message) != nullptr :
      dynamic_cast<const DeathStateContainerMessage*>(&message) != nullptr;
    if (armed && relevant) {
      ++deliveries;
      if (deliveries == 1) {
        auto& source = p.worldState.GetFormAt<MpActor>(remoteId);
        if (change == "reload-source") source.ApplyChangeForm(source.GetChangeForm());
        else if (change == "destroy-source") p.DestroyActor(remoteId);
        else if (change != "none") {
          auto listeners = source.GetActorListeners();
          auto other = *std::find_if(listeners.begin(), listeners.end(), [&](auto entry) { return entry != actor; });
          if (change == "reload-other") other->ApplyChangeForm(other->GetChangeForm());
          else if (change == "destroy-other") p.DestroyActor(other->GetFormId());
          else MpObjectReference::Unsubscribe(&source, other);
        }
      }
    }
    send(actor, message, reliable);
  };
  for (uint32_t i = 0; i < 2; ++i) {
    DoConnect(p, i);
    const auto id = 0xff000ac0 + i;
    auto observer = std::make_unique<MpActor>(
      LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x7);
    p.worldState.AddForm(std::move(observer), id);
    p.worldState.GetFormAt<MpActor>(id).RegisterProfileId(42 + i);
    p.SetUserActor(i, id);
  }
  auto creature = std::make_unique<PublicationActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x3053f);
  auto* published = creature.get();
  p.worldState.AddForm(std::move(creature), remoteId);
  auto& source = *published;
  source.SetServerControlled(true);
  REQUIRE(source.IsServerControlled());
  // Actors also subscribe to themselves. This fixture exercises two remote
  // recipients and must not select the source as the other observer.
  MpObjectReference::Unsubscribe(&source, &source);
  for (uint32_t i = 0; i < 2; ++i)
    MpObjectReference::Subscribe(&source, &p.worldState.GetFormAt<MpActor>(0xff000ac0 + i));
  REQUIRE(source.GetActorListeners().size() == 2);
  armed = true;
  if (generic) {
    UpdatePropertyMessage message;
    source.SendMessageToActorListeners(message, true);
  } else {
    source.Kill();
  }
  REQUIRE(deliveries == (change == "none" ? 2 : 1));
}

TEST_CASE("Damage publication cannot mutate a replacement runtime",
          "[actor-runtime-identity][death-publication][espm]")
{
  const std::string change = GENERATE("reload", "destroy", "reload-aggressor", "destroy-aggressor", "none");
  const std::string entry = GENERATE("direct", "weapon", "spell");
  CAPTURE(change, entry);
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  constexpr uint32_t id = 0xff000abc;
  constexpr uint32_t aggressorId = 0xff000abd;
  bool armed = false, published = false;
  std::weak_ptr<MpForm> lifetime;
  std::weak_ptr<MpForm> aggressorLifetime;
  auto callbacks = p.CreateFormCallbacks();
  const auto send = callbacks.sendToUser;
  callbacks.sendToUser = [&](auto actor, auto& message, auto reliable) {
    if (armed && !published && dynamic_cast<const ChangeValuesMessage*>(&message)) {
      published = true;
      if (change == "reload") actor->ApplyChangeForm(actor->GetChangeForm());
      if (change == "destroy") {
        p.DestroyActor(id);
        REQUIRE_FALSE(lifetime.expired());
      }
      if (change == "reload-aggressor") {
        auto& aggressor = p.worldState.GetFormAt<MpActor>(aggressorId);
        aggressor.ApplyChangeForm(aggressor.GetChangeForm());
      }
      if (change == "destroy-aggressor") {
        p.DestroyActor(aggressorId);
        REQUIRE_FALSE(aggressorLifetime.expired());
      }
    }
    send(actor, message, reliable);
  };
  DoConnect(p, 0);
  auto owned = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x7);
  p.worldState.AddForm(std::move(owned), id);
  auto& actor = p.worldState.GetFormAt<MpActor>(id);
  actor.RegisterProfileId(42);
  p.SetUserActor(0, id);
  lifetime = p.worldState.LookupFormById(id);
  DoConnect(p, 1);
  p.CreateActor(aggressorId, {1, 1, 1}, 0, 0x3c, 43);
  p.SetUserActor(1, aggressorId);
  auto& aggressor = p.worldState.GetFormAt<MpActor>(aggressorId);
  aggressorLifetime = p.worldState.LookupFormById(aggressorId);
  constexpr uint32_t dagger = 0x1397e, flames = 0x12fcd;
  Equipment equipment;
  if (entry == "weapon") {
    aggressor.AddItem(dagger, 1);
    Inventory::ExtraData worn;
    worn.worn_ = true;
    equipment.inv.entries.emplace_back(dagger, 1, worn);
  } else equipment.leftSpell = flames;
  aggressor.SetEquipment(equipment);
  actor.SetPercentages({0.00001f, 1, 1});
  const auto previousHit = aggressor.GetLastHitTime(id);
  auto listener = std::make_shared<DeathPublicationListener>();
  p.AddListener(listener);
  armed = true;
  ActorValues lethal;
  lethal.healthPercentage = 0;
  if (entry == "direct") actor.NetSetPercentages(lethal, &aggressor, std::nullopt);
  else {
    RawMessageData raw;
    raw.userId = 1;
    HitMessage hit;
    hit.data.aggressor = 0x14;
    hit.data.target = id;
    hit.data.source = entry == "weapon" ? dagger : flames;
    p.GetActionListener().OnHit(raw, hit);
  }
  REQUIRE(published);
  REQUIRE(listener->deaths == (change == "none" ? 1 : 0));
  if (change == "reload") REQUIRE_FALSE(actor.IsDead());
  if (change == "destroy") REQUIRE(lifetime.expired());
  if (change == "destroy-aggressor") REQUIRE(aggressorLifetime.expired());
  if (entry == "weapon" && change != "none" && change != "destroy-aggressor")
    REQUIRE(aggressor.GetLastHitTime(id) == previousHit);
}

TEST_CASE("Death publication retains the original killer lifetime",
          "[actor-runtime-identity][death-publication][espm]")
{
  const std::string change = GENERATE("reload", "destroy", "none");
  CAPTURE(change);
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  constexpr uint32_t victimId = 0xff000abc, killerId = 0xff000abd;
  bool armed = false, published = false;
  std::weak_ptr<MpForm> killerLifetime;
  auto callbacks = p.CreateFormCallbacks();
  const auto send = callbacks.sendToUser;
  callbacks.sendToUser = [&](auto actor, auto& message, auto reliable) {
    if (armed && !published && dynamic_cast<const DeathStateContainerMessage*>(&message)) {
      published = true;
      if (change == "destroy") {
        p.DestroyActor(killerId);
        REQUIRE_FALSE(killerLifetime.expired());
      } else if (change == "reload") {
        auto& killer = p.worldState.GetFormAt<MpActor>(killerId);
        killer.ApplyChangeForm(killer.GetChangeForm());
      }
    }
    send(actor, message, reliable);
  };
  DoConnect(p, 0);
  auto owned = std::make_unique<MpActor>(
    LocationalData{{1, 1, 1}, {}, FormDesc::Tamriel()}, callbacks, 0x7);
  p.worldState.AddForm(std::move(owned), victimId);
  auto& actor = p.worldState.GetFormAt<MpActor>(victimId);
  actor.RegisterProfileId(42);
  p.SetUserActor(0, victimId);
  p.CreateActor(killerId, {1, 1, 1}, 0, 0x3c, 43);
  auto& killer = p.worldState.GetFormAt<MpActor>(killerId);
  killerLifetime = p.worldState.LookupFormById(killerId);
  auto listener = std::make_shared<DeathPublicationListener>();
  p.AddListener(listener);
  armed = true;
  actor.Kill(&killer);
  REQUIRE(published);
  REQUIRE(killerLifetime.expired() == (change == "destroy"));
  REQUIRE(listener->deaths == 1);
  const auto arguments = nlohmann::json::parse(listener->arguments);
  REQUIRE(arguments[1] == (change == "none" ? killerId : 0));
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
