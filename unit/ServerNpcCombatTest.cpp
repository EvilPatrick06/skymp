#include "ActionListener.h"
#include "ChangeValuesMessage.h"
#include "CreateActorMessage.h"
#include "DeathStateContainerMessage.h"
#include "HitMessage.h"
#include "TestUtils.hpp"
#include "UpdateMovementMessage.h"
#include "UpdateAnimationMessage.h"
#include "UpdateAnimationMessage.h"
#include "formulas/TES5DamageFormula.h"
#include "libespm/IterateFields.h"
#include <algorithm>
#include <chrono>
#include <map>

PartOne& GetPartOne();

TEST_CASE("NPC faction ranks use the SNAM rank byte rather than the form ID",
          "[ServerNpcCombat][espm]")
{
  auto& p = GetPartOne();
  p.worldState.npcEnabled = true;
  p.worldState.npcAllowEssential = true;
  p.worldState.npcAllowCrimeFaction = true;
  auto form = p.worldState.LookupFormById(0x1a66e);
  REQUIRE(form);
  auto actor = form->AsActor();
  REQUIRE(actor);
  auto lookup = p.GetEspm().GetBrowser().LookupById(actor->GetBaseId());
  auto npc = espm::Convert<espm::NPC_>(lookup.rec);
  REQUIRE(npc);
  auto data = npc->GetData(p.worldState.GetEspmCache());
  size_t index = 0;
  espm::IterateFields_(lookup.rec, [&](const char* type, uint32_t size, const char* bytes) {
    if (std::memcmp(type, "SNAM", 4) || size < 5) return;
    REQUIRE(index < data.factions.size());
    REQUIRE(data.factions[index++].rank == static_cast<int8_t>(bytes[4]));
  }, p.worldState.GetEspmCache());
  REQUIRE(index > 0);
}

namespace {
constexpr uint32_t kNpc = 0xff000100;
constexpr uint32_t kHuman = 0xff000000;
constexpr uint32_t kDagger = 0x1397e;
constexpr uint32_t kUnarmed = 0x1f4;

// Observe the hit chosen by the server, while retaining real Skyrim damage.
class RecordingDamageFormula : public IDamageFormula
{
public:
  float CalculateDamage(const MpActor& aggressor, const MpActor& target,
                        const HitData& hit) const override
  {
    ++calls;
    last = hit;
    return TES5DamageFormula().CalculateDamage(aggressor, target, hit);
  }
  float CalculateDamage(const MpActor& aggressor, const MpActor& target,
                        const SpellCastData& spell) const override
  {
    return TES5DamageFormula().CalculateDamage(aggressor, target, spell);
  }
  mutable size_t calls = 0;
  mutable HitData last;
};

Equipment DaggerEquipment(bool worn = true)
{
  Inventory::ExtraData extra;
  extra.worn_ = worn;
  Equipment equipment;
  equipment.inv.entries.emplace_back(kDagger, 1, extra);
  return equipment;
}

struct CombatFixture
{
  PartOne& p = GetPartOne();
  RecordingDamageFormula* damage = nullptr;

  CombatFixture()
  {
    p.worldState.npcEnabled = true;
    p.worldState.npcAllowEssential = true;
    p.worldState.npcAllowCrimeFaction = true;
    // Reuse Hulda's real base record; default player base 7 is not an NPC.
    auto hulda = p.worldState.LookupFormById(0x1a66e);
    REQUIRE(hulda);
    REQUIRE(hulda->AsActor());
    auto npc = std::make_unique<MpActor>(
      LocationalData{ { 0, 0, 0 }, {}, FormDesc::Tamriel() },
      p.CreateFormCallbacks(), hulda->AsActor()->GetBaseId());
    p.worldState.AddForm(std::move(npc), kNpc);
    Npc().SetEquipment(Equipment());
    Npc().SetPercentages({ 1, 1, 1 });
    Npc().SetServerControlled(true);
    DoConnect(p, 0);
    p.CreateActor(kHuman, { 0, 40, 0 }, 180, 0x3c);
    p.SetUserActor(0, kHuman);
    Npc().ForceSubscriptionsUpdate();
    Human().SetEquipment(Equipment());
    Human().SetPercentages({ 1, 1, 1 });
    auto formula = std::make_unique<RecordingDamageFormula>();
    damage = formula.get();
    p.SetDamageFormula(std::move(formula));
    Arm(Npc());
    p.Messages().clear();
  }
  MpActor& Npc() { return p.worldState.GetFormAt<MpActor>(kNpc); }
  MpActor& Human() { return p.worldState.GetFormAt<MpActor>(kHuman); }
  void Arm(MpActor& actor)
  {
    actor.AddItem(kDagger, 1);
    actor.SetEquipment(DaggerEquipment());
  }
  bool Attack() { return p.GetActionListener().ServerNpcAttack(kNpc, kHuman); }
};
}

