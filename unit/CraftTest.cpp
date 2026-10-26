#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "CraftItemMessage.h"
#include "PacketParser.h"
#include "PartOneListener.h"
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include "SetInventoryMessage.h"
#include "gamemode_events/CraftEvent.h"
#include "gamemode_events/GameModeEvent.h"

using Catch::Matchers::ContainsSubstring;

PartOne& GetPartOne();

namespace {
// THORNSWOOD PATCH. The gamemode's part in a craft with no recipe.
//
// Since 8bce01a0 a craft that matches no COBJ is not dropped here: onCraft
// fires with recipeId 0 and the bench as a fifth argument, and the gamemode
// decides (alchemy and enchanting have no COBJ at all). A listener that
// answers false refuses it and nothing moves; with no listener it goes
// through. This stands in for a gamemode that refuses, and records what it
// was asked. PartOne is shared by every test in the run and has no way to
// remove a listener, so it only acts while `armed` is set.
class RefuseRecipelessCraft : public PartOneListener
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
    if (!armed || event.GetName() != std::string("onCraft")) {
      return true;
    }
    auto args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    if (args.at(3).get<uint32_t>() != 0) {
      return true;
    }
    asked++;
    askedResult = args.at(1).get<uint32_t>();
    askedBench = args.at(4).get<uint32_t>();
    return false;
  }

  bool armed = false;
  int asked = 0;
  uint32_t askedResult = 0;
  uint32_t askedBench = 0;
};
}

TEST_CASE("CraftItem packet is parsed", "[Craft][espm]")
{
  class MyActionListener : public ActionListener
  {
  public:
    MyActionListener()
      : ActionListener(GetPartOne())
    {
    }

    void OnCraftItem(const RawMessageData& rawMsgData_,
                     const CraftItemMessage& msg_) override
    {
      rawMsgData = rawMsgData_;
      inputObjects = msg_.data.craftInputObjects;
      workbenchId = msg_.data.workbench;
      resultObjectId = msg_.data.resultObjectId;
    }

    RawMessageData rawMsgData;
    Inventory inputObjects;
    uint32_t workbenchId = 0;
    uint32_t resultObjectId = 0;
  };

  nlohmann::json j{
    { "t", MsgType::CraftItem },
    { "data",
      { { "workbench", 0xdeadbeef },
        { "resultObjectId", 0x123 },
        { "craftInputObjects", Inventory().AddItem(0x12eb7, 1).ToJson() } } }
  };

  auto msg = MakeMessage(j);

  MyActionListener listener;

  PacketParser p;
  p.TransformPacketIntoAction(
    122, reinterpret_cast<Networking::PacketData>(msg.data()), msg.size(),
    listener);

  REQUIRE(listener.workbenchId == 0xdeadbeef);
  REQUIRE(listener.resultObjectId == 0x123);
  REQUIRE(listener.inputObjects == Inventory().AddItem(0x12eb7, 1));
  REQUIRE(listener.rawMsgData.userId == 122);
}

TEST_CASE("Player is able to craft item", "[Craft][espm]")
{
  const Inventory requiredItems =
    Inventory().AddItem(0x5ace4, 1).AddItem(0x800e4, 3).AddItem(0x5ace5, 4);
  const Inventory requiredItemsForNails = Inventory().AddItem(0x5ace4, 1);

  PartOne& p = GetPartOne();

  const auto workbenchId = 0x1ad6e;
  auto& refr = p.worldState.GetFormAt<MpObjectReference>(workbenchId);

  DoConnect(p, 0);
  p.CreateActor(0xff000000, refr.GetPos(), 0,
                refr.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  for (auto entry : requiredItems.entries)
    ac.AddItem(entry.baseId, entry.count);
  for (auto entry : requiredItemsForNails.entries)
    ac.AddItem(entry.baseId, entry.count);

  RawMessageData msgData;
  msgData.userId = 0;

  // Vanilla item
  REQUIRE(ac.GetInventory().GetItemCount(0x1398a) == 0);
  CraftItemMessage msg1;
  msg1.data.craftInputObjects = requiredItems;
  msg1.data.workbench = workbenchId;
  msg1.data.resultObjectId = 0x1398a;
  p.GetActionListener().OnCraftItem(msgData, msg1);
  REQUIRE(ac.GetInventory().GetItemCount(0x1398a) == 1);

  // Hearthfires item (nails)
  REQUIRE(ac.GetInventory().GetItemCount(0x300300f) == 0);
  CraftItemMessage msg2;
  msg2.data.craftInputObjects = requiredItemsForNails;
  msg2.data.workbench = workbenchId;
  msg2.data.resultObjectId = 0x300300f;
  p.GetActionListener().OnCraftItem(msgData, msg2);
  REQUIRE(ac.GetInventory().GetItemCount(0x300300f) == 10);

  REQUIRE(ac.GetInventory().GetItemCount(0x5ace4) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(0x800e4) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(0x5ace5) == 0);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE(
  "Player is unable to craft an artifact item by using a tempering recipe",
  "[Craft][espm]")
{
  auto DeerPelt = 0x000CF89E;
  auto LeatherStrips = 0x000800E4;
  auto WolfPelt = 0x0003AD74;
  auto Leather = 0x000DB5D2;
  const Inventory requiredItems =
    Inventory()
      .AddItem(DeerPelt, 1)
      .AddItem(LeatherStrips, 2)
      .AddItem(WolfPelt, 1)
      .AddItem(Leather, 1); // Required for temper in vanila

  PartOne& p = GetPartOne();
  const auto workbenchId = 0x1ad6e;
  auto& workbench = p.worldState.GetFormAt<MpObjectReference>(workbenchId);

  DoConnect(p, 0);
  p.CreateActor(0xff000000, workbench.GetPos(), 0,
                workbench.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  for (auto entry : requiredItems.entries)
    ac.AddItem(entry.baseId, entry.count);

  const uint32_t wrongResultObject = 0xd8d4e;

  RawMessageData msgData;
  msgData.userId = 0;

  Inventory previousInventory = ac.GetInventory();

  // THORNSWOOD PATCH. The fork asks the gamemode about a craft with no recipe
  // (8bce01a0) instead of refusing it here, so "unable" is the gamemode's
  // answer: this one refuses, as Thornswood's does for anything that is not a
  // brew or an enchantment.
  auto gamemode = std::make_shared<RefuseRecipelessCraft>();
  p.AddListener(gamemode);
  gamemode->armed = true;

  // Must result in "Recipe not found, asking the gamemode" in logs
  CraftItemMessage msg3;
  msg3.data.craftInputObjects = requiredItems;
  msg3.data.workbench = workbenchId;
  msg3.data.resultObjectId = wrongResultObject;
  p.GetActionListener().OnCraftItem(msgData, msg3);

  gamemode->armed = false;

  Inventory newInventory = ac.GetInventory();

  REQUIRE(gamemode->asked == 1);
  REQUIRE(gamemode->askedResult == wrongResultObject);
  REQUIRE(gamemode->askedBench == workbenchId);
  REQUIRE(previousInventory == newInventory);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("DLC Dragonborn recipes are working", "[Craft][espm]")
{

  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();

  auto form = craftService->FindRecipe(std::nullopt, std::nullopt,
                                       p.GetEspm().GetBrowser(),
                                       Inventory()
                                         .AddItem(0x0005ACE4, 1)
                                         .AddItem(0x0401CD7C, 2)
                                         .AddItem(0x00034CDD, 10),
                                       0x04037564);
  REQUIRE(form.size() == 1);
  REQUIRE(form[0].rec->GetId() == 0x0203d581);
}

TEST_CASE("DLC Hearthfires recipes are working", "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();

  auto recipe = p.GetEspm().GetBrowser().LookupById(0x0300306d);
  auto inputObjects = Inventory().AddItem(0x0005ACE4, 1);
  bool matches =
    craftService->RecipeItemsMatch(recipe, inputObjects, 0x300300F);

  REQUIRE(matches == true);

  auto form = craftService->FindRecipe(
    std::nullopt, std::nullopt, p.GetEspm().GetBrowser(),
    Inventory().AddItem(0x0005ACE4, 1), 0x300300F);
  REQUIRE(form.size() > 0);
  REQUIRE(form[0].rec->GetId() == 0x0200306d);
}

/*
  THORNSWOOD PATCH. A craft that does not go through sends the client its
  inventory back, whichever way it failed. See CraftService::OnCraftItem.
*/
namespace {
// Every inventory the server sent to userId since Messages() was cleared.
std::vector<Inventory> SentInventories(PartOne& p, Networking::UserId userId)
{
  std::vector<Inventory> res;
  for (auto& m : p.Messages()) {
    auto setInventory = dynamic_cast<SetInventoryMessage*>(m.message.get());
    if (setInventory && m.userId == userId) {
      res.push_back(setInventory->inventory);
    }
  }
  return res;
}

// Says no to every craft, the way mp.onCraft returning false does, while
// `armed` is set. PartOne is shared by every test in the run and has no way
// to remove a listener, so an unarmed one would refuse every craft in every
// test after this one.
class RefuseCraftListener : public PartOneListener
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
    if (armed && event.GetName() == std::string("onCraft")) {
      ++numRefused;
      return false;
    }
    return true;
  }

  bool armed = false;
  int numRefused = 0;
};

constexpr uint32_t kIronIngot = 0x5ace4;
constexpr uint32_t kLeatherStrips = 0x800e4;
constexpr uint32_t kSteelIngot = 0x5ace5;
constexpr uint32_t kSteelWarhammer = 0x1398a;
constexpr uint32_t kForge = 0x1ad6e;

MpActor& MakeCrafter(PartOne& p, uint32_t nearRefrId)
{
  auto& refr = p.worldState.GetFormAt<MpObjectReference>(nearRefrId);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, refr.GetPos(), 0,
                refr.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  return p.worldState.GetFormAt<MpActor>(0xff000000);
}
}

TEST_CASE("A craft the server cannot carry out sends the client its "
          "inventory back",
          "[Craft][espm]")
{
  // The shape of the first craft measured on the dev server on 23 September,
  // at 20:46:05: the client listed an input the server does not hold, so
  // taking the inputs away throws, and by then the client has already shown
  // the result. This tests that the inventory is sent. When the client
  // applies it is the client's business, and for that craft it was the same
  // frame with or without this; see CraftService::OnCraftItem.
  const uint32_t notHeld = 0x1be1a;

  PartOne& p = GetPartOne();
  auto& ac = MakeCrafter(p, kForge);
  ac.AddItem(kIronIngot, 3);
  const Inventory before = ac.GetInventory();

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects =
    Inventory().AddItem(notHeld, 1).AddItem(kIronIngot, 3);
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;

  RawMessageData msgData;
  msgData.userId = 0;
  // Since Thornswood #1618 the missing input is caught before the gamemode is
  // asked, so the craft is refused rather than thrown on; the inventory goes
  // back all the same.
  REQUIRE_NOTHROW(p.GetActionListener().OnCraftItem(msgData, msg));

  // Nothing was taken and nothing was made.
  REQUIRE(ac.GetInventory() == before);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == before);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft the gamemode refuses sends the client its inventory back",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto listener = std::make_shared<RefuseCraftListener>();
  p.AddListener(listener);
  listener->armed = true;

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  const Inventory before = ac.GetInventory();

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;

  RawMessageData msgData;
  msgData.userId = 0;
  p.GetActionListener().OnCraftItem(msgData, msg);

  listener->armed = false;
  REQUIRE(listener->numRefused == 1);
  REQUIRE(ac.GetInventory() == before);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) == 0);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == before);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft at something that is not a workbench sends the client its "
          "inventory back",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  const uint32_t barrel = ToUnderlying(Constants::kBarrelInWhiterun);

  auto& ac = MakeCrafter(p, barrel);
  ac.AddItem(kIronIngot, 1);
  const Inventory before = ac.GetInventory();

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects = Inventory().AddItem(kIronIngot, 1);
  msg.data.workbench = barrel;
  msg.data.resultObjectId = kSteelWarhammer;

  RawMessageData msgData;
  msgData.userId = 0;
  p.GetActionListener().OnCraftItem(msgData, msg);

  REQUIRE(ac.GetInventory() == before);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == before);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft that goes through still sends the inventory with the "
          "result in it",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;

  RawMessageData msgData;
  msgData.userId = 0;
  p.GetActionListener().OnCraftItem(msgData, msg);

  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) == 1);
  REQUIRE(ac.GetInventory().GetItemCount(kIronIngot) == 0);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == ac.GetInventory());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

