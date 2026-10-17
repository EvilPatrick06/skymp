#include "TestUtils.hpp"

TEST_CASE("UpdateEquipment", "[PartOne]")
{

  PartOne partOne;

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000ABC);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC));
  DoMessage(partOne, 0, jEquipment);
  partOne.Messages().clear();

  DoConnect(partOne, 1);
  partOne.CreateActor(0xffABCABC, { 11.f, 22.f, 33.f }, 180.f, 0x3c);
  partOne.SetUserActor(1, 0xffABCABC);

  // createActor should contain equipment
  auto res = FindRefrMessageIdx<CreateActorMessage>(partOne, 0);
  REQUIRE(res.filteredMessages.size() == 1);
  REQUIRE(res.filteredMessages[0].equipment.has_value());
  REQUIRE(res.filteredMessages[0].equipment->ToJson().dump() ==
          jEquipment["data"].dump());

  partOne.Messages().clear();

  DoMessage(partOne, 0, jEquipment);

  REQUIRE(partOne.Messages().size() == 2);
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::UpdateEquipment &&
                           m.j["idx"] == 0 && m.reliable && m.userId == 1 &&
                           m.j["data"] == jEquipment["data"];
                       }) != partOne.Messages().end());
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::UpdateEquipment &&
                           m.j["idx"] == 0 && m.reliable && m.userId == 0 &&
                           m.j["data"] == jEquipment["data"];
                       }) != partOne.Messages().end());
}

// THORNSWOOD #2262. onEquipItemAttempt: the gamemode refuses one item and the
// rest of the update stands.
#include "PartOneListener.h"
#include "gamemode_events/GameModeEvent.h"
#include <set>

namespace {
class RefuseEquipItems : public PartOneListener
{
public:
  void OnConnect(Networking::UserId) override {}
  void OnDisconnect(Networking::UserId) override {}
  void OnCustomPacket(Networking::UserId,
                      const simdjson::dom::element&) override
  {
  }
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (event.GetName() != std::string("onEquipItemAttempt")) {
      return true;
    }
    auto args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    asked.push_back(args.at(1).get<uint32_t>());
    return refuse.count(args.at(1).get<uint32_t>()) == 0;
  }

  std::set<uint32_t> refuse;
  std::vector<uint32_t> asked;
};

constexpr uint32_t kClub = 0x000a1b2c;
constexpr uint32_t kSword = 0x00012eb7;

nlohmann::json EquipTwo(bool clubWorn, bool swordWorn)
{
  auto j = jEquipment;
  j["data"]["inv"]["entries"] = nlohmann::json::array(
    { { { "baseId", kClub }, { "count", 1 }, { "worn", clubWorn } },
      { { "baseId", kSword }, { "count", 1 }, { "worn", swordWorn } } });
  return j;
}

Inventory::Worn WornOf(MpActor& actor, uint32_t baseId)
{
  for (const auto& entry : actor.GetEquipment().inv.entries) {
    if (entry.baseId == baseId) {
      return entry.GetWorn();
    }
  }
  return Inventory::Worn::None;
}

bool HasEntry(MpActor& actor, uint32_t baseId)
{
  for (const auto& entry : actor.GetEquipment().inv.entries) {
    if (entry.baseId == baseId) {
      return true;
    }
  }
  return false;
}

size_t UnequipSnippetsFor(PartOne& partOne, uint32_t baseId)
{
  partOne.Tick();
  size_t n = 0;
  for (auto& m : partOne.Messages()) {
    if (m.j["t"] == MsgType::SpSnippet && m.j["function"] == "UnequipItem" &&
        m.userId == 0 && m.j["arguments"][0]["formId"] == baseId) {
      n++;
    }
  }
  return n;
}

struct EquipFixture
{
  EquipFixture()
  {
    partOne.worldState.equipmentSlotCheckEnabled = false;
    DoConnect(partOne, 0);
    partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
    partOne.SetUserActor(0, 0xff000ABC);
    GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC));
    DoConnect(partOne, 1);
    partOne.CreateActor(0xffABCABC, { 11.f, 22.f, 33.f }, 180.f, 0x3c);
    partOne.SetUserActor(1, 0xffABCABC);
    actor().AddItem(kClub, 1);
    actor().AddItem(kSword, 1);
    partOne.AddListener(gamemode);
    gamemode->refuse.insert(kClub);
    partOne.Messages().clear();
  }
  MpActor& actor() { return partOne.worldState.GetFormAt<MpActor>(0xff000ABC); }

  PartOne partOne;
  std::shared_ptr<RefuseEquipItems> gamemode =
    std::make_shared<RefuseEquipItems>();
};
}