TEST_CASE("Server NPC attacks use owned equipped weapons or unarmed damage",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  uint32_t expectedSource = kDagger;
  SECTION("Equipped owned iron dagger") {}
  SECTION("Empty equipment uses unarmed")
  {
    f.Npc().SetEquipment(Equipment());
    expectedSource = kUnarmed;
  }
  SECTION("An inventory weapon which is not worn is not an equipped attack")
  {
    f.Npc().SetEquipment(DaggerEquipment(false));
    expectedSource = kUnarmed;
  }
  SECTION("Counterfeit equipped weapons cannot enter the damage pipeline")
  {
    f.Npc().SetInventory(Inventory());
    f.Npc().SetEquipment(DaggerEquipment());
    REQUIRE_FALSE(f.Npc().GetInventory().HasItem(kDagger));
    REQUIRE_FALSE(f.Attack());
    REQUIRE(f.damage->calls == 0);
    REQUIRE(f.Human().GetActorValues().healthPercentage == 1.f);
    return;
  }
  REQUIRE(f.Attack());
  REQUIRE(f.damage->calls == 1);
  REQUIRE(f.damage->last.source == expectedSource);
  REQUIRE(f.damage->last.aggressor == kNpc);
  REQUIRE(f.damage->last.target == kHuman);
  REQUIRE(f.Human().GetActorValues().healthPercentage < 1.f);
  REQUIRE(f.Npc().GetActorValues().healthPercentage == 1.f);
  REQUIRE(f.p.worldState.hosters.count(kNpc) == 0);
  if (f.p.IsConnected(0)) {
    bool sentHealth = false, sentAttack = false;
    for (const auto& sent : f.p.Messages()) {
      auto update = dynamic_cast<ChangeValuesMessage*>(sent.message.get());
      sentHealth |= sent.userId == 0 && update &&
        update->idx == f.Human().GetIdx() && update->data.health.has_value();
      auto animation = dynamic_cast<UpdateAnimationMessage*>(sent.message.get());
      sentAttack |= sent.userId == 0 && animation &&
        animation->idx == f.Npc().GetIdx() && !animation->data.animEventName.empty();
    }
    REQUIRE(sentHealth);
    REQUIRE(sentAttack);
  }
}

TEST_CASE("Server NPC attacks reject invalid authority and inactive actors",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  uint32_t aggressor = kNpc;
  uint32_t target = kHuman;
  SECTION("Uncontrolled NPC") { f.Npc().SetServerControlled(false); }
  SECTION("Human aggressor") { aggressor = kHuman; target = kNpc; }
  SECTION("NPC assigned a human profile after taking authority")
  {
    f.Npc().RegisterProfileId(123);
  }
  SECTION("Missing aggressor") { aggressor = 0xff123456; }
  SECTION("Missing target") { target = 0xff123456; }
  SECTION("Dead aggressor") { f.Npc().SetIsDead(true); }
  SECTION("Dead target") { f.Human().SetIsDead(true); }
  SECTION("Disconnected human target")
  {
    DoDisconnect(f.p, 0);
    REQUIRE(f.Human().GetUserId() == Networking::InvalidUserId);
    REQUIRE_FALSE(f.p.IsConnected(0));
  }
  SECTION("Disabled aggressor") { f.Npc().Disable(); }
  SECTION("Disabled target") { f.Human().Disable(); }
  SECTION("Different cells")
  {
    f.Human().SetCellOrWorld(FormDesc{ 0x1a26f, "Skyrim.esm" });
  }
  SECTION("Melee target outside reach but within the old 4096-unit allowance")
  {
    f.Human().SetPos({ 0, 1000, 0 });
  }
  const auto npcHealth = f.Npc().GetActorValues().healthPercentage;
  const auto humanHealth = f.Human().GetActorValues().healthPercentage;
  const auto previousHit = f.Npc().GetLastHitTime(kHuman);
  REQUIRE_FALSE(f.p.GetActionListener().ServerNpcAttack(aggressor, target));
  REQUIRE(f.Npc().GetActorValues().healthPercentage == npcHealth);
  REQUIRE(f.Human().GetActorValues().healthPercentage == humanHealth);
  REQUIRE(f.Npc().GetLastHitTime(kHuman) == previousHit);
  REQUIRE(f.damage->calls == 0);
}

