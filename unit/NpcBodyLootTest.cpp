#include "TestUtils.hpp"
#include "ActionListener.h"
#include "archives/BitStreamInputArchive.h"
#include "archives/BitStreamOutputArchive.h"
#include "script_classes/PapyrusActor.h"
#include "script_classes/PapyrusObjectReference.h"
#include "script_objects/EspmGameObject.h"
#include <cstring>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kNpc = 0x39fe4;
constexpr uint32_t kHuman = 0xff031079;
constexpr uint32_t kAssigned = 0x64b2e;

struct NpcLootFixture
{
  PartOne& p = GetPartOne();
  NpcLootFixture()
  {
    p.worldState.npcEnabled = true;
    p.worldState.npcAllowEssential = true;
    p.worldState.npcAllowCrimeFaction = true;
    p.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
    auto& npc = Npc();
    p.CreateActor(kHuman, npc.GetPos(), 0,
                  npc.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
    Human().SetInventory(Inventory());
  }
  MpActor& Npc() { return p.worldState.GetFormAt<MpActor>(kNpc); }
  MpActor& Human() { return p.worldState.GetFormAt<MpActor>(kHuman); }
  Inventory::Entry Assigned()
  {
    Inventory::ExtraData extra;
    extra.name = "Assigned body goods";
    return Inventory::Entry(kAssigned, 2, extra);
  }
};
}

TEST_CASE("NPC body transfers give assigned goods without plugin equipment",
          "[NpcBodyLoot][espm]")
{
  NpcLootFixture f;
  const auto gear = f.Npc().GetEquipment();
  REQUIRE_FALSE(gear.inv.IsEmpty());
  f.Npc().AddItems({f.Assigned()});
  SECTION("A living NPC cannot give away inherited gear") {}
  SECTION("Death does not add vanilla body drops")
  {
    f.Npc().SetIsDead(true);
  }
  f.Npc().RemoveAllItems(&f.Human());
  REQUIRE(f.Human().GetInventory().entries ==
          std::vector<Inventory::Entry>{f.Assigned()});
  REQUIRE(f.Npc().GetEquipment() == gear);
  for (const auto& equipped : f.Npc().GetEquippedWeapon())
    if (equipped)
      REQUIRE(f.Npc().GetInventory().HasItem(equipped->baseId));
}

TEST_CASE("NPC native death and return preserve deliberately assigned inventory",
          "[NpcBodyLoot][espm]")
{
  NpcLootFixture f;
  f.Npc().AddItems({f.Assigned()});
  const auto before = f.Npc().GetInventory();
  f.Npc().SetIsDead(true);
  REQUIRE(f.Npc().GetInventory() == before);
  f.Npc().SetIsDead(false);
  REQUIRE(f.Npc().GetInventory() == before);
  f.Npc().RemoveAllItems(&f.Human());
  REQUIRE(f.Human().GetInventory().entries ==
          std::vector<Inventory::Entry>{f.Assigned()});
}

TEST_CASE("NPC inherited merchant gold is not stock or a body reward",
          "[NpcBodyLoot][espm]")
{
  auto& p = GetPartOne();
  p.worldState.npcEnabled = true;
  p.worldState.npcAllowEssential = true;
  p.worldState.npcAllowCrimeFaction = true;
  auto& trader = p.worldState.GetFormAt<MpActor>(0x1a66e);
  REQUIRE(trader.GetInventory().GetItemCount(0xf) == 0);
  trader.AddItem(0xf, 250);
  REQUIRE(trader.GetInventory().GetItemCount(0xf) == 250);
}

TEST_CASE("Draugr death and timed respawn cannot insert vanilla death items",
          "[NpcBodyLoot][Respawn][espm]")
{
  auto& p = GetPartOne();
  p.worldState.npcEnabled = true;
  p.worldState.npcAllowEssential = true;
  p.worldState.npcAllowCrimeFaction = true;
  p.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
  auto& draugr = p.worldState.GetFormAt<MpActor>(0x1006ac);
  SECTION("A hosted body") {}
  SECTION("A server controlled body") { draugr.SetServerControlled(true); }
  draugr.AddItem(kAssigned, 2);
  const auto before = draugr.GetInventory();
  draugr.SetRespawnTime(0);
  draugr.SetIsDead(true);
  REQUIRE(draugr.GetInventory() == before);
  draugr.RespawnWithDelay(false);
  p.Tick();
  REQUIRE_FALSE(draugr.IsDead());
  REQUIRE(draugr.GetInventory() == before);
}