namespace {
// THORNSWOOD PATCH (Thornswood #1618). Counts every onCraft the gamemode is
// asked about while armed, recipe or not, and allows them all, so a craft
// that reaches the gamemode is visible here.
class CountCrafts : public PartOneListener
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
    if (armed && event.GetName() == std::string("onCraft")) {
      asked++;
    }
    return true;
  }

  bool armed = false;
  int asked = 0;
};
}

TEST_CASE("A craft whose inputs the server does not hold never reaches "
          "mp.onCraft (Thornswood #1618)",
          "[Craft][espm]")
{
  // The iron helmet recipe's inputs, as the client claims them. The actor
  // holds only part of them.
  const Inventory claimedInputs =
    Inventory().AddItem(0x5ace4, 1).AddItem(0x800e4, 3).AddItem(0x5ace5, 4);

  PartOne& p = GetPartOne();
  const auto workbenchId = 0x1ad6e;
  auto& refr = p.worldState.GetFormAt<MpObjectReference>(workbenchId);

  DoConnect(p, 0);
  p.CreateActor(0xff000000, refr.GetPos(), 0,
                refr.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  auto& ac = p.worldState.GetFormAt<MpActor>(0xff000000);
  ac.AddItem(0x5ace4, 1);
  ac.AddItem(0x800e4, 3);
  ac.AddItem(0x5ace5, 3); // one short

  auto gamemode = std::make_shared<CountCrafts>();
  p.AddListener(gamemode);
  gamemode->armed = true;

  RawMessageData msgData;
  msgData.userId = 0;
  const Inventory before = ac.GetInventory();

  // With a recipe.
  CraftItemMessage withRecipe;
  withRecipe.data.craftInputObjects = claimedInputs;
  withRecipe.data.workbench = workbenchId;
  withRecipe.data.resultObjectId = 0x1398a;
  CHECK_NOTHROW(p.GetActionListener().OnCraftItem(msgData, withRecipe));

  // With no recipe (the alchemy and enchanting path).
  CraftItemMessage noRecipe;
  noRecipe.data.craftInputObjects = Inventory().AddItem(0x5ace5, 4);
  noRecipe.data.workbench = workbenchId;
  noRecipe.data.resultObjectId = 0xd8d4e;
  CHECK_NOTHROW(p.GetActionListener().OnCraftItem(msgData, noRecipe));

  gamemode->armed = false;

  CHECK(gamemode->asked == 0);
  CHECK(ac.GetInventory() == before);
  CHECK(ac.GetInventory().GetItemCount(0x1398a) == 0);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

namespace {
// THORNSWOOD PATCH (Thornswood #1648). A gamemode that holds the soul gem
// table from docs/systems/enchantments.md, the way game/gamemode.js does, and
// judges each craft by the inputs onCraft now carries as its seventh
// argument. The person's rank is set by the test. Only acts while armed,
// because PartOne is shared and keeps every listener.
constexpr uint32_t kPettySoulGem = 0x2e4e2;
constexpr uint32_t kLesserSoulGem = 0x2e4e4;
constexpr uint32_t kCommonSoulGem = 0x2e4e6;
constexpr uint32_t kGreaterSoulGem = 0x2e4f4;
constexpr uint32_t kGrandSoulGem = 0x2e4fc;
constexpr uint32_t kIronDagger = 0x1397e;

class SoulGemRankGate : public PartOneListener
{
public:
  void OnConnect(Networking::UserId) override {}
  void OnDisconnect(Networking::UserId) override {}
  void OnCustomPacket(Networking::UserId,
                      const simdjson::dom::element&) override
  {
  }
  static int RankNeeded(uint32_t baseId)
  {
    switch (baseId) {
      case kPettySoulGem:
        return 0; // novice
      case kLesserSoulGem:
        return 1; // advanced
      case kCommonSoulGem:
        return 2; // expert
      case kGreaterSoulGem:
      case kGrandSoulGem:
        return 3; // master
      default:
        return -1; // not a soul gem
    }
  }
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (!armed || event.GetName() != std::string("onCraft")) {
      return true;
    }
    auto args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    asked++;
    argCount = args.size();
    effectsWasNull = args.at(5).is_null();
    inputs = args.at(6);
    for (auto& entry : inputs) {
      int need = RankNeeded(entry.at("baseId").get<uint32_t>());
      if (need > rank) {
        refused++;
        return false;
      }
    }
    return true;
  }

  bool armed = false;
  int rank = 0;
  int asked = 0;
  int refused = 0;
  size_t argCount = 0;
  bool effectsWasNull = false;
  nlohmann::json inputs;
};

// A new one each time: GetPartOne() is not the same PartOne for the whole
// run, so a listener kept from an earlier test may be on one no longer used.
// One left behind unarmed allows everything.
std::shared_ptr<SoulGemRankGate> SoulGemGate(PartOne& p)
{
  auto gate = std::make_shared<SoulGemRankGate>();
  p.AddListener(gate);
  return gate;
}

CraftItemMessage EnchantDagger(uint32_t gem)
{
  CraftItemMessage msg;
  msg.data.craftInputObjects =
    Inventory().AddItem(kIronDagger, 1).AddItem(gem, 1);
  msg.data.workbench = kForge;
  // An enchanted piece keeps its own record, so what arrives is the dagger.
  msg.data.resultObjectId = kIronDagger;
  return msg;
}
}

TEST_CASE("onCraft carries the craft's inputs as its seventh argument "
          "(Thornswood #1648)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gate = SoulGemGate(p);
  gate->rank = 3;
  auto& ac = MakeCrafter(p, kForge);
  ac.AddItem(kIronDagger, 1);
  ac.AddItem(kPettySoulGem, 1);

  RawMessageData msgData;
  msgData.userId = 0;
  gate->armed = true;
  p.GetActionListener().OnCraftItem(msgData, EnchantDagger(kPettySoulGem));
  gate->armed = false;

  REQUIRE(gate->asked == 1);
  REQUIRE(gate->argCount == 8);
  REQUIRE(gate->effectsWasNull);
  REQUIRE(gate->inputs.is_array());
  REQUIRE(gate->inputs.size() == 2);
  std::map<uint32_t, uint32_t> got;
  for (auto& entry : gate->inputs) {
    auto baseId = entry.at("baseId").get<uint32_t>();
    got[baseId] = entry.at("count").get<uint32_t>();
  }
  REQUIRE(got[kIronDagger] == 1);
  REQUIRE(got[kPettySoulGem] == 1);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A soul gem above the person's rank is refused and the gem and the "
          "piece stay in the pack (Thornswood #1648)",
          "[Craft][espm]")
{
  struct Case
  {
    int rank;
    uint32_t gem;
  };
  const Case cases[] = { { 0, kLesserSoulGem },  { 0, kGreaterSoulGem },
                         { 1, kCommonSoulGem },  { 2, kGreaterSoulGem },
                         { 2, kGrandSoulGem } };

  PartOne& p = GetPartOne();
  for (auto& c : cases) {
    auto gate = SoulGemGate(p);
    gate->rank = c.rank;
    auto& ac = MakeCrafter(p, kForge);
    ac.AddItem(kIronDagger, 1);
    ac.AddItem(c.gem, 1);
    const Inventory before = ac.GetInventory();

    p.Tick();
    p.Messages().clear();

    RawMessageData msgData;
    msgData.userId = 0;
    gate->armed = true;
    p.GetActionListener().OnCraftItem(msgData, EnchantDagger(c.gem));
    gate->armed = false;

    CHECK(gate->refused == 1);
    CHECK(ac.GetInventory() == before);
    CHECK(ac.GetInventory().GetItemCount(c.gem) ==
          before.GetItemCount(c.gem));
    CHECK(ac.GetInventory().GetItemCount(kIronDagger) ==
          before.GetItemCount(kIronDagger));

    // The client is told its pack is as it was, gem and piece included.
    p.Tick();
    auto sent = SentInventories(p, 0);
    CHECK(sent.size() == 1);
    if (!sent.empty()) {
      CHECK(sent[0] == before);
    }

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }
}

TEST_CASE("A petty soul gem works at every rank (Thornswood #1648)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  for (int rank = 0; rank <= 3; ++rank) {
    auto gate = SoulGemGate(p);
    gate->rank = rank;
    auto& ac = MakeCrafter(p, kForge);
    ac.AddItem(kIronDagger, 1);
    ac.AddItem(kPettySoulGem, 1);
    const Inventory before = ac.GetInventory();

    RawMessageData msgData;
    msgData.userId = 0;
    gate->armed = true;
    p.GetActionListener().OnCraftItem(msgData, EnchantDagger(kPettySoulGem));
    gate->armed = false;

    CHECK(gate->asked == 1);
    CHECK(gate->refused == 0);
    // The gem is spent and the piece comes back.
    CHECK(ac.GetInventory().GetItemCount(kPettySoulGem) ==
          before.GetItemCount(kPettySoulGem) - 1);
    CHECK(ac.GetInventory().GetItemCount(kIronDagger) ==
          before.GetItemCount(kIronDagger));

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }
}
namespace {
// THORNSWOOD PATCH (Thornswood #1389). Skyrim.esm's ingredients and the
// magic effects their records carry, read off the plugin on 12 Oct.
constexpr uint32_t kBlueMountainFlower = 0x77e1c; // 3eb15 3eb25 3eaf3 73f2b
constexpr uint32_t kWheat = 0x4b0ba;              // 3eb15 3eaf3 73f2c 10de5f
constexpr uint32_t kNightshade = 0x2f44c;         // 3eb42 73f2b 10de5e 3eb26
constexpr uint32_t kLavender = 0x45c28;           // 39e51 3eaf9 73f27 3eb25
constexpr uint32_t kTundraCotton = 0x3f7f8;       // 39e51 3eaf8 3eb1c 3eb23
constexpr uint32_t kSaltPile = 0x34cdf;           // 73f51 3eb28 73f25 3eb07
constexpr uint32_t kRestoreHealth = 0x3eb15;
constexpr uint32_t kFortifyHealth = 0x3eaf3;
constexpr uint32_t kDamageMagickaRegen = 0x73f2b;
constexpr uint32_t kResistMagic = 0x39e51;
// What a lab hands back is a bottle the brewing client made, a runtime form
// the server has no record of. A plain healing potion's record stands in for
// it here (Potion of Minor Healing), because the bottle is added to the pack
// when the craft goes through, and the effects are read from the ingredients,
// never from the bottle.
constexpr uint32_t kBrewedBottle = 0x3eadd;

// A gamemode holding a short mixtures list, the way game/gamemode.js holds
// MIXTURES: it reads the sixth argument and refuses a brew carrying an effect
// that is not on it. Only acts while armed; PartOne keeps every listener.
class MixturesListGate : public PartOneListener
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
    if (!armed || event.GetName() != std::string("onCraft")) {
      return true;
    }
    auto args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    asked++;
    argCount = args.size();
    effects = args.at(5);
    if (!effects.is_array()) {
      return true;
    }
    for (auto& mgef : effects) {
      auto id = mgef.get<uint32_t>();
      if (id != kRestoreHealth && id != kFortifyHealth) {
        refused++;
        return false;
      }
    }
    return true;
  }

  bool armed = false;
  int asked = 0;
  int refused = 0;
  size_t argCount = 0;
  nlohmann::json effects;
};