TEST_CASE("onEquipItemAttempt is off by default", "[PartOne][2262]")
{
  EquipFixture f;
  REQUIRE(f.partOne.worldState.equipItemAttemptEnabled == false);

  DoMessage(f.partOne, 0, EquipTwo(true, true));

  REQUIRE(f.gamemode->asked.empty());
  REQUIRE(WornOf(f.actor(), kClub) != Inventory::Worn::None);
  REQUIRE(UnequipSnippetsFor(f.partOne, kClub) == 0);
}

TEST_CASE("A refused item alone is taken off; the rest of the update stands",
          "[PartOne][2262]")
{
  EquipFixture f;
  f.partOne.worldState.equipItemAttemptEnabled = true;

  // A first update after spawning, the login path: nothing is on the
  // server's record yet, and the game put both back on.
  DoMessage(f.partOne, 0, EquipTwo(true, true));

  REQUIRE(f.gamemode->asked.size() == 2);
  // Kept in the pack, not worn; the sword stays in hand.
  REQUIRE(HasEntry(f.actor(), kClub));
  REQUIRE(WornOf(f.actor(), kClub) == Inventory::Worn::None);
  REQUIRE(WornOf(f.actor(), kSword) != Inventory::Worn::None);
  // The owner's game is told to take it off, once.
  REQUIRE(UnequipSnippetsFor(f.partOne, kClub) == 1);
  REQUIRE(UnequipSnippetsFor(f.partOne, kSword) == 0);
  // The neighbour sees what was stored, never the club in hand.
  bool neighbourSawStored = false;
  for (auto& m : f.partOne.Messages()) {
    if (m.j["t"] == MsgType::UpdateEquipment && m.userId == 1) {
      for (auto& e : m.j["data"]["inv"]["entries"]) {
        if (e["baseId"] == kClub) {
          REQUIRE((!e.contains("worn") || e["worn"] == false));
          neighbourSawStored = true;
        }
      }
    }
  }
  REQUIRE(neighbourSawStored);
}

TEST_CASE("An allowed item is stored and forwarded as the client sent it",
          "[PartOne][2262]")
{
  EquipFixture f;
  f.partOne.worldState.equipItemAttemptEnabled = true;
  f.gamemode->refuse.clear();

  auto j = EquipTwo(true, true);
  DoMessage(f.partOne, 0, j);

  REQUIRE(f.gamemode->asked.size() == 2);
  REQUIRE(WornOf(f.actor(), kClub) != Inventory::Worn::None);
  REQUIRE(UnequipSnippetsFor(f.partOne, kClub) == 0);
  REQUIRE(std::find_if(f.partOne.Messages().begin(),
                       f.partOne.Messages().end(), [&](auto m) {
                         return m.j["t"] == MsgType::UpdateEquipment &&
                           m.userId == 1 && m.j["data"] == j["data"];
                       }) != f.partOne.Messages().end());
}

TEST_CASE("An item already worn on the server's record is not asked again",
          "[PartOne][2262]")
{
  EquipFixture f;
  f.partOne.worldState.equipItemAttemptEnabled = true;
  f.gamemode->refuse.clear();

  DoMessage(f.partOne, 0, EquipTwo(false, true));
  REQUIRE(f.gamemode->asked == std::vector<uint32_t>{ kSword });

  // Now the club refused: putting it on asks about the club only, and the
  // sword, already on, is neither asked about nor taken off.
  f.gamemode->refuse.insert(kClub);
  f.partOne.Messages().clear();
  DoMessage(f.partOne, 0, EquipTwo(true, true));
  REQUIRE(f.gamemode->asked == std::vector<uint32_t>{ kSword, kClub });
  REQUIRE(WornOf(f.actor(), kSword) != Inventory::Worn::None);
  REQUIRE(WornOf(f.actor(), kClub) == Inventory::Worn::None);
  REQUIRE(UnequipSnippetsFor(f.partOne, kClub) == 1);
}