TEST_CASE("Server combat between NPCs needs no connected humans and protects offline human profiles",
          "[ServerNpcCombat][NpcOfflineTarget][PartOne][espm]")
{
  CombatFixture f;
  constexpr uint32_t targetId = 0xff000101;
  auto target = std::make_unique<MpActor>(
    LocationalData{ { 0, 40, 0 }, {}, FormDesc::Tamriel() },
    f.p.CreateFormCallbacks(), f.Npc().GetBaseId());
  f.p.worldState.AddForm(std::move(target), targetId);
  auto& npcTarget = f.p.worldState.GetFormAt<MpActor>(targetId);
  npcTarget.SetEquipment(Equipment());
  npcTarget.SetPercentages({ 1, 1, 1 });
  npcTarget.SetServerControlled(true);
  bool accepted = true;
  SECTION("Two genuine NPCs can fight with no connected humans") {}
  SECTION("An offline human profile on an NPC base cannot be hurt")
  {
    npcTarget.RegisterProfileId(123);
    accepted = false;
  }
  DoDisconnect(f.p, 0);
  REQUIRE_FALSE(f.p.IsConnected(0));
  REQUIRE(npcTarget.GetUserId() == Networking::InvalidUserId);
  REQUIRE(f.Npc().GetUserId() == Networking::InvalidUserId);
  f.p.Messages().clear();
  const auto health = npcTarget.GetActorValues().healthPercentage;
  REQUIRE(f.p.GetActionListener().ServerNpcAttack(kNpc, targetId) == accepted);
  if (accepted) {
    REQUIRE(f.damage->calls == 1);
    REQUIRE(f.damage->last.target == targetId);
    REQUIRE(npcTarget.GetActorValues().healthPercentage < health);
  } else {
    REQUIRE(f.damage->calls == 0);
    REQUIRE(npcTarget.GetActorValues().healthPercentage == health);
  }
  REQUIRE(f.Human().GetActorValues().healthPercentage == 1.f);
}

TEST_CASE("Server NPC attacks reject immediate repeats and recover after cooldown",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  REQUIRE(f.Attack());
  const auto afterFirst = f.Human().GetActorValues().healthPercentage;
  const auto lastHit = f.Npc().GetLastHitTime(kHuman);
  REQUIRE_FALSE(f.Attack());
  REQUIRE(f.Human().GetActorValues().healthPercentage == afterFirst);
  REQUIRE(f.Npc().GetLastHitTime(kHuman) == lastHit);
  REQUIRE(f.damage->calls == 1);
  f.Npc().SetLastHitTime(kHuman,
    std::chrono::steady_clock::now() - std::chrono::seconds(10));
  REQUIRE(f.Attack());
  REQUIRE(f.Human().GetActorValues().healthPercentage < afterFirst);
  REQUIRE(f.damage->calls == 2);
}

TEST_CASE("Server NPC attacks enter the normal directional block pipeline",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  bool facingAttacker = true;
  SECTION("Facing attacker blocks") {}
  SECTION("Facing away does not block") { facingAttacker = false; }
  const auto direction = f.Human().GetViewDirection();
  f.Npc().SetPos(f.Human().GetPos() +
    direction * (facingAttacker ? 40.f : -40.f));
  f.Human().SetIsBlockActive(true);
  REQUIRE(f.Attack());
  REQUIRE(f.damage->calls == 1);
  REQUIRE(f.damage->last.isHitBlocked == facingAttacker);
  const float expectedDamage = facingAttacker ? 0.4f : 4.f;
  const float expectedHealth = 1.f -
    expectedDamage / GetBaseActorValues(&f.p.worldState, f.Human().GetBaseId(),
      f.Human().GetRaceId(), f.Human().GetTemplateChain()).health;
  REQUIRE_THAT(f.Human().GetActorValues().healthPercentage,
               Catch::Matchers::WithinAbs(expectedHealth, 0.0001f));
}

TEST_CASE("Lethal server NPC attacks use the engine death and notification path",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  f.Human().SetPercentages({ 0.0001f, 1, 1 });
  f.p.Messages().clear();
  REQUIRE(f.Attack());
  REQUIRE(f.Human().IsDead());
  bool deathSent = false;
  for (const auto& sent : f.p.Messages()) {
    deathSent |= sent.userId == 0 &&
      dynamic_cast<DeathStateContainerMessage*>(sent.message.get());
  }
  REQUIRE(deathSent);
  const auto calls = f.damage->calls;
  REQUIRE_FALSE(f.Attack());
  REQUIRE(f.damage->calls == calls);
}

TEST_CASE("Spoofed hosted client hits cannot drive server NPC combat",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  // Reinstate stale hosting to prove the authority guard, not map erasure.
  f.p.worldState.hosters[kNpc] = kHuman;
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = kNpc;
  hit.data.target = 0x14;
  hit.data.source = kDagger;
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.Human().GetActorValues().healthPercentage == 1.f);
  REQUIRE(f.damage->calls == 0);
  REQUIRE(f.Npc().GetServerCombatTarget() == 0);
  REQUIRE(f.Attack());
  REQUIRE(f.Human().GetActorValues().healthPercentage < 1.f);
}

TEST_CASE("Successful human hits record a server NPC retaliation target",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  f.Arm(f.Human());
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = 0x14;
  hit.data.target = kNpc;
  hit.data.source = kDagger;
  REQUIRE(f.Npc().GetServerCombatTarget() == 0);
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.Npc().GetActorValues().healthPercentage < 1.f);
  REQUIRE(f.Npc().GetServerCombatTarget() == kHuman);
  REQUIRE(f.Human().GetActorValues().healthPercentage == 1.f);
}

TEST_CASE("Server NPC health and death are replicated to every current observer",
          "[ServerNpcCombat][PartOne][espm]")
{
  CombatFixture f;
  constexpr uint32_t observerId = 0xff000001;
  DoConnect(f.p, 1);
  f.p.CreateActor(observerId, { 0, 20, 0 }, 0, 0x3c);
  f.p.SetUserActor(1, observerId);
  f.Npc().ForceSubscriptionsUpdate();
  for (uint32_t id : { kHuman, observerId }) {
    bool subscribed = false;
    for (const auto* listener : f.Npc().GetActorListeners()) {
      subscribed |= listener->GetFormId() == id;
    }
    REQUIRE(subscribed);
  }
  bool lethal = false;
  SECTION("Nonlethal hit sends reliable health to both humans") {}
  SECTION("Lethal hit sends reliable death and a terminal movement snapshot")
  {
    lethal = true;
    f.Npc().SetPercentages({ 0.0001f, 1, 1 });
    f.Npc().UpdateServerMovement({ 0, 1, 0 }, { 0, 0, 0 }, 60);
  }
  f.Arm(f.Human());
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = 0x14;
  hit.data.target = kNpc;
  hit.data.source = kDagger;
  f.p.Messages().clear();
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.damage->calls == 1);
  REQUIRE(f.Npc().IsDead() == lethal);
  REQUIRE(f.Npc().GetActorValues().healthPercentage < 1.f);
  for (Networking::UserId user : { 0, 1 }) {
    bool health = false, death = false, terminalMovement = false;
    for (const auto& sent : f.p.Messages()) {
      if (sent.userId != user) continue;
      if (auto change = dynamic_cast<ChangeValuesMessage*>(sent.message.get());
          change && change->idx == f.Npc().GetIdx()) {
        REQUIRE(sent.reliable);
        REQUIRE(change->data.health.has_value());
        health = true;
      }
      if (auto container = dynamic_cast<DeathStateContainerMessage*>(sent.message.get());
          container && container->tIsDead &&
          container->tIsDead->idx == f.Npc().GetIdx()) {
        REQUIRE(sent.reliable);
        REQUIRE(container->tIsDead->dataDump == "true");
        death = true;
      }
      if (auto movement = dynamic_cast<UpdateMovementMessage*>(sent.message.get());
          movement && movement->idx == f.Npc().GetIdx() && movement->data.isDead) {
        REQUIRE(sent.reliable);
        REQUIRE(movement->data.speed == 0.f);
        REQUIRE(movement->data.runMode == "Standing");
        terminalMovement = true;
      }
    }
    if (lethal) {
      REQUIRE(death);
      REQUIRE(terminalMovement);
    } else {
      REQUIRE(health);
    }
  }
  if (lethal) {
    // The death must also survive stream-in; no later AI tick is needed.
    DoConnect(f.p, 2);
    f.p.CreateActor(0xff000002, f.Npc().GetPos(), 0, 0x3c);
    f.p.Messages().clear();
    f.p.SetUserActor(2, 0xff000002);
    bool deadCreate = false, deadSnapshot = false;
    for (const auto& sent : f.p.Messages()) {
      if (sent.userId != 2) continue;
      if (auto create = dynamic_cast<CreateActorMessage*>(sent.message.get());
          create && create->idx == f.Npc().GetIdx()) {
        REQUIRE(create->isDead == true);
        REQUIRE(create->props.isHostedByOther == true);
        deadCreate = true;
      }
      if (auto movement = dynamic_cast<UpdateMovementMessage*>(sent.message.get());
          movement && movement->idx == f.Npc().GetIdx()) {
        REQUIRE(sent.reliable);
        REQUIRE(movement->data.isDead);
        REQUIRE(movement->data.speed == 0.f);
        REQUIRE(movement->data.runMode == "Standing");
        deadSnapshot = true;
      }
    }
    REQUIRE(deadCreate);
    REQUIRE(deadSnapshot);
  }
}