CraftItemMessage Brew(std::vector<uint32_t> ingredients)
{
  CraftItemMessage msg;
  for (auto id : ingredients) {
    msg.data.craftInputObjects.AddItem(id, 1);
  }
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kBrewedBottle;
  return msg;
}
}

TEST_CASE("A brew's effects are the ones two of its ingredients share "
          "(Thornswood #1389)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();
  auto& br = p.GetEspm().GetBrowser();

  auto two = craftService->BrewEffects(
    br, Inventory().AddItem(kBlueMountainFlower, 1).AddItem(kWheat, 1));
  REQUIRE(two.has_value());
  REQUIRE(*two == std::vector<uint32_t>{ kRestoreHealth, kFortifyHealth });

  // Three ingredients: each effect two of them share, in the order the
  // inputs first name them.
  auto three = craftService->BrewEffects(br,
                                         Inventory()
                                           .AddItem(kBlueMountainFlower, 1)
                                           .AddItem(kWheat, 1)
                                           .AddItem(kNightshade, 1));
  REQUIRE(three.has_value());
  REQUIRE(*three ==
          std::vector<uint32_t>{ kRestoreHealth, kFortifyHealth,
                                 kDamageMagickaRegen });

  auto resist = craftService->BrewEffects(
    br, Inventory().AddItem(kLavender, 1).AddItem(kTundraCotton, 1));
  REQUIRE(resist.has_value());
  REQUIRE(*resist == std::vector<uint32_t>{ kResistMagic });

  // Nothing shared, and one ingredient twice, hold no effect: a named empty
  // list, which the gamemode refuses as a bottle with nothing in it.
  auto none = craftService->BrewEffects(
    br, Inventory().AddItem(kBlueMountainFlower, 1).AddItem(kSaltPile, 1));
  REQUIRE(none.has_value());
  REQUIRE(none->empty());
  auto twice = craftService->BrewEffects(
    br, Inventory().AddItem(kBlueMountainFlower, 2));
  REQUIRE(twice.has_value());
  REQUIRE(twice->empty());

  // No ingredient at all (an enchantment): not named.
  auto enchant = craftService->BrewEffects(
    br, Inventory().AddItem(kIronDagger, 1).AddItem(kPettySoulGem, 1));
  REQUIRE(!enchant.has_value());
}

