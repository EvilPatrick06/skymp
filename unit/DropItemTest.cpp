#include "MpChangeForms.h"
#include "TestUtils.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>

#include "MpObjectReference.h"
#include "PartOne.h"

PartOne& GetPartOne();

TEST_CASE("Dropping an item", "[DropItemTest]")
{
  auto& partOne = GetPartOne();
  // an iron dagger
  constexpr uint32_t ironDagger = 0x0001397E;
  constexpr uint32_t ironSword = 0x00012EB7;
  DoConnect(partOne, 0);

  partOne.CreateActor(0xff000000, { 1, 2, 3 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);

  MpActor& ac = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  ac.RemoveAllItems();
  REQUIRE(ac.GetInventory().GetTotalItemCount() == 0);
  ac.AddItem(ironDagger, 1);
  REQUIRE(ac.GetInventory().GetTotalItemCount() == 1);
  partOne.Messages().clear();
  REQUIRE(partOne.Messages().size() == 0);
  DoMessage(partOne, 0,
            nlohmann::json{ { "t", MsgType::DropItem },
                            { "baseId", ironDagger },
                            { "count", 1 } });
  // 1 message from here and another 1 is comming from actionListener
  partOne.Tick();
  REQUIRE(partOne.Messages().size() == 2);
  REQUIRE(ac.GetInventory().GetItemCount(ironDagger) == 0);
  MpObjectReference& refr =
    partOne.worldState.GetFormAt<MpObjectReference>(0xff000001);
  REQUIRE(refr.GetBaseId() == ironDagger);
  REQUIRE(refr.GetPos().x == 1.f);
  REQUIRE(refr.GetPos().y == 2.f);
  REQUIRE(refr.GetPos().z == 3.f);
  ac.AddItem(ironSword, 5);
  partOne.Messages().clear();
  REQUIRE(partOne.Messages().size() == 0);
  DoMessage(partOne, 0,
            nlohmann::json{ { "t", MsgType::DropItem },
                            { "baseId", ironSword },
                            { "count", 5 } });
  partOne.Tick();
  REQUIRE(partOne.Messages().size() == 2);
  REQUIRE(ac.GetInventory().GetItemCount(ironSword) == 0);
}

TEST_CASE("A dropped piece keeps its extra through the pickup",
          "[DropItemTest]")
{
  auto& partOne = GetPartOne();
  constexpr uint32_t ironDagger = 0x0001397E;
  constexpr uint32_t ironSword = 0x00012EB7;
  constexpr uint32_t ironHelmet = 0x00012E4D;
  // KEYM WhiterunYsoldasHouseKey in Skyrim.esm
  constexpr uint32_t houseKey = 0x00093B13;
  DoConnect(partOne, 0);

  partOne.CreateActor(0xff000000, { 1, 2, 3 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  MpActor& ac = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  Inventory::ExtraData named;
  named.name = "Key to the Thornwood house";

  Inventory::ExtraData enchanted;
  enchanted.enchantmentId = 0xff000abc;
  enchanted.maxCharge = 800.f;
  enchanted.chargePercent = 40.f;
  enchanted.name = "Iron Sword of Frost";

  Inventory::ExtraData worn;
  worn.health = 0.85f;
  worn.name = "Iron Helmet (85%)";

  std::vector<Inventory::Entry> pieces = {
    Inventory::Entry(houseKey, 1, named),
    Inventory::Entry(ironSword, 1, enchanted),
    Inventory::Entry(ironHelmet, 1, worn)
  };

  for (auto& piece : pieces) {
    ac.RemoveAllItems();
    // a plain item of another base, so the pack is not just the piece
    ac.AddItem(ironDagger, 1);
    ac.AddItems({ piece });

    DoMessage(partOne, 0,
              nlohmann::json{ { "t", MsgType::DropItem },
                              { "baseId", piece.baseId },
                              { "count", 1 } });
    partOne.Tick();
    REQUIRE(ac.GetInventory().GetItemCount(piece.baseId) == 0);
    REQUIRE(ac.GetInventory().GetItemCount(ironDagger) == 1);

    MpObjectReference* refr = nullptr;
    for (uint32_t id = 0xff000001; id < 0xff000100; ++id) {
      auto r = std::dynamic_pointer_cast<MpObjectReference>(
        partOne.worldState.LookupFormById(id));
      if (r && r->GetBaseId() == piece.baseId && !r->IsDeleted() &&
          !r->IsHarvested()) {
        refr = r.get();
      }
    }
    REQUIRE(refr != nullptr);
    REQUIRE(refr->GetPickupExtra().has_value());

    refr->Activate(ac);

    auto& entries = ac.GetInventory().entries;
    auto it = std::find_if(entries.begin(), entries.end(), [&](auto& e) {
      return e.baseId == piece.baseId;
    });
    REQUIRE(it != entries.end());
    REQUIRE(it->count == 1);
    REQUIRE(it->EqualExceptCount(piece));
  }
}

TEST_CASE("A drop takes a plain stack before a named one", "[DropItemTest]")
{
  auto& partOne = GetPartOne();
  constexpr uint32_t ironDagger = 0x0001397E;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 1, 2, 3 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  MpActor& ac = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  Inventory::ExtraData named;
  named.name = "Named Dagger";
  ac.RemoveAllItems();
  ac.AddItems({ Inventory::Entry(ironDagger, 1, named) });
  ac.AddItems({ Inventory::Entry(ironDagger, 2) });

  DoMessage(partOne, 0,
            nlohmann::json{ { "t", MsgType::DropItem },
                            { "baseId", ironDagger },
                            { "count", 1 } });
  partOne.Tick();
  REQUIRE(ac.GetInventory().GetItemCount(ironDagger) == 2);
  auto& entries = ac.GetInventory().entries;
  REQUIRE(std::any_of(entries.begin(), entries.end(), [&](auto& e) {
    return e.baseId == ironDagger && e.name == named.name && e.count == 1;
  }));
}