TEST_CASE("Saved empty and removed NPC factions take precedence over base factions after restart",
          "[ServerNpcCombat][NpcFactions][save][espm]")
{
  auto& p = GetPartOne();
  p.worldState.npcEnabled = true;
  p.worldState.npcAllowEssential = true;
  p.worldState.npcAllowCrimeFaction = true;
  constexpr uint32_t huldaId = 0x1a66e;
  auto form = p.worldState.LookupFormById(huldaId);
  REQUIRE(form);
  auto* hulda = form->AsActor();
  REQUIRE(hulda);
  const auto baseFactions = hulda->GetFactions(-128, 127);
  REQUIRE_FALSE(baseFactions.empty());
  std::vector<Faction> expected;
  SECTION("An explicit saved empty list stays empty")
  {
    auto change = hulda->GetChangeForm();
    change.factions = std::vector<Faction>();
    hulda->ApplyChangeForm(change);
  }
  SECTION("Removing every base faction stays empty")
  {
    for (const auto& faction : baseFactions) {
      hulda->RemoveFromFaction(faction.formDesc);
    }
  }
  SECTION("Removing one faction does not restore it or remove its surviving peers")
  {
    hulda->RemoveFromFaction(baseFactions.front().formDesc);
    expected.assign(baseFactions.begin() + 1, baseFactions.end());
  }
  // Exercise the actual save codec, not just a copied in-memory actor.
  const auto serialized = MpChangeForm::ToJson(hulda->GetChangeForm()).dump();
  simdjson::dom::parser parser;
  auto document = parser.parse(serialized).value();
  const auto saved = MpChangeForm::JsonToChangeForm(document);
  REQUIRE(saved.factions.has_value());
  REQUIRE(saved.factions->size() == expected.size());
  auto& recovered = GetPartOne();
  recovered.worldState.npcEnabled = true;
  recovered.worldState.npcAllowEssential = true;
  recovered.worldState.npcAllowCrimeFaction = true;
  recovered.worldState.LoadChangeForm(saved, recovered.CreateFormCallbacks());
  auto recoveredForm = recovered.worldState.LookupFormById(huldaId);
  REQUIRE(recoveredForm);
  auto& restored = *recoveredForm->AsActor();
  const auto after = restored.GetFactions(-128, 127);
  REQUIRE(after.size() == expected.size());
  for (const auto& faction : expected) {
    auto found = std::find_if(after.begin(), after.end(), [&](const auto& entry) {
      return entry.formDesc == faction.formDesc && entry.rank == faction.rank;
    });
    REQUIRE(found != after.end());
  }
  REQUIRE(restored.GetFactions(-128, 127).size() == expected.size());
}

TEST_CASE("Rejected repeated human weapon hits cannot replace a newer retaliation target",
          "[ServerNpcCombat][NpcRetaliation][PartOne][espm]")
{
  CombatFixture f;
  constexpr uint32_t secondHuman = 0xff000001;
  DoConnect(f.p, 1);
  f.p.CreateActor(secondHuman, { 0, 40, 0 }, 180, 0x3c);
  f.p.SetUserActor(1, secondHuman);
  f.Arm(f.Human());
  f.Arm(f.p.worldState.GetFormAt<MpActor>(secondHuman));
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = 0x14;
  hit.data.target = kNpc;
  hit.data.source = kDagger;
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.Npc().GetServerCombatTarget() == kHuman);
  raw.userId = 1;
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.damage->calls == 2);
  REQUIRE(f.Npc().GetServerCombatTarget() == secondHuman);
  const auto health = f.Npc().GetActorValues().healthPercentage;
  // Avoid machine scheduling delays turning the replay into a valid attack.
  f.Human().SetLastHitTime(kNpc, std::chrono::steady_clock::now());
  raw.userId = 0;
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.damage->calls == 2);
  REQUIRE(f.Npc().GetActorValues().healthPercentage == health);
  REQUIRE(f.Npc().GetServerCombatTarget() == secondHuman);
}

