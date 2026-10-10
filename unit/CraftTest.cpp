#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "CraftItemMessage.h"
#include "PacketParser.h"
#include "PartOneListener.h"
#include <memory>
#include <nlohmann/json.hpp>
#include "SetInventoryMessage.h"
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