#include "OpenContainerMessage.h"
#include "TestUtils.hpp"

PartOne& GetPartOne();

namespace {
MpObjectReference& CreateFurniture(PartOne& p)
{
  // The forge base is checked by EspmTest. All furniture uses this path.
  auto furniture = std::make_unique<MpObjectReference>(
    LocationalData{ { 0, 0, 0 }, {}, FormDesc::Tamriel() },
    p.CreateFormCallbacks(), 0xbbcf1, "FURN");
  p.worldState.AddForm(std::move(furniture), 0xff000002);
  return p.worldState.GetFormAt<MpObjectReference>(0xff000002);
}
}

TEST_CASE("Hosted NPC activations do not open furniture for their host",
          "[HostedActivation][PartOne][espm]")
{
  auto& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c);
  auto& host = p.worldState.GetFormAt<MpActor>(0xff000000);
  auto& npc = p.worldState.GetFormAt<MpActor>(0xff000001);
  auto& furniture = CreateFurniture(p);
  p.worldState.hosters[npc.GetFormId()] = host.GetFormId();
  REQUIRE(npc.GetUserId() == Networking::InvalidUserId);
  REQUIRE(npc.GetActorToSendTo().GetFormId() == host.GetFormId());

  p.Messages().clear();
  furniture.Activate(npc, true);
  REQUIRE(p.Messages().empty());

  // The NPC still occupies its seat until it leaves it.
  furniture.Activate(host, true);
  REQUIRE(p.Messages().empty());
  furniture.Activate(npc, true, true);
  furniture.Activate(host, true);
  REQUIRE(p.Messages().size() == 1);
  REQUIRE(p.Messages()[0].userId == 0);
  auto message =
    dynamic_cast<OpenContainerMessage*>(p.Messages()[0].message.get());
  REQUIRE(message);
  REQUIRE(message->target == furniture.GetFormId());
}

TEST_CASE("Connected actors receive their own furniture activation",
          "[HostedActivation][PartOne][espm]")
{
  auto& p = GetPartOne();
  DoConnect(p, 0);
  DoConnect(p, 1);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  p.SetUserActor(1, 0xff000001);
  auto& actor = p.worldState.GetFormAt<MpActor>(0xff000001);
  auto& furniture = CreateFurniture(p);
  p.worldState.hosters[actor.GetFormId()] = 0xff000000;

  p.Messages().clear();
  furniture.Activate(actor, true);

  REQUIRE(p.Messages().size() == 1);
  REQUIRE(p.Messages()[0].userId == 1);
  auto message =
    dynamic_cast<OpenContainerMessage*>(p.Messages()[0].message.get());
  REQUIRE(message);
  REQUIRE(message->target == furniture.GetFormId());
}