TEST_CASE("Accepted human retaliation targets clear when they are no longer valid",
          "[ServerNpcCombat][NpcRetaliation][PartOne][espm]")
{
  CombatFixture f;
  f.Arm(f.Human());
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = 0x14;
  hit.data.target = kNpc;
  hit.data.source = kDagger;
  f.p.GetActionListener().OnHit(raw, hit);
  REQUIRE(f.damage->calls == 1);
  REQUIRE(f.Npc().GetServerCombatTarget() == kHuman);
  SECTION("Target dies") { f.Human().SetIsDead(true); }
  SECTION("Target is disabled") { f.Human().Disable(); }
  SECTION("Target changes cell")
  {
    f.Human().SetCellOrWorld(FormDesc{ 0x1a26f, "Skyrim.esm" });
  }
  SECTION("Target disconnects") { DoDisconnect(f.p, 0); }
  REQUIRE(f.Npc().GetServerCombatTarget() == 0);
}

TEST_CASE("Accepted equipped spell hits can establish server NPC retaliation",
          "[ServerNpcCombat][NpcRetaliation][SpellHit][PartOne][espm]")
{
  CombatFixture f;
  // Evaluate the real Flames SPEL and its health effects through the formula.
  constexpr uint32_t flames = 0x12fcd;
  const auto lookup = f.p.GetEspm().GetBrowser().LookupById(flames);
  REQUIRE(lookup.rec);
  REQUIRE(espm::Convert<espm::SPEL>(lookup.rec));
  Equipment equipment;
  equipment.leftSpell = flames;
  f.Human().SetEquipment(equipment);
  bool equipped = true;
  SECTION("Accepted equipped spell hit records the human caster") {}
  SECTION("An unequipped spell cannot damage or set retaliation")
  {
    equipped = false;
    f.Human().SetEquipment(Equipment());
  }
  const float health = f.Npc().GetActorValues().healthPercentage;
  RawMessageData raw;
  raw.userId = 0;
  HitMessage hit;
  hit.data.aggressor = 0x14;
  hit.data.target = kNpc;
  hit.data.source = flames;
  REQUIRE(f.Npc().GetServerCombatTarget() == 0);
  f.p.GetActionListener().OnHit(raw, hit);
  if (equipped) {
    REQUIRE(f.Npc().GetActorValues().healthPercentage < health);
    REQUIRE(f.Npc().GetServerCombatTarget() == kHuman);
  } else {
    REQUIRE(f.Npc().GetActorValues().healthPercentage == health);
    REQUIRE(f.Npc().GetServerCombatTarget() == 0);
  }
}

namespace {
struct DeliveryFixture : CombatFixture
{
  static constexpr uint32_t kOtherHuman = 0xff000001;
  static constexpr uint32_t kDisconnectedHuman = 0xff000002;
  static constexpr uint32_t kHostedListener = 0xff000200;