TEST_CASE("onCraft's sixth argument names a brew's effects, and a brew off "
          "the list is refused with the ingredients kept (Thornswood #1389)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  RawMessageData msgData;
  msgData.userId = 0;

  {
    auto gate = std::make_shared<MixturesListGate>();
    p.AddListener(gate);
    auto& ac = MakeCrafter(p, kForge);
    ac.AddItem(kBlueMountainFlower, 1);
    ac.AddItem(kWheat, 1);

    gate->armed = true;
    p.GetActionListener().OnCraftItem(
      msgData, Brew({ kBlueMountainFlower, kWheat }));
    gate->armed = false;

    REQUIRE(gate->asked == 1);
    REQUIRE(gate->argCount == 8);
    REQUIRE(gate->effects ==
            nlohmann::json::array({ kRestoreHealth, kFortifyHealth }));
    CHECK(gate->refused == 0);
    // A brew on the list goes through: the ingredients are spent.
    CHECK(ac.GetInventory().GetItemCount(kBlueMountainFlower) == 0);
    CHECK(ac.GetInventory().GetItemCount(kWheat) == 0);

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }

  {
    auto gate = std::make_shared<MixturesListGate>();
    p.AddListener(gate);
    auto& ac = MakeCrafter(p, kForge);
    ac.AddItem(kLavender, 1);
    ac.AddItem(kTundraCotton, 1);
    const Inventory before = ac.GetInventory();

    gate->armed = true;
    p.GetActionListener().OnCraftItem(msgData,
                                      Brew({ kLavender, kTundraCotton }));
    gate->armed = false;

    REQUIRE(gate->asked == 1);
    REQUIRE(gate->effects == nlohmann::json::array({ kResistMagic }));
    CHECK(gate->refused == 1);
    CHECK(ac.GetInventory() == before);

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }

  {
    // An enchantment still has no effects named, so the gamemode is told
    // null and decides as it did before.
    auto gate = std::make_shared<MixturesListGate>();
    p.AddListener(gate);
    auto& ac = MakeCrafter(p, kForge);
    ac.AddItem(kIronDagger, 1);
    ac.AddItem(kPettySoulGem, 1);

    gate->armed = true;
    p.GetActionListener().OnCraftItem(msgData, EnchantDagger(kPettySoulGem));
    gate->armed = false;

    REQUIRE(gate->asked == 1);
    CHECK(gate->effects.is_null());

    p.DestroyActor(0xff000000);
    DoDisconnect(p, 0);
  }
}

namespace {
// THORNSWOOD PATCH (Thornswood #2906). Stands in for a gamemode's onCraft
// handler setting craftOutput, which reaches the event the same way (the
// events stack): a cook's burnt meal in place of the dish. Only acts while
// armed, because PartOne is shared and keeps every listener.
class NameCraftOutput : public PartOneListener
{
public:
  explicit NameCraftOutput(WorldState& worldState_)
    : worldState(worldState_)
  {
  }
  void OnConnect(Networking::UserId) override {}
  void OnDisconnect(Networking::UserId) override {}
  void OnCustomPacket(Networking::UserId,
                      const simdjson::dom::element&) override
  {
  }
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (!armed || event.GetName() != std::string("onCraft")) {
      return true;
    }
    auto& stack = worldState.currentGameModeEventsStack;
    auto craft =
      stack.empty() ? nullptr : dynamic_cast<CraftEvent*>(stack.back());
    if (craft) {
      seenBaseId = craft->GetOutputBaseId();
      seenCount = craft->GetOutputCount();
      seenName = craft->GetOutputName();
      craft->SetOutput(baseId, count);
      if (name) {
        craft->SetOutputName(*name);
      }
    }
    return allow;
  }

  bool armed = false;
  bool allow = true;
  uint32_t baseId = 0;
  uint32_t count = 0;
  // Thornswood #3026: the name to give the entry, when set.
  std::optional<std::string> name;
  uint32_t seenBaseId = 0;
  uint32_t seenCount = 0;
  std::optional<std::string> seenName;

private:
  WorldState& worldState;
};
}

TEST_CASE("An onCraft handler names what the craft gives (Thornswood #2906)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = std::make_shared<NameCraftOutput>(p.worldState);
  p.AddListener(gamemode);

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  // A new actor's pack holds its base record's own items once it is read (an
  // iron dagger among them, measured on the fork's build), so what the craft
  // gives is counted from the pack as it was.
  const Inventory before = ac.GetInventory();

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  RawMessageData msgData;
  msgData.userId = 0;

  // The warhammer's recipe, and the handler names two iron daggers instead.
  gamemode->baseId = kIronDagger;
  gamemode->count = 2;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, msg);
  gamemode->armed = false;

  // Until it was named the output was the recipe's own.
  REQUIRE(gamemode->seenBaseId == kSteelWarhammer);
  REQUIRE(gamemode->seenCount == 1);

  // The inputs are gone, the named item is in the pack and the recipe's
  // output never was.
  REQUIRE(ac.GetInventory().GetItemCount(kIronDagger) ==
          before.GetItemCount(kIronDagger) + 2);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) ==
          before.GetItemCount(kSteelWarhammer));
  REQUIRE(ac.GetInventory().GetItemCount(kIronIngot) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(kLeatherStrips) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == ac.GetInventory());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("An onCraft handler doubles a batch of the recipe's own output "
          "(Thornswood #2906)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = std::make_shared<NameCraftOutput>(p.worldState);
  p.AddListener(gamemode);

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  const Inventory before = ac.GetInventory();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  RawMessageData msgData;
  msgData.userId = 0;

  // Grand Chef's rule (cook.md): every batch yields two instead of one.
  gamemode->baseId = kSteelWarhammer;
  gamemode->count = 2;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, msg);
  gamemode->armed = false;

  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) ==
          before.GetItemCount(kSteelWarhammer) + 2);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A refused craft gives nothing whatever its handler named "
          "(Thornswood #2906)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = std::make_shared<NameCraftOutput>(p.worldState);
  p.AddListener(gamemode);

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  const Inventory before = ac.GetInventory();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  RawMessageData msgData;
  msgData.userId = 0;

  gamemode->baseId = kIronDagger;
  gamemode->count = 2;
  gamemode->allow = false;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, msg);
  gamemode->armed = false;

  REQUIRE(ac.GetInventory() == before);
  REQUIRE(ac.GetInventory().GetItemCount(kIronDagger) ==
          before.GetItemCount(kIronDagger));

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft's output cannot be named as nothing (Thornswood #2906)",
          "[Craft]")
{
  std::vector<Inventory::Entry> entries;
  CraftEvent craft(nullptr, kSteelWarhammer, 1, 0, entries);
  REQUIRE_THROWS(craft.SetOutput(0, 1));
  REQUIRE_THROWS(craft.SetOutput(kIronDagger, 0));
  REQUIRE(craft.GetOutputBaseId() == kSteelWarhammer);
  REQUIRE(craft.GetOutputCount() == 1);
}

namespace {
// How many of baseId the pack holds under that name (Thornswood #3026).
uint32_t NamedCount(const Inventory& inventory, uint32_t baseId,
                    const std::optional<std::string>& name)
{
  uint32_t sum = 0;
  for (auto& entry : inventory.entries) {
    if (entry.baseId == baseId && entry.name == name) {
      sum += entry.count;
    }
  }
  return sum;
}
}