TEST_CASE("NPC native item requests cannot transfer inherited gear",
          "[NpcBodyLoot][espm]")
{
  NpcLootFixture f;
  const auto before = f.Npc().GetInventory();
  REQUIRE_FALSE(before.IsEmpty());
  auto inherited = before.entries.front();
  inherited.count = 1;
  SECTION("The exact equipment stack cannot be transferred")
  {
    REQUIRE_THROWS(f.Npc().RemoveItems({inherited}, &f.Human()));
    REQUIRE(f.Npc().GetInventory() == before);
    REQUIRE(f.Human().GetInventory().IsEmpty());
  }
  SECTION("The inherited stack cannot be dropped into the world")
  {
    REQUIRE_THROWS(f.Npc().DropItem(inherited.baseId, inherited));
    REQUIRE(f.Npc().GetInventory() == before);
    REQUIRE(f.Human().GetInventory().IsEmpty());
  }
  SECTION("An explicit assignment of the same base remains transferable")
  {
    f.Npc().AddItem(inherited.baseId, 3);
    f.Npc().RemoveItem(inherited.baseId, 3, &f.Human());
    REQUIRE(f.Npc().GetInventory() == before);
    REQUIRE(f.Human().GetInventory().GetItemCount(inherited.baseId) == 3);
    REQUIRE_THROWS(f.Npc().RemoveItem(inherited.baseId, 1, &f.Human()));
    REQUIRE(f.Npc().GetInventory() == before);
    REQUIRE(f.Human().GetInventory().GetItemCount(inherited.baseId) == 3);
  }
  SECTION("A mixed request refuses before moving any assigned goods")
  {
    f.Npc().AddItems({f.Assigned()});
    const auto assignedBefore = f.Npc().GetInventory();
    REQUIRE_THROWS(f.Npc().RemoveItems({f.Assigned(), inherited}, &f.Human()));
    REQUIRE(f.Npc().GetInventory() == assignedBefore);
    REQUIRE(f.Human().GetInventory().IsEmpty());
  }
}

TEST_CASE("NPC equipment origin survives saves without merging with assignments",
          "[NpcBodyLoot][Inventory][espm]")
{
  NpcLootFixture f;
  auto inherited = f.Npc().GetInventory().entries.front();
  REQUIRE(inherited.npcEquipment.value_or(false));
  Inventory::Entry assigned(inherited.baseId, 2);
  Inventory combined;
  combined.AddItems({inherited, assigned});
  REQUIRE(combined.entries.size() == 2);
  auto saved = Inventory::FromJson(combined.ToJson());
  REQUIRE(saved == combined);
  f.Npc().SetInventory(saved);
  f.Npc().RemoveAllItems(&f.Human());
  REQUIRE(f.Npc().GetInventory().entries ==
          std::vector<Inventory::Entry>{inherited});
  REQUIRE(f.Human().GetInventory().entries ==
          std::vector<Inventory::Entry>{assigned});
  Inventory::Entry explicitFalse = assigned;
  explicitFalse.npcEquipment = false;
  REQUIRE(explicitFalse == assigned);
}

TEST_CASE("NPC equipment metadata does not change the inventory wire format",
          "[NpcBodyLoot][Inventory]")
{
  Inventory::Entry entry(0x1397e, 1);
  SECTION("An ordinary item") {}
  SECTION("An inherited equipped item")
  {
    entry.npcEquipment = true;
    entry.SetWorn(Inventory::Worn::Right);
    entry.name = "Guard sword";
    entry.health = 0.85f;
  }
  SLNet::BitStream expected, actual;
  BitStreamOutputArchive legacy(expected), current(actual);
  // The complete Entry layout without npcEquipment, including every optional
  // field. made (Thornswood #1649) is on the wire; npcEquipment is not.
  legacy.Serialize("baseId", entry.baseId).Serialize("count", entry.count);
  legacy.Serialize("health", entry.health)
    .Serialize("enchantmentId", entry.enchantmentId)
    .Serialize("maxCharge", entry.maxCharge)
    .Serialize("removeEnchantmentOnUnequip", entry.removeEnchantmentOnUnequip)
    .Serialize("chargePercent", entry.chargePercent)
    .Serialize("name", entry.name).Serialize("soul", entry.soul)
    .Serialize("poisonId", entry.poisonId).Serialize("poisonCount", entry.poisonCount)
    .Serialize("worn", entry.worn_).Serialize("wornLeft", entry.wornLeft)
    .Serialize("made", entry.made);
  entry.Serialize(current);
  REQUIRE(actual.GetNumberOfBitsUsed() == expected.GetNumberOfBitsUsed());
  REQUIRE(std::memcmp(actual.GetData(), expected.GetData(),
                      actual.GetNumberOfBytesUsed()) == 0);
  Inventory::Entry received;
  BitStreamInputArchive input(expected);
  received.Serialize(input);
  REQUIRE_FALSE(received.npcEquipment.has_value());
  entry.npcEquipment.reset();
  REQUIRE(received == entry);
}