  DeliveryFixture()
  {
    DoConnect(p, 1);
    p.CreateActor(kOtherHuman, { 0, 20, 0 }, 0, 0x3c);
    p.SetUserActor(1, kOtherHuman);
    DoConnect(p, 2);
    p.CreateActor(kDisconnectedHuman, { 0, 30, 0 }, 0, 0x3c);
    p.SetUserActor(2, kDisconnectedHuman);
    DoDisconnect(p, 2);
    REQUIRE_FALSE(p.IsConnected(2));
    auto listener = std::make_unique<MpActor>(
      LocationalData{ { 0, 10, 0 }, {}, FormDesc::Tamriel() },
      p.CreateFormCallbacks(), Npc().GetBaseId());
    p.worldState.AddForm(std::move(listener), kHostedListener);
    auto& hosted = p.worldState.GetFormAt<MpActor>(kHostedListener);
    auto& disconnected = p.worldState.GetFormAt<MpActor>(kDisconnectedHuman);
    p.worldState.hosters[kHostedListener] = kHuman;
    p.worldState.hosters[kDisconnectedHuman] = kHuman;
    REQUIRE(hosted.GetUserId() == Networking::InvalidUserId);
    REQUIRE(disconnected.GetUserId() == Networking::InvalidUserId);
    REQUIRE(hosted.GetActorToSendTo().GetFormId() == kHuman);
    // Keep stale entries in the listener collection to exercise delivery itself.
    for (uint32_t id : { kHuman, kOtherHuman, kDisconnectedHuman, kHostedListener }) {
      auto& observer = p.worldState.GetFormAt<MpActor>(id);
      MpObjectReference::Subscribe(&Npc(), &observer);
      REQUIRE(std::find(Npc().GetActorListeners().begin(),
                        Npc().GetActorListeners().end(), &observer) !=
              Npc().GetActorListeners().end());
    }
    p.Messages().clear();
  }
};

template <class Message, class Predicate>
void CheckDirectDelivery(PartOne& p, Predicate matches, bool reliable,
                         const std::vector<Networking::UserId>& recipients)
{
  std::map<Networking::UserId, size_t> counts;
  for (const auto& sent : p.Messages()) {
    auto message = dynamic_cast<Message*>(sent.message.get());
    if (!message || !matches(*message)) continue;
    CHECK(sent.reliable == reliable);
    ++counts[sent.userId];
  }
  for (const auto& entry : counts) {
    CHECK(std::find(recipients.begin(), recipients.end(), entry.first) !=
          recipients.end());
  }
  for (Networking::UserId recipient : recipients) {
    CHECK(counts[recipient] == 1);
  }
  CHECK(counts[2] == 0);
}
}

TEST_CASE("Ordinary server NPC movement delivers one unreliable snapshot to each connected human",
          "[ServerNpcCombat][NpcDelivery][PartOne][espm]")
{
  DeliveryFixture f;
  const NiPoint3 position{ 1, 0, 0 };
  f.Npc().UpdateServerMovement(position, { 0, 0, 90 }, 60);
  CheckDirectDelivery<UpdateMovementMessage>(f.p, [&](const auto& message) {
    if (message.idx != f.Npc().GetIdx()) return false;
    CHECK(message.data.pos == std::array<float, 3>{ 1, 0, 0 });
    CHECK(message.data.speed == 60);
    return true;
  }, false, { 0, 1 });
}

TEST_CASE("Server NPC health death and attack events deliver reliably without hosted redirects",
          "[ServerNpcCombat][NpcDelivery][PartOne][espm]")
{
  DeliveryFixture f;
  SECTION("Health update reaches each connected human once")
  {
    auto values = f.Npc().GetActorValues();
    values.healthPercentage = 0.75f;
    f.Npc().NetSendChangeValues(values,
      std::vector<espm::ActorValue>{ espm::ActorValue::Health });
    CheckDirectDelivery<ChangeValuesMessage>(f.p, [&](const auto& message) {
      return message.idx == f.Npc().GetIdx();
    }, true, { 0, 1 });
  }
  SECTION("Terminal death and stopped movement reach each connected human once")
  {
    f.Npc().Kill(&f.Human());
    REQUIRE(f.Npc().IsDead());
    CheckDirectDelivery<DeathStateContainerMessage>(f.p, [&](const auto& message) {
      return message.tIsDead && message.tIsDead->idx == f.Npc().GetIdx();
    }, true, { 0, 1 });
    CheckDirectDelivery<UpdateMovementMessage>(f.p, [&](const auto& message) {
      if (message.idx != f.Npc().GetIdx()) return false;
      CHECK(message.data.isDead);
      CHECK(message.data.speed == 0);
      CHECK(message.data.runMode == "Standing");
      return true;
    }, true, { 0, 1 });
  }
  SECTION("Accepted attack animation reaches each connected human once")
  {
    REQUIRE(f.Attack());
    CheckDirectDelivery<UpdateAnimationMessage>(f.p, [&](const auto& message) {
      if (message.idx != f.Npc().GetIdx()) return false;
      CHECK(message.data.animEventName == "attackStart");
      return true;
    }, true, { 0, 1 });
  }
}

