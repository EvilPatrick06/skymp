#include "MpChangeForms.h"
#include "TestUtils.hpp"
#include <algorithm>
#include <catch2/catch_all.hpp>

#include "MpObjectReference.h"
#include "PartOne.h"

PartOne& GetPartOne();
extern espm::Loader& GetEspmLoader();

namespace {
MpObjectReference* FindDrop(PartOne& partOne, uint32_t baseId)
{
  MpObjectReference* refr = nullptr;
  for (uint32_t id = 0xff000001; id < 0xff000100; ++id) {
    auto r = std::dynamic_pointer_cast<MpObjectReference>(
      partOne.worldState.LookupFormByIdNoLoad(id));
    if (r && r->GetBaseId() == baseId && !r->IsDeleted() &&
        !r->IsHarvested()) {
      refr = r.get();
    }
  }
  return refr;
}

// The change form as a save writes it and a load reads it back.
MpChangeForm ThroughJson(const MpChangeForm& changeForm)
{
  static simdjson::dom::parser parser;
  std::string dump = MpChangeForm::ToJson(changeForm).dump();
  simdjson::dom::element element = parser.parse(dump).value();
  return MpChangeForm::JsonToChangeForm(element);
}
}

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
  // MISC dunRagnTorstensKey in Skyrim.esm. Thornswood's keys are MISC
  // records; KEYM is not in espm::utils::IsItem, so it is not picked up.
  constexpr uint32_t houseKey = 0x000AADB7;
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

TEST_CASE("A drop keeps its extra and its delete time across a restart",
          "[DropItemTest]")
{
  // Thornswood #3498: the timer and the extra used to live in memory only,
  // so a drop saved before a restart came back plain and stayed for good.
  constexpr uint32_t ironDagger = 0x0001397E;
  constexpr uint32_t ironHelmet = 0x00012E4D;
  Inventory::ExtraData worn;
  worn.health = 0.85f;
  worn.name = "Iron Helmet (85%)";
  const Inventory::Entry piece(ironHelmet, 1, worn);

  auto time = std::chrono::system_clock::now();
  PartOne server;
  server.AttachEspm(&GetEspmLoader());
  server.worldState.SetTimerClock([&] { return time; });
  DoConnect(server, 0);
  server.CreateActor(0xff000000, { 1, 2, 3 }, 0, 0x3c);
  server.SetUserActor(0, 0xff000000);
  MpActor& ac = server.worldState.GetFormAt<MpActor>(0xff000000);
  ac.RemoveAllItems();
  ac.AddItem(ironDagger, 1);
  ac.AddItems({ piece });

  DoMessage(server, 0,
            nlohmann::json{ { "t", MsgType::DropItem },
                            { "baseId", ironHelmet },
                            { "count", 1 } });
  server.Tick();
  MpObjectReference* refr = FindDrop(server, ironHelmet);
  REQUIRE(refr != nullptr);
  const uint32_t refrId = refr->GetFormId();

  const auto saved = ThroughJson(refr->GetChangeForm());
  REQUIRE(saved.pickupExtra.has_value());
  REQUIRE(saved.pickupExtra->name == worn.name);
  REQUIRE(saved.deleteDatetime ==
          static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
              (time + std::chrono::seconds(120)).time_since_epoch())
              .count()));

  SECTION("A reference that was not dropped saves neither")
  {
    auto plain = refr->GetChangeForm();
    plain.pickupExtra = std::nullopt;
    plain.deleteDatetime = 0;
    const auto json = MpChangeForm::ToJson(plain);
    REQUIRE_FALSE(json.contains("pickupExtra"));
    REQUIRE_FALSE(json.contains("deleteDatetime"));
    const auto loaded = ThroughJson(plain);
    REQUIRE_FALSE(loaded.pickupExtra.has_value());
    REQUIRE(loaded.deleteDatetime == 0);
  }

  SECTION("Restarted before its time: still there, still the piece, then "
          "gone on time")
  {
    time += std::chrono::seconds(30);
    PartOne restarted;
    restarted.AttachEspm(&GetEspmLoader());
    restarted.worldState.SetTimerClock([&] { return time; });
    restarted.worldState.LoadChangeForm(saved,
                                        restarted.CreateFormCallbacks());
    auto& again = restarted.worldState.GetFormAt<MpObjectReference>(refrId);
    REQUIRE(again.GetPickupExtra().has_value());
    REQUIRE(again.GetPickupExtra()->name == worn.name);

    time += std::chrono::seconds(89);
    restarted.worldState.Tick();
    REQUIRE_FALSE(again.IsDeleted());

    time += std::chrono::seconds(2);
    restarted.worldState.Tick();
    REQUIRE(again.IsDeleted());
  }

  SECTION("Restarted before its time and picked up: the piece that was "
          "dropped")
  {
    PartOne restarted;
    restarted.AttachEspm(&GetEspmLoader());
    restarted.worldState.SetTimerClock([&] { return time; });
    restarted.worldState.LoadChangeForm(saved,
                                        restarted.CreateFormCallbacks());
    DoConnect(restarted, 0);
    restarted.CreateActor(0xff000000, { 1, 2, 3 }, 0, 0x3c);
    restarted.SetUserActor(0, 0xff000000);
    MpActor& picker = restarted.worldState.GetFormAt<MpActor>(0xff000000);
    picker.RemoveAllItems();
    restarted.worldState.GetFormAt<MpObjectReference>(refrId).Activate(
      picker);

    auto& entries = picker.GetInventory().entries;
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](auto& e) { return e.baseId == ironHelmet; });
    REQUIRE(it != entries.end());
    REQUIRE(it->count == 1);
    REQUIRE(it->EqualExceptCount(piece));
  }

  SECTION("Its time passed while the server was down: gone on the first "
          "tick")
  {
    time += std::chrono::minutes(10);
    PartOne restarted;
    restarted.AttachEspm(&GetEspmLoader());
    restarted.worldState.SetTimerClock([&] { return time; });
    restarted.worldState.LoadChangeForm(saved,
                                        restarted.CreateFormCallbacks());
    auto& again = restarted.worldState.GetFormAt<MpObjectReference>(refrId);
    restarted.worldState.Tick();
    REQUIRE(again.IsDeleted());
  }
}