TEST_CASE("An onCraft handler names the entry a craft gives (Thornswood "
          "#3026)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = std::make_shared<NameCraftOutput>(p.worldState);
  p.AddListener(gamemode);

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  const Inventory before = ac.GetInventory();

  p.Tick();
  p.Messages().clear();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  RawMessageData msgData;
  msgData.userId = 0;

  // A master weaver's mark (tailor.md): the recipe's own output, named.
  const std::string woven = "Weatherwoven Steel Warhammer";
  gamemode->baseId = kSteelWarhammer;
  gamemode->count = 1;
  gamemode->name = woven;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, msg);
  gamemode->armed = false;
  gamemode->name.reset();

  // Until it was named the output had no name.
  REQUIRE(!gamemode->seenName.has_value());

  // The craft added the named entry and no plain one: nothing was added
  // plain to be renamed afterwards.
  REQUIRE(NamedCount(ac.GetInventory(), kSteelWarhammer, woven) == 1);
  REQUIRE(NamedCount(ac.GetInventory(), kSteelWarhammer, std::nullopt) ==
          NamedCount(before, kSteelWarhammer, std::nullopt));
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);

  p.Tick(); // send deferred inventory update messages
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == ac.GetInventory());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A refused craft gives no named entry (Thornswood #3026)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = std::make_shared<NameCraftOutput>(p.worldState);
  p.AddListener(gamemode);

  auto& ac = MakeCrafter(p, kForge);
  const Inventory recipe = Inventory()
                             .AddItem(kIronIngot, 1)
                             .AddItem(kLeatherStrips, 3)
                             .AddItem(kSteelIngot, 4);
  for (auto& entry : recipe.entries) {
    ac.AddItem(entry.baseId, entry.count);
  }
  const Inventory before = ac.GetInventory();

  CraftItemMessage msg;
  msg.data.craftInputObjects = recipe;
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  RawMessageData msgData;
  msgData.userId = 0;

  gamemode->baseId = kSteelWarhammer;
  gamemode->count = 1;
  gamemode->name = "Weatherwoven Steel Warhammer";
  gamemode->allow = false;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, msg);
  gamemode->armed = false;
  gamemode->name.reset();

  REQUIRE(ac.GetInventory() == before);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft's output name cannot be empty, and null names none "
          "(Thornswood #3026)",
          "[Craft]")
{
  std::vector<Inventory::Entry> entries;
  CraftEvent craft(nullptr, kSteelWarhammer, 1, 0, entries);
  REQUIRE(!craft.GetOutputName().has_value());
  REQUIRE_THROWS(craft.SetOutputName(std::string()));
  REQUIRE(!craft.GetOutputName().has_value());
  craft.SetOutputName(std::string("Woven"));
  REQUIRE(craft.GetOutputName() == std::optional<std::string>("Woven"));
  craft.SetOutputName(std::nullopt);
  REQUIRE(!craft.GetOutputName().has_value());
}

/*
  THORNSWOOD PATCH (Thornswood #3159). A bench in a building takes what its
  owner's chests hold. The gamemode names the chests when the bench is
  pressed (the craftStock property, MpActor::SetBenchStock), they open as the
  bench is taken, and a craft takes from the pack first and from them for the
  rest.
*/
namespace {
const uint32_t kStockBarrel = ToUnderlying(Constants::kBarrelInWhiterun);

// Records the arguments of the last onCraft it was asked about while armed,
// and allows it unless `allow` is false. With `emptyBarrel` set it empties
// kStockBarrel first, the way a craft and somebody at the chest can meet in
// one tick.
class RecordCraft : public PartOneListener
{
public:
  RecordCraft(PartOne& p_)
    : p(p_)
  {
  }
  void OnConnect(Networking::UserId) override {}
  void OnDisconnect(Networking::UserId) override {}
  void OnCustomPacket(Networking::UserId,
                      const simdjson::dom::element&) override
  {
  }
  bool OnMpApiEvent(const GameModeEvent& event) override
  {
    if (!armed || event.GetName() != std::string("onCraft")) {
      return true;
    }
    asked++;
    args = nlohmann::json::parse(event.GetArgumentsJsonArray());
    if (emptyBarrel) {
      p.worldState.GetFormAt<MpObjectReference>(kStockBarrel)
        .SetInventory(Inventory());
    }
    return allow;
  }

  PartOne& p;
  bool armed = false;
  bool allow = true;
  bool emptyBarrel = false;
  int asked = 0;
  nlohmann::json args;
};

std::shared_ptr<RecordCraft> RecordCrafts(PartOne& p)
{
  auto res = std::make_shared<RecordCraft>(p);
  p.AddListener(res);
  return res;
}

// The steel warhammer's inputs. The pack holds all of them but three of the
// four steel ingots, and the barrel holds five.
void StockForWarhammer(PartOne& p, MpActor& ac)
{
  ac.AddItem(kIronIngot, 1);
  ac.AddItem(kLeatherStrips, 3);
  ac.AddItem(kSteelIngot, 1);
  p.worldState.GetFormAt<MpObjectReference>(kStockBarrel)
    .SetInventory(Inventory().AddItem(kSteelIngot, 5));
}

CraftItemMessage Warhammer()
{
  CraftItemMessage msg;
  msg.data.craftInputObjects = Inventory()
                                 .AddItem(kIronIngot, 1)
                                 .AddItem(kLeatherStrips, 3)
                                 .AddItem(kSteelIngot, 4);
  msg.data.workbench = kForge;
  msg.data.resultObjectId = kSteelWarhammer;
  return msg;
}
}

