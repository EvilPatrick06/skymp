// THORNSWOOD PATCH (Thornswood #1649). A brewed potion is kept as what is in
// it, and drinking one does what is in it.
//
// The game makes a brewed bottle as a runtime form (above 0xff000000) only
// the brewing session has. The server used to keep that id: the next
// session's game found nothing under it, the server could not tell what
// drinking it did (so it was never taken out of the pack and onEatItem never
// fired), and the gamemode's inventory calls threw on it. Now a brew carries
// what the client read off the bottle, CraftService::MadePotion checks it
// against the ingredients, and the pack holds Inventory::Made under
// Inventory::MadeBaseId.
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ActionListener.h"
#include "CraftItemMessage.h"
#include "CraftService.h"
#include "OnEquipMessage.h"
#include "PartOneListener.h"
#include "gamemode_events/GameModeEvent.h"
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kForge = 0x1ad6e;
constexpr uint32_t kBlueMountainFlower = 0x77e1c; // 3eb15 3eb25 3eaf3 73f2b
constexpr uint32_t kWheat = 0x4b0ba;              // 3eb15 3eaf3 73f2c 10de5f
constexpr uint32_t kRestoreHealth = 0x3eb15;
constexpr uint32_t kFortifyHealth = 0x3eaf3;
constexpr uint32_t kResistMagic = 0x39e51;
// What the brewing game called its bottle: a runtime id, nothing more.
constexpr uint32_t kRuntimeBottle = 0xff000a01;

Inventory::Made Healing(float magnitude = 12.5f)
{
  Inventory::Made made;
  made.name = "Potion of Restore Health";
  made.effects.push_back({ kRestoreHealth, magnitude, 0, 0 });
  made.effects.push_back({ kFortifyHealth, 8.f, 0, 60 });
  return made;
}

CraftItemMessage Brew(std::optional<Inventory::Made> bottle)
{
  CraftItemMessage msg;
  msg.data.craftInputObjects =
    Inventory().AddItem(kBlueMountainFlower, 1).AddItem(kWheat, 1);
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kRuntimeBottle;
  msg.data.resultPotion = std::move(bottle);
  return msg;
}

MpActor& MakeCrafter(PartOne& p)
{
  auto& refr = p.worldState.GetFormAt<MpObjectReference>(kForge);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, refr.GetPos(), 0,
                refr.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  return p.worldState.GetFormAt<MpActor>(0xff000000);
}

// Records what onCraft and onEatItem were told, while armed (PartOne is
// shared and keeps every listener).
class Watch : public PartOneListener
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
    if (!armed) {
      return true;
    }
    auto args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    if (event.GetName() == std::string("onCraft")) {
      crafts++;
      craftedId = args.at(1).get<uint32_t>();
    } else if (event.GetName() == std::string("onEatItem")) {
      eats++;
      eatenId = args.at(1).get<uint32_t>();
    }
    return true;
  }

  bool armed = false;
  int crafts = 0;
  uint32_t craftedId = 0;
  int eats = 0;
  uint32_t eatenId = 0;
};

const Inventory::Entry* MadeEntry(MpActor& ac)
{
  for (auto& entry : ac.GetInventory().entries) {
    if (entry.made) {
      return &entry;
    }
  }
  return nullptr;
}
}

TEST_CASE("A made bottle has one id for its contents and survives the "
          "archives (Thornswood #1649)",
          "[MadePotion]")
{
  const auto a = Healing();
  const uint32_t id = Inventory::MadeBaseId(a);
  REQUIRE(id >= Inventory::kMadeIdFirst);
  REQUIRE(Inventory::MadeBaseId(Healing()) == id);
  REQUIRE(Inventory::MadeBaseId(Healing(13.f)) != id);

  auto poison = a;
  poison.poison = true;
  REQUIRE(Inventory::MadeBaseId(poison) != id);

  // Rounded to two decimals: what the game reads as 12.5000001 is 12.5.
  auto noisy = Healing(12.5000001f);
  Inventory::Round(noisy);
  REQUIRE(noisy == a);
  REQUIRE(Inventory::MadeBaseId(noisy) == id);
  REQUIRE(Inventory::Canonical(a) ==
          "potion|Potion of Restore Health|3eb15:12.50:0:0|3eaf3:8.00:0:60");

  // Kept in a pack, written out and read back.
  Inventory::ExtraData extra;
  extra.made = a;
  Inventory inv;
  inv.AddItems({ Inventory::Entry(id, 2, extra) });
  auto j = inv.ToJson();
  REQUIRE(j["entries"][0]["made"]["effects"][1]["duration"] == 60);
  auto back = Inventory::FromJson(j);
  REQUIRE(back == inv);
  REQUIRE(back.entries[0].made == a);
  simdjson::dom::parser parser;
  const std::string dumped = j.dump();
  simdjson::dom::element simd = parser.parse(dumped).value();
  REQUIRE(Inventory::FromJson(simd) == inv);

  // Two different brews do not stack, two of one brew do.
  Inventory::ExtraData other;
  other.made = Healing(20.f);
  REQUIRE(!Inventory::Entry(id, 1, extra).EqualExceptCount(
    Inventory::Entry(id, 1, other)));
  inv.AddItems({ Inventory::Entry(id, 1, extra) });
  REQUIRE(inv.entries.size() == 1);
  REQUIRE(inv.entries[0].count == 3);
}

TEST_CASE("MadePotion believes only a bottle the ingredients could have made "
          "(Thornswood #1649)",
          "[MadePotion][espm]")
{
  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();
  auto& br = p.GetEspm().GetBrowser();
  const auto inputs =
    Inventory().AddItem(kBlueMountainFlower, 1).AddItem(kWheat, 1);
  const std::vector<uint32_t> effects = { kRestoreHealth, kFortifyHealth };

  auto ok = craftService->MadePotion(br, inputs, effects, Healing());
  REQUIRE(ok.has_value());
  REQUIRE(*ok == Healing());

  // The game may list them strongest first; the order is kept.
  auto swapped = Healing();
  std::swap(swapped.effects[0], swapped.effects[1]);
  auto okSwapped = craftService->MadePotion(br, inputs, effects, swapped);
  REQUIRE(okSwapped.has_value());
  REQUIRE(okSwapped->effects[0].effectId == kFortifyHealth);

  // No description: a client from before #1649.
  REQUIRE(!craftService->MadePotion(br, inputs, effects, std::nullopt));

  // Effects the ingredients do not share, one missing, or one twice.
  auto wrong = Healing();
  wrong.effects[1].effectId = kResistMagic;
  REQUIRE(!craftService->MadePotion(br, inputs, effects, wrong));
  auto missing = Healing();
  missing.effects.pop_back();
  REQUIRE(!craftService->MadePotion(br, inputs, effects, missing));
  auto twice = Healing();
  twice.effects[1] = twice.effects[0];
  REQUIRE(!craftService->MadePotion(br, inputs, effects, twice));

  // Numbers nothing could brew.
  REQUIRE(!craftService->MadePotion(br, inputs, effects, Healing(1e6f)));
  REQUIRE(!craftService->MadePotion(br, inputs, effects, Healing(-1.f)));
  REQUIRE(!craftService->MadePotion(
    br, inputs, effects, Healing(std::numeric_limits<float>::quiet_NaN())));
  auto forever = Healing();
  forever.effects[1].duration = 1000000;
  REQUIRE(!craftService->MadePotion(br, inputs, effects, forever));
  auto longName = Healing();
  longName.name = std::string(300, 'x');
  REQUIRE(!craftService->MadePotion(br, inputs, effects, longName));

  // Nothing shared: nothing to believe.
  REQUIRE(!craftService->MadePotion(br, inputs, {}, Healing()));
}

TEST_CASE("A brew lands in the pack by its contents, and onCraft is told that "
          "id (Thornswood #1649)",
          "[MadePotion][espm]")
{
  PartOne& p = GetPartOne();
  auto watch = std::make_shared<Watch>();
  p.AddListener(watch);
  auto& ac = MakeCrafter(p);
  ac.AddItem(kBlueMountainFlower, 2);
  ac.AddItem(kWheat, 2);

  RawMessageData msgData;
  msgData.userId = 0;
  const uint32_t id = Inventory::MadeBaseId(Healing());

  watch->armed = true;
  p.GetActionListener().OnCraftItem(msgData, Brew(Healing(12.5000001f)));
  watch->armed = false;

  REQUIRE(watch->crafts == 1);
  CHECK(watch->craftedId == id);
  CHECK(ac.GetInventory().GetItemCount(kRuntimeBottle) == 0);
  CHECK(ac.GetInventory().GetItemCount(id) == 1);
  auto held = MadeEntry(ac);
  REQUIRE(held);
  CHECK(held->baseId == id);
  CHECK(*held->made == Healing());
  CHECK(ac.GetInventory().GetItemCount(kBlueMountainFlower) == 1);

  // The same brew again stacks with the first.
  p.GetActionListener().OnCraftItem(msgData, Brew(Healing()));
  CHECK(ac.GetInventory().GetItemCount(id) == 2);
  size_t madeEntries = 0;
  for (auto& entry : ac.GetInventory().entries) {
    madeEntries += entry.made ? 1 : 0;
  }
  CHECK(madeEntries == 1);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A brew without a believable bottle is refused before the gamemode "
          "is asked, and the ingredients stay (Thornswood #1649)",
          "[MadePotion][espm]")
{
  PartOne& p = GetPartOne();
  for (auto bottle :
       { std::optional<Inventory::Made>(), std::optional(Healing(1e6f)) }) {
    auto watch = std::make_shared<Watch>();
    p.AddListener(watch);
    auto& ac = MakeCrafter(p);
    ac.AddItem(kBlueMountainFlower, 1);
    ac.AddItem(kWheat, 1);
    const Inventory before = ac.GetInventory();

    RawMessageData msgData;
    msgData.userId = 0;
    watch->armed = true;
    REQUIRE_NOTHROW(p.GetActionListener().OnCraftItem(msgData, Brew(bottle)));
    watch->armed = false;

    CHECK(watch->crafts == 0);
    CHECK(ac.GetInventory() == before);
    CHECK(ac.GetInventory().GetItemCount(kRuntimeBottle) == 0);

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }
}