TEST_CASE("Papyrus equipping inherited NPC gear creates no loot copy",
          "[NpcBodyLoot][Papyrus][espm]")
{
  NpcLootFixture f;
  const auto before = f.Npc().GetInventory();
  const auto weapons = f.Npc().GetEquippedWeapon();
  REQUIRE(weapons[1].has_value());
  const auto lookup = f.p.GetEspm().GetBrowser().LookupById(weapons[1]->baseId);
  VarValue item(std::make_shared<EspmGameObject>(lookup));
  PapyrusActor papyrus;
  for (int i = 0; i < 2; ++i) {
    papyrus.EquipItem(f.Npc().ToVarValue(), {item, VarValue(false), VarValue(false)});
    papyrus.EquipItemEx(f.Npc().ToVarValue(),
      {item, VarValue(int32_t(1)), VarValue(false), VarValue(false)});
  }
  REQUIRE(f.Npc().GetInventory() == before);
  f.Npc().RemoveAllItems(&f.Human());
  REQUIRE(f.Human().GetInventory().IsEmpty());
  REQUIRE(f.Npc().GetInventory() == before);
}

TEST_CASE("An NPC can fight with inherited gear after an attempted loot transfer",
          "[NpcBodyLoot][ServerNpcCombat][espm]")
{
  NpcLootFixture f;
  DoConnect(f.p, 0);
  f.p.SetUserActor(0, kHuman);
  f.Npc().SetServerControlled(true);
  f.Npc().SetPercentages({1, 1, 1});
  f.Human().SetPercentages({1, 1, 1});
  const auto gear = f.Npc().GetEquipment();
  REQUIRE(f.Npc().GetEquippedWeapon()[1].has_value());
  REQUIRE(f.Npc().GetEquippedWeapon()[1]->npcEquipment.value_or(false));
  f.Npc().RemoveAllItems(&f.Human());
  REQUIRE(f.Human().GetInventory().IsEmpty());
  REQUIRE(f.Npc().GetEquipment() == gear);
  REQUIRE(f.p.GetActionListener().ServerNpcAttack(kNpc, kHuman));
  REQUIRE(f.Human().GetActorValues().healthPercentage < 1.f);
}

TEST_CASE("Papyrus removes only eligible assigned copies of inherited NPC items",
          "[NpcBodyLoot][Papyrus][espm]")
{
  NpcLootFixture f;
  const auto before = f.Npc().GetInventory();
  const auto weapon = f.Npc().GetEquippedWeapon()[1];
  REQUIRE(weapon.has_value());
  int count = -1;
  Inventory::Entry assigned(weapon->baseId, 3);
  SECTION("Remove all copies") {}
  SECTION("Clamp an oversized request") { count = 100; }
  SECTION("Keep assigned metadata") { assigned.name = "Assigned guard sword"; }
  f.Npc().AddItems({assigned});
  const auto lookup = f.p.GetEspm().GetBrowser().LookupById(weapon->baseId);
  VarValue item(std::make_shared<EspmGameObject>(lookup));
  PapyrusObjectReference papyrus;
  REQUIRE_NOTHROW(papyrus.RemoveItem(f.Npc().ToVarValue(),
    {item, VarValue(int32_t(count)), VarValue(true), f.Human().ToVarValue()}));
  REQUIRE(f.Npc().GetInventory() == before);
  REQUIRE(f.Human().GetInventory().entries ==
          std::vector<Inventory::Entry>{assigned});
}
