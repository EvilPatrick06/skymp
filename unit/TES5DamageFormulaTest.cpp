#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>

#include "GetBaseActorValues.h"
#include "HitData.h"
#include "PacketParser.h"
#include "formulas/TES5DamageFormula.h"
#include "libespm/Loader.h"

namespace {
const auto kExtraWornTrue = [] {
  Inventory::ExtraData extra;
  extra.worn_ = true;
  return extra;
}();
const auto kExtraWornFalse = [] {
  Inventory::ExtraData extra;
  extra.worn_ = false;
  return extra;
}();

// THORNSWOOD (#1384): a worn piece whose name carries its condition, the way
// the gamemode writes it: "Iron Dagger (120%)".
Inventory::ExtraData WornNamed(const std::string& name, bool left = false)
{
  Inventory::ExtraData extra;
  if (left) {
    extra.wornLeft = true;
  } else {
    extra.worn_ = true;
  }
  extra.name = name;
  return extra;
}
}

PartOne& GetPartOne();
extern espm::Loader l;
using namespace std::chrono_literals;

TEST_CASE("Formula takes weapon damage into account", "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  ac.SetEquipment(Equipment());

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage

  TES5DamageFormula formula{};
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 4.0f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("Damage is reduced based on target's armor", "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage

  // 77382 = 0x12e46: Iron Gauntlets, rating = 10
  // 77387 = 0x12e4b: Iron Boots, rating = 10
  // 77389 = 0x12e4d: Iron Helmet, rating = 15
  // Total rating for worn armor: 10 + 10 = 20

  Equipment eq;
  eq.inv.entries.push_back(Inventory::Entry(77382, 1, kExtraWornTrue));
  eq.inv.entries.push_back(Inventory::Entry(77387, 1, kExtraWornTrue));
  eq.inv.entries.push_back(Inventory::Entry(77389, 1, kExtraWornFalse));
  ac.SetEquipment(eq);

  TES5DamageFormula formula{};
  // 4 * 0.01 * (100 - 20 * .12) = 3,904
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 3.903999805f);

  auto repeatativeEntry = Inventory::Entry(77382, 1, kExtraWornTrue);
  Equipment eq2;

  for (int i = 0; i < 70; i++) {
    eq2.inv.entries.push_back(repeatativeEntry);
  }

  // Total rating for worn armor: 10 * 70 = 700
  ac.SetEquipment(eq2);

  // Armor rating is 700 * 0.12% = 84%
  // But fMaxArmorRating = 80%
  // 4 * 0.01 * (100 - 80) = 4 * 0.2 = 0.8
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 0.7999999523f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("Formula is race-dependent for unarmed attack",
          "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  // Nord bu default
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  ac.SetEquipment(Equipment());

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x1f4; // unarmed attack

  {
    TES5DamageFormula formula{};
    REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 4.0f);
  }

  Appearance appearance;
  appearance.raceId = 0x13745; // KhajiitRace
  ac.SetAppearance(&appearance);
  ac.SetPercentages({ 1, 1, 1 });

  {
    TES5DamageFormula formula{};
    REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 10.0f);
  }

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("THORNSWOOD: a weapon at 120% condition does 5% more damage",
          "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage

  TES5DamageFormula formula{};
  auto with = [&](std::vector<Inventory::Entry> entries) {
    Equipment eq;
    eq.inv.entries = std::move(entries);
    ac.SetEquipment(eq);
    return formula.CalculateDamage(ac, ac, hitData);
  };

  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (120%)")) }) ==
          Catch::Approx(4.2f));
  // Held to 120, the way the gamemode holds it.
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (130%)")) }) ==
          Catch::Approx(4.2f));
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (119%)")) }) ==
          4.0f);
  REQUIRE(with({ Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger")) }) ==
          4.0f);
  REQUIRE(with({ Inventory::Entry(0x1397E, 1, kExtraWornTrue) }) == 4.0f);
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (12a%)")) }) ==
          4.0f);
  // Carried and not worn is not the weapon that struck.
  {
    Inventory::ExtraData carried;
    carried.name = "Iron Dagger (120%)";
    REQUIRE(with({ Inventory::Entry(0x1397E, 1, carried) }) == 4.0f);
  }
  // Another weapon at 120% does not lend this one its buff.
  REQUIRE(with({ Inventory::Entry(0x12EB7, 1,
                                  WornNamed("Iron Sword (120%)")) }) == 4.0f);
  // Two of the same dagger, one in each hand: HitData names no hand, so the
  // buff is given only when both are at 120%.
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (120%)")),
                 Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger", true)) }) ==
          4.0f);
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (120%)")),
                 Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (120%)", true)) }) ==
          Catch::Approx(4.2f));

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("THORNSWOOD: an armour piece at 120% condition gives 5% more of its "
          "rating",
          "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage, carried in no hand

  TES5DamageFormula formula{};

  // 77382 = 0x12e46: Iron Gauntlets, rating = 10
  // 77387 = 0x12e4b: Iron Boots, rating = 10
  Equipment plain;
  plain.inv.entries.push_back(Inventory::Entry(77382, 1, kExtraWornTrue));
  plain.inv.entries.push_back(Inventory::Entry(77387, 1, kExtraWornTrue));
  ac.SetEquipment(plain);
  // 4 * 0.01 * (100 - 20 * .12) = 3.904
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 3.903999805f);

  // The gauntlets at 120%: 10 * 1.05 + 10 = 20.5
  // 4 * 0.01 * (100 - 20.5 * .12) = 3.9016
  Equipment one;
  one.inv.entries.push_back(
    Inventory::Entry(77382, 1, WornNamed("Iron Gauntlets (120%)")));
  one.inv.entries.push_back(Inventory::Entry(77387, 1, kExtraWornTrue));
  ac.SetEquipment(one);
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) ==
          Catch::Approx(3.9016f));

  // Both at 120%: 21 -> 4 * 0.01 * (100 - 21 * .12) = 3.8992
  Equipment both;
  both.inv.entries.push_back(
    Inventory::Entry(77382, 1, WornNamed("Iron Gauntlets (120%)")));
  both.inv.entries.push_back(
    Inventory::Entry(77387, 1, WornNamed("Iron Boots (120%)")));
  ac.SetEquipment(both);
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) ==
          Catch::Approx(3.8992f));

  // Below 120% is a plain piece, and a piece carried at 120% gives nothing.
  Equipment worse;
  worse.inv.entries.push_back(
    Inventory::Entry(77382, 1, WornNamed("Iron Gauntlets (85%)")));
  worse.inv.entries.push_back(Inventory::Entry(77387, 1, kExtraWornTrue));
  {
    Inventory::ExtraData carried;
    carried.name = "Iron Helmet (120%)";
    worse.inv.entries.push_back(Inventory::Entry(77389, 1, carried));
  }
  ac.SetEquipment(worse);
  REQUIRE(formula.CalculateDamage(ac, ac, hitData) == 3.903999805f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("THORNSWOOD: a crude or masterwork weapon does a tenth less or more "
          "damage",
          "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage

  TES5DamageFormula formula{};
  auto with = [&](std::vector<Inventory::Entry> entries) {
    Equipment eq;
    eq.inv.entries = std::move(entries);
    ac.SetEquipment(eq);
    return formula.CalculateDamage(ac, ac, hitData);
  };

  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (Masterwork)")) }) ==
          Catch::Approx(4.4f));
  REQUIRE(
    with({ Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger (Crude)")) }) ==
    Catch::Approx(3.6f));
  // The condition goes after the quality, and both count.
  REQUIRE(with({ Inventory::Entry(
            0x1397E, 1, WornNamed("Iron Dagger (Masterwork) (120%)")) }) ==
          Catch::Approx(4.62f));
  REQUIRE(with({ Inventory::Entry(0x1397E, 1,
                                  WornNamed("Iron Dagger (Crude) (85%)")) }) ==
          Catch::Approx(3.6f));
  // A Master Weaver's mark in front changes nothing.
  REQUIRE(
    with({ Inventory::Entry(
      0x1397E, 1, WornNamed("Master-Lined Iron Dagger (Masterwork)")) }) ==
    Catch::Approx(4.4f));
  // Only the tier as the gamemode writes it.
  REQUIRE(with({ Inventory::Entry(
            0x1397E, 1, WornNamed("Iron Dagger(Masterwork)")) }) == 4.0f);
  REQUIRE(with({ Inventory::Entry(
            0x1397E, 1, WornNamed("Iron Dagger (Masterwork) x")) }) == 4.0f);
  REQUIRE(with({ Inventory::Entry(0x1397E, 1, WornNamed(" (Masterwork)")) }) ==
          4.0f);
  // Carried and not worn is not the weapon that struck.
  {
    Inventory::ExtraData carried;
    carried.name = "Iron Dagger (Masterwork)";
    REQUIRE(with({ Inventory::Entry(0x1397E, 1, carried) }) == 4.0f);
  }
  // Two of the same dagger, one in each hand: the hit counts as the weaker.
  REQUIRE(
    with({ Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger (Masterwork)")),
           Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger", true)) }) ==
    4.0f);
  REQUIRE(
    with({ Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger (Masterwork)")),
           Inventory::Entry(0x1397E, 1,
                            WornNamed("Iron Dagger (Crude)", true)) }) ==
    Catch::Approx(3.6f));
  REQUIRE(
    with({ Inventory::Entry(0x1397E, 1, WornNamed("Iron Dagger (Masterwork)")),
           Inventory::Entry(0x1397E, 1,
                            WornNamed("Iron Dagger (Masterwork)", true)) }) ==
    Catch::Approx(4.4f));

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("THORNSWOOD: a crude or masterwork armour piece gives a tenth less "
          "or more of its rating",
          "[TES5DamageFormula]")
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  HitData hitData;
  hitData.target = 0x14;
  hitData.aggressor = 0x14;
  hitData.source = 0x0001397E; // iron dagger 4 damage, carried in no hand

  TES5DamageFormula formula{};
  auto with = [&](const char* gauntlets) {
    Equipment eq;
    // 77382 = 0x12e46: Iron Gauntlets, rating = 10
    // 77387 = 0x12e4b: Iron Boots, rating = 10
    eq.inv.entries.push_back(Inventory::Entry(77382, 1, WornNamed(gauntlets)));
    eq.inv.entries.push_back(Inventory::Entry(77387, 1, kExtraWornTrue));
    ac.SetEquipment(eq);
    return formula.CalculateDamage(ac, ac, hitData);
  };

  // 11 + 10 = 21 -> 4 * 0.01 * (100 - 21 * .12) = 3.8992
  REQUIRE(with("Iron Gauntlets (Masterwork)") == Catch::Approx(3.8992f));
  // 9 + 10 = 19 -> 4 * 0.01 * (100 - 19 * .12) = 3.9088
  REQUIRE(with("Iron Gauntlets (Crude)") == Catch::Approx(3.9088f));
  // 10 * 1.05 * 1.1 + 10 = 21.55 -> 4 * 0.01 * (100 - 21.55 * .12) = 3.89656
  REQUIRE(with("Iron Gauntlets (Masterwork) (120%)") ==
          Catch::Approx(3.89656f));
  // A plain name is the piece as its record has it.
  REQUIRE(with("Iron Gauntlets") == 3.903999805f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}