TEST_CASE("Drinking a made bottle spends it, raises onEatItem and does what "
          "is in it (Thornswood #1649)",
          "[MadePotion][Restoration]")
{
  using namespace std::chrono_literals;
  PartOne& p = GetPartOne();
  auto watch = std::make_shared<Watch>();
  p.AddListener(watch);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  ac.SetLastAttributesPercentagesUpdate(std::chrono::steady_clock::now() -
                                        10s);
  ac.SetPercentages({ 0.1f, 0.f, 0.f });

  Inventory::Made strong;
  strong.name = "Potion of Restore Health";
  strong.effects.push_back({ kRestoreHealth, 100.f, 0, 0 });
  const uint32_t id = Inventory::MadeBaseId(strong);
  Inventory::ExtraData extra;
  extra.made = strong;
  ac.AddItems({ Inventory::Entry(id, 2, extra) });

  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  OnEquipMessage msg;
  msg.baseId = id;
  watch->armed = true;
  p.GetActionListener().OnEquip(rawMsgData, msg);
  watch->armed = false;

  CHECK(watch->eats == 1);
  CHECK(watch->eatenId == id);
  CHECK(ac.GetInventory().GetItemCount(id) == 1);
  CHECK(ac.GetChangeForm().actorValues.healthPercentage == 1.0f);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A made bottle whose craft named it is drunk off its own stack "
          "(Thornswood #1649, #3026)",
          "[MadePotion]")
{
  PartOne& p = GetPartOne();
  auto watch = std::make_shared<Watch>();
  p.AddListener(watch);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  Inventory::Made bottle = Healing();
  Inventory::Round(bottle);
  const uint32_t id = Inventory::MadeBaseId(bottle);
  Inventory::ExtraData named;
  named.made = bottle;
  named.name = "Healer's Draught";
  ac.AddItems({ Inventory::Entry(id, 2, named) });

  watch->armed = true;
  bool drunk = false;
  REQUIRE_NOTHROW(drunk = ac.OnEquip(id));
  watch->armed = false;

  CHECK(drunk);
  CHECK(watch->eats == 1);
  std::vector<Inventory::Entry> left;
  for (auto& entry : ac.GetInventory().entries) {
    if (entry.baseId == id) {
      left.push_back(entry);
    }
  }
  REQUIRE(left.size() == 1);
  CHECK(left[0].count == 1);
  CHECK(left[0].name == named.name);
  CHECK(left[0].made == bottle);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A made poison is spent without being drunk, and a made id the "
          "pack does not hold does nothing (Thornswood #1649)",
          "[MadePotion]")
{
  PartOne& p = GetPartOne();
  auto watch = std::make_shared<Watch>();
  p.AddListener(watch);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);

  Inventory::Made poison;
  poison.name = "Weak Poison";
  poison.poison = true;
  poison.effects.push_back({ 0x3eb42, 4.f, 0, 0 });
  const uint32_t id = Inventory::MadeBaseId(poison);
  Inventory::ExtraData extra;
  extra.made = poison;
  ac.AddItems({ Inventory::Entry(id, 1, extra) });

  watch->armed = true;
  CHECK(ac.OnEquip(id));
  CHECK(!ac.OnEquip(id));
  CHECK(!ac.OnEquip(Inventory::MadeBaseId(Healing())));
  watch->armed = false;

  CHECK(watch->eats == 0);
  CHECK(ac.GetInventory().GetItemCount(id) == 0);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}