TEST_CASE("A bench's open stock gives what the pack lacks, and onCraft "
          "names it as its eighth argument (Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = RecordCrafts(p);
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  StockForWarhammer(p, ac);

  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  REQUIRE(ac.OpenBenchStock(kForge));

  RawMessageData msgData;
  msgData.userId = 0;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, Warhammer());
  gamemode->armed = false;

  REQUIRE(gamemode->asked == 1);
  REQUIRE(gamemode->args.size() == 8);
  auto& draws = gamemode->args.at(7);
  REQUIRE(draws.is_array());
  REQUIRE(draws.size() == 1);
  REQUIRE(draws[0].at("baseId").get<uint32_t>() == kSteelIngot);
  REQUIRE(draws[0].at("count").get<uint32_t>() == 3);
  REQUIRE(draws[0].at("from").get<uint32_t>() == kStockBarrel);

  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) == 1);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(kIronIngot) == 0);
  REQUIRE(ac.GetInventory().GetItemCount(kLeatherStrips) == 0);
  REQUIRE(barrel.GetInventory().GetItemCount(kSteelIngot) == 2);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A craft the pack gives in full draws nothing from the stock "
          "(Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = RecordCrafts(p);
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  StockForWarhammer(p, ac);
  ac.AddItem(kSteelIngot, 3);

  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  REQUIRE(ac.OpenBenchStock(kForge));

  RawMessageData msgData;
  msgData.userId = 0;
  gamemode->armed = true;
  p.GetActionListener().OnCraftItem(msgData, Warhammer());
  gamemode->armed = false;

  REQUIRE(gamemode->asked == 1);
  REQUIRE(gamemode->args.at(7).empty());
  REQUIRE(ac.GetInventory().GetItemCount(kSteelWarhammer) == 1);
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);
  REQUIRE(barrel.GetInventory().GetItemCount(kSteelIngot) == 5);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A stock that is not open, or is another bench's, gives nothing "
          "(Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = RecordCrafts(p);
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  StockForWarhammer(p, ac);
  const Inventory before = ac.GetInventory();

  RawMessageData msgData;
  msgData.userId = 0;
  gamemode->armed = true;

  SECTION("named but the bench not taken")
  {
    ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  }
  SECTION("open at another bench")
  {
    const uint32_t otherBench = 0xbbcf1;
    ac.SetBenchStock(MpActor::BenchStock{ otherBench, { kStockBarrel } });
    REQUIRE(ac.OpenBenchStock(otherBench));
  }
  SECTION("named for this bench, then another bench taken")
  {
    ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
    REQUIRE(!ac.OpenBenchStock(0xbbcf1));
    REQUIRE(!ac.GetBenchStock());
  }
  SECTION("open, then the bench left")
  {
    ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
    REQUIRE(ac.OpenBenchStock(kForge));
    ac.CloseBenchStock(kForge);
    REQUIRE(!ac.GetBenchStock());
  }

  REQUIRE_NOTHROW(p.GetActionListener().OnCraftItem(msgData, Warhammer()));
  gamemode->armed = false;

  REQUIRE(gamemode->asked == 0);
  REQUIRE(ac.GetInventory() == before);
  REQUIRE(barrel.GetInventory().GetItemCount(kSteelIngot) == 5);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A refused craft takes nothing from the stock (Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = RecordCrafts(p);
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  StockForWarhammer(p, ac);
  const Inventory before = ac.GetInventory();
  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  REQUIRE(ac.OpenBenchStock(kForge));

  RawMessageData msgData;
  msgData.userId = 0;
  gamemode->allow = false;
  gamemode->armed = true;
  REQUIRE_NOTHROW(p.GetActionListener().OnCraftItem(msgData, Warhammer()));
  gamemode->armed = false;

  REQUIRE(gamemode->asked == 1);
  REQUIRE(ac.GetInventory() == before);
  REQUIRE(barrel.GetInventory().GetItemCount(kSteelIngot) == 5);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A chest emptied while the craft is asked about refuses it with "
          "nothing moved (Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto gamemode = RecordCrafts(p);
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  StockForWarhammer(p, ac);
  const Inventory before = ac.GetInventory();
  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  REQUIRE(ac.OpenBenchStock(kForge));

  RawMessageData msgData;
  msgData.userId = 0;
  gamemode->emptyBarrel = true;
  gamemode->armed = true;
  REQUIRE_THROWS(p.GetActionListener().OnCraftItem(msgData, Warhammer()));
  gamemode->armed = false;

  REQUIRE(gamemode->asked == 1);
  REQUIRE(ac.GetInventory() == before);
  REQUIRE(barrel.GetInventory().IsEmpty());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("The pack is sent with the stock in it while the bench is open, "
          "and without it after (Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  ac.AddItem(kIronIngot, 1);

  // A named piece is not something a craft can take (the inputs the client
  // names carry no name), so it is not shown either.
  Inventory::ExtraData named;
  named.name = "Grandfather's Ingot";
  Inventory stock;
  stock.AddItem(kSteelIngot, 5);
  stock.AddItems({ Inventory::Entry(kIronIngot, 1, named) });
  barrel.SetInventory(stock);

  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  p.Tick();
  p.Messages().clear();

  REQUIRE(ac.OpenBenchStock(kForge));

  // Sent at once, not at the end of the tick.
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0].GetItemCount(kIronIngot) == 1);
  REQUIRE(sent[0].GetItemCount(kSteelIngot) == 5);
  REQUIRE(sent[0] == ac.GetInventoryWithBenchStock());
  REQUIRE(ac.GetInventory().GetItemCount(kSteelIngot) == 0);

  // And the one at the end of the tick says the same.
  p.Tick();
  sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 2);
  REQUIRE(sent[1] == sent[0]);

  p.Messages().clear();
  ac.CloseBenchStock(kForge);
  p.Tick();
  sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0] == ac.GetInventory());
  REQUIRE(sent[0].GetItemCount(kSteelIngot) == 0);

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}

TEST_CASE("A chest that changes while its bench is open is sent again, and "
          "not after (Thornswood #3159)",
          "[Craft][espm]")
{
  PartOne& p = GetPartOne();
  auto& ac = MakeCrafter(p, kForge);
  auto& barrel = p.worldState.GetFormAt<MpObjectReference>(kStockBarrel);
  barrel.SetInventory(Inventory().AddItem(kSteelIngot, 5));
  ac.SetBenchStock(MpActor::BenchStock{ kForge, { kStockBarrel } });
  REQUIRE(ac.OpenBenchStock(kForge));
  p.Tick();
  p.Messages().clear();

  // Somebody else takes two out of the chest.
  barrel.RemoveItems({ Inventory::Entry(kSteelIngot, 2) });
  p.Tick();
  auto sent = SentInventories(p, 0);
  REQUIRE(sent.size() == 1);
  REQUIRE(sent[0].GetItemCount(kSteelIngot) == 3);

  ac.CloseBenchStock(kForge);
  p.Tick();
  p.Messages().clear();
  barrel.AddItem(kSteelIngot, 4);
  p.Tick();
  REQUIRE(SentInventories(p, 0).empty());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}
