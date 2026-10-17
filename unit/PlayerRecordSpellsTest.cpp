// Thornswood #1688. Every person is created on the Player record (0x7), and
// IsSpellLearnedFromBase counted that record's spell list, Flames and
// Healing, as held by every person. A person now keeps only the record's
// abilities; any other base still hands its actors its whole spell list.
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "script_classes/PapyrusActor.h"
#include "script_objects/EspmGameObject.h"
#include <algorithm>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kPlayerRecord = 0x7;
constexpr uint32_t kFlames = 0x12fcd;
constexpr uint32_t kHealing = 0x12fcc;
constexpr uint32_t kPcHealRateCombat = 0x1031d3; // an ability
// WoodElfMalePreset10: an NPC_ whose spell list is Flames and Healing.
constexpr uint32_t kNpcWithFlames = 0x10abac;
constexpr uint32_t kPersonId = 0xff000a88;
constexpr uint32_t kNpcId = 0xff000a89;

VarValue SpellArg(PartOne& p, uint32_t spellId)
{
  return VarValue(std::make_shared<EspmGameObject>(
    p.worldState.GetEspm().GetBrowser().LookupById(spellId)));
}
}

TEST_CASE("The Player record hands a person its abilities and no spells",
          "[Thornswood1688]")
{
  using T = espm::SPEL::SpellType;
  REQUIRE(MpActor::InheritsBaseSpell(kPlayerRecord, T::Ability));
  REQUIRE_FALSE(MpActor::InheritsBaseSpell(kPlayerRecord, T::Spell));
  REQUIRE_FALSE(MpActor::InheritsBaseSpell(kPlayerRecord, T::Power));
  REQUIRE_FALSE(MpActor::InheritsBaseSpell(kPlayerRecord, T::LesserPower));
  REQUIRE_FALSE(MpActor::InheritsBaseSpell(kPlayerRecord, T::Voice));
  REQUIRE_FALSE(MpActor::InheritsBaseSpell(kPlayerRecord, T::Disease));

  // Any other base: unchanged, every entry of its spell list.
  for (auto t : { T::Spell, T::Disease, T::Power, T::LesserPower, T::Ability,
                  T::Poison, T::Addiction, T::Voice }) {
    REQUIRE(MpActor::InheritsBaseSpell(kNpcWithFlames, t));
  }
}

TEST_CASE("A person does not start with Flames and Healing",
          "[Thornswood1688][espm]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(kPersonId, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, kPersonId);
  auto& actor = p.worldState.GetFormAt<MpActor>(kPersonId);
  REQUIRE(actor.GetBaseId() == kPlayerRecord);

  REQUIRE_FALSE(actor.IsSpellLearnedFromBase(kFlames));
  REQUIRE_FALSE(actor.IsSpellLearnedFromBase(kHealing));
  REQUIRE_FALSE(actor.IsSpellLearned(kFlames));
  REQUIRE_FALSE(actor.IsSpellLearned(kHealing));

  // The record's ability is still the person's, and still cannot be removed.
  REQUIRE(actor.IsSpellLearnedFromBase(kPcHealRateCombat));
  PapyrusActor papyrusActor;
  REQUIRE_FALSE(static_cast<bool>(papyrusActor.RemoveSpell(
    actor.ToVarValue(), { SpellArg(p, kPcHealRateCombat) })));

  p.DestroyActor(kPersonId);
  DoDisconnect(p, 0);
}

TEST_CASE("The server grants and takes back Flames like any other spell",
          "[Thornswood1688][espm]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(kPersonId, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, kPersonId);
  auto& actor = p.worldState.GetFormAt<MpActor>(kPersonId);
  PapyrusActor papyrusActor;

  // AddSpell used to answer false and record nothing: the engine counted
  // Flames as held already, from the Player record.
  REQUIRE(static_cast<bool>(
    papyrusActor.AddSpell(actor.ToVarValue(), { SpellArg(p, kFlames) })));
  REQUIRE(actor.IsSpellLearned(kFlames));
  auto list = actor.GetSpellList();
  REQUIRE(std::find(list.begin(), list.end(), kFlames) != list.end());

  // Listed by GetNthSpell, which skips what comes from the base.
  REQUIRE(static_cast<int>(
            papyrusActor.GetSpellCount(actor.ToVarValue(), {})) == 1);

  // RemoveSpell used to refuse: "can't remove spells inherited from
  // RACE/NPC_ records".
  REQUIRE(static_cast<bool>(
    papyrusActor.RemoveSpell(actor.ToVarValue(), { SpellArg(p, kFlames) })));
  REQUIRE_FALSE(actor.IsSpellLearned(kFlames));

  p.DestroyActor(kPersonId);
  DoDisconnect(p, 0);
}

TEST_CASE("An NPC still holds the spells on its own base record",
          "[Thornswood1688][espm]")
{
  PartOne& p = GetPartOne();
  p.worldState.AddForm(
    std::make_unique<MpActor>(
      LocationalData{ { 0, 0, 0 }, {}, FormDesc::Tamriel() },
      p.CreateFormCallbacks(), kNpcWithFlames),
    kNpcId);
  auto& npc = p.worldState.GetFormAt<MpActor>(kNpcId);

  REQUIRE(npc.IsSpellLearnedFromBase(kFlames));
  REQUIRE(npc.IsSpellLearnedFromBase(kHealing));
  REQUIRE(npc.IsSpellLearned(kFlames));

  p.worldState.DestroyForm<MpActor>(kNpcId);
}