TEST_CASE("A disconnected observer receives no later server NPC movement or health events",
          "[ServerNpcCombat][NpcDelivery][PartOne][espm]")
{
  DeliveryFixture f;
  DoDisconnect(f.p, 1);
  auto& stale = f.p.worldState.GetFormAt<MpActor>(DeliveryFixture::kOtherHuman);
  REQUIRE(stale.GetUserId() == Networking::InvalidUserId);
  REQUIRE_FALSE(f.p.IsConnected(1));
  f.p.worldState.hosters[DeliveryFixture::kOtherHuman] = kHuman;
  MpObjectReference::Subscribe(&f.Npc(), &stale);
  f.p.Messages().clear();
  f.Npc().UpdateServerMovement({ 1, 0, 0 }, { 0, 0, 90 }, 60);
  CheckDirectDelivery<UpdateMovementMessage>(f.p, [&](const auto& message) {
    return message.idx == f.Npc().GetIdx();
  }, false, { 0 });
  auto values = f.Npc().GetActorValues();
  values.healthPercentage = 0.5f;
  f.Npc().NetSendChangeValues(values,
    std::vector<espm::ActorValue>{ espm::ActorValue::Health });
  CheckDirectDelivery<ChangeValuesMessage>(f.p, [&](const auto& message) {
    return message.idx == f.Npc().GetIdx();
  }, true, { 0 });
}

TEST_CASE("Stopping server NPC movement reliably preserves position and clears combat for current and late observers",
          "[ServerNpcCombat][NpcDelivery][NpcStop][PartOne][espm]")
{
  DeliveryFixture f;
  f.Npc().UpdateServerMovement({ 1, 0, 0 }, { 0, 0, 90 }, 60);
  f.Npc().RecordServerCombatTarget(kHuman);
  REQUIRE(f.Npc().GetServerMovementMessage().data.speed == 60);
  REQUIRE(f.Npc().GetServerCombatTarget() == kHuman);
  const auto position = f.Npc().GetPos();
  const auto angle = f.Npc().GetAngle();
  f.p.Messages().clear();
  f.Npc().StopServerMovement();
  REQUIRE(f.Npc().GetPos() == position);
  REQUIRE(f.Npc().GetAngle() == angle);
  REQUIRE(f.Npc().IsServerControlled());
  REQUIRE(f.Npc().GetServerCombatTarget() == 0);
  const auto snapshot = f.Npc().GetServerMovementMessage();
  REQUIRE(snapshot.data.speed == 0);
  REQUIRE(snapshot.data.runMode == "Standing");
  CheckDirectDelivery<UpdateMovementMessage>(f.p, [&](const auto& message) {
    if (message.idx != f.Npc().GetIdx()) return false;
    CHECK(message.data.speed == 0);
    CHECK(message.data.runMode == "Standing");
    CHECK(message.data.pos == snapshot.data.pos);
    CHECK(message.data.rot == snapshot.data.rot);
    return true;
  }, true, { 0, 1 });
  // Bind a new observer after the stop, without a subsequent movement tick.
  DoConnect(f.p, 3);
  f.p.CreateActor(0xff000003, position, 0, 0x3c);
  f.p.Messages().clear();
  f.p.SetUserActor(3, 0xff000003);
  CheckDirectDelivery<UpdateMovementMessage>(f.p, [&](const auto& message) {
    if (message.idx != f.Npc().GetIdx()) return false;
    CHECK(message.data.speed == 0);
    CHECK(message.data.runMode == "Standing");
    CHECK(message.data.pos == snapshot.data.pos);
    CHECK(message.data.rot == snapshot.data.rot);
    return true;
  }, true, { 3 });
}

TEST_CASE("Server movement stop rejects human and uncontrolled NPC actors without changes",
          "[ServerNpcCombat][NpcStop][PartOne][espm]")
{
  DeliveryFixture f;
  MpActor* actor = nullptr;
  SECTION("Connected human cannot use the native NPC stop") { actor = &f.Human(); }
  SECTION("An uncontrolled NPC cannot use the native NPC stop")
  {
    f.Npc().SetServerControlled(false);
    actor = &f.Npc();
  }
  REQUIRE(actor);
  const auto position = actor->GetPos();
  const auto angle = actor->GetAngle();
  const auto controlled = actor->IsServerControlled();
  f.p.Messages().clear();
  REQUIRE_THROWS_AS(actor->StopServerMovement(), std::invalid_argument);
  REQUIRE(actor->GetPos() == position);
  REQUIRE(actor->GetAngle() == angle);
  REQUIRE(actor->IsServerControlled() == controlled);
  REQUIRE(f.p.Messages().empty());
}
