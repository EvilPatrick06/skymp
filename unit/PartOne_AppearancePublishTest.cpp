#include "TestUtils.hpp"
#include <algorithm>

namespace {

size_t CountOnlineActors(PartOne& partOne)
{
  size_t numOnlineActors = 0;
  for (size_t i = 0, n = partOne.serverState.maxConnectedId; i <= n; ++i) {
    if (partOne.serverState.ActorByUser(i)) {
      ++numOnlineActors;
    }
  }
  return numOnlineActors;
}

bool HasCreateActorFor(PartOne& partOne, Networking::UserId userId,
                       uint32_t idx)
{
  return std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                      [&](auto m) {
                        return m.j["t"] == MsgType::CreateActor &&
                          m.j["idx"] == static_cast<int>(idx) &&
                          m.userId == userId;
                      }) != partOne.Messages().end();
}

}

TEST_CASE("Owner still receives an unpublished actor so the race menu can run",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  REQUIRE_FALSE(actor.HasStoredAppearance());

  partOne.SetUserActor(0, 0xff000ABC);
  REQUIRE(partOne.GetUserActor(0) == 0xff000ABC);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(HasCreateActorFor(partOne, 0, 0));
  REQUIRE(partOne.Messages()[0].j["isMe"] == true);

  partOne.Messages().clear();
  partOne.SetRaceMenuOpen(0xff000ABC, true);
  REQUIRE(actor.IsRaceMenuOpen() == true);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j ==
          nlohmann::json{ { "t", MsgType::SetRaceMenuOpen },
                          { "open", true } });
  REQUIRE(partOne.Messages()[0].userId == 0);
}

TEST_CASE("Neighbors do not receive a connected player before a face is stored",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c, 7);
  partOne.SetUserActor(0, 0xff000ABC);
  partOne.SetRaceMenuOpen(0xff000ABC, true);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c, 8);
  partOne.Messages().clear();
  partOne.SetUserActor(1, 0xff000FFF);

  REQUIRE_FALSE(HasCreateActorFor(partOne, 1, 0));
  REQUIRE_FALSE(HasCreateActorFor(partOne, 0, 1));
  REQUIRE(HasCreateActorFor(partOne, 1, 1));

  REQUIRE(CountOnlineActors(partOne) == 2);
  REQUIRE(partOne.serverState.ActorByUser(0) != nullptr);
  REQUIRE(partOne.serverState.ActorByUser(1) != nullptr);

  partOne.Messages().clear();
  DoMessage(partOne, 0, jMovement);
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [](auto m) {
                         return m.j["t"] == MsgType::UpdateMovement &&
                           m.userId == 1;
                       }) == partOne.Messages().end());
}

TEST_CASE("Storing an appearance publishes the actor with the stored name",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000ABC);
  partOne.SetRaceMenuOpen(0xff000ABC, true);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(1, 0xff000FFF);

  partOne.Messages().clear();
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  GiveStoredAppearance(actor);

  REQUIRE(actor.HasStoredAppearance());
  REQUIRE(actor.ShouldPublishToOtherClients());
  REQUIRE(partOne.GetActorName(0xff000ABC) == "Oberyn");

  auto res = FindRefrMessageIdx<CreateActorMessage>(partOne, 0);
  REQUIRE(res.filteredMessages.size() == 1);
  REQUIRE(res.filteredMessagesOriginals[0].userId == 1);
  REQUIRE(res.filteredMessages[0].appearance.has_value());
  REQUIRE(res.filteredMessages[0].appearance->name == "Oberyn");
  REQUIRE(res.filteredMessages[0].baseId != 0x7);
}

TEST_CASE("A blank stored name is not the publish gate",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000ABC);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(1, 0xff000FFF);

  auto data = jAppearance["data"];
  data["name"] = "";
  partOne.Messages().clear();
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC),
                       data);

  auto res = FindRefrMessageIdx<CreateActorMessage>(partOne, 0);
  REQUIRE(res.filteredMessages.size() == 1);
  REQUIRE(res.filteredMessagesOriginals[0].userId == 1);
  REQUIRE(res.filteredMessages[0].appearance.has_value());
  REQUIRE(res.filteredMessages[0].appearance->name.empty());
}

TEST_CASE("A returning character with an appearance is visible on login",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  GiveStoredAppearance(actor);
  REQUIRE_FALSE(actor.IsRaceMenuOpen());

  DoConnect(partOne, 0);
  partOne.SetUserActor(0, 0xff000ABC);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000FFF));
  partOne.Messages().clear();
  partOne.SetUserActor(1, 0xff000FFF);

  REQUIRE(HasCreateActorFor(partOne, 1, 0));
  auto res = FindRefrMessageIdx<CreateActorMessage>(partOne, 0);
  REQUIRE(res.filteredMessages.size() == 1);
  REQUIRE(res.filteredMessages[0].appearance.has_value());
  REQUIRE(res.filteredMessages[0].appearance->name == "Oberyn");
  REQUIRE_FALSE(actor.IsRaceMenuOpen());
}

TEST_CASE("CreateActor and SetUserActor accept an empty appearance",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  REQUIRE_NOTHROW(
    partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c));
  REQUIRE_NOTHROW(partOne.SetUserActor(0, 0xff000ABC));
  REQUIRE_FALSE(
    partOne.worldState.GetFormAt<MpActor>(0xff000ABC).HasStoredAppearance());
  REQUIRE(partOne.GetUserActor(0) == 0xff000ABC);
}

TEST_CASE("A faceless player sends no neighbour state until a face is stored",
          "[AppearancePublish][PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  partOne.SetUserActor(0, 0xff000ABC);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(1, 0xff000FFF);

  Equipment eq = actor.GetEquipment();
  eq.rightSpell = 0x12FCD;
  actor.SetEquipment(eq);

  const auto jAnimation =
    nlohmann::json{ { "t", MsgType::UpdateAnimation },
                    { "idx", 0 },
                    { "data",
                      { { "animEventName", "Idle" },
                        { "numChanges", 1 } } } };
  const auto jSpell = nlohmann::json{
    { "t", MsgType::SpellCast },
    { "data",
      { { "caster", 0x14 },
        { "target", 0 },
        { "spell", 0x12FCD },
        { "isDualCasting", false },
        { "interruptCast", true },
        { "castingSource", 0 },
        { "aimAngle", 0.f },
        { "aimHeading", 0.f },
        { "actorAnimationVariables",
          { { "booleans", nlohmann::json::array() },
            { "floats", nlohmann::json::array() },
            { "integers", nlohmann::json::array() } } } } }
  };

  partOne.Messages().clear();
  // Spell goes out before the equipment packet, which would clear the spell.
  DoMessage(partOne, 0, jSpell);
  DoMessage(partOne, 0, jMovement);
  DoMessage(partOne, 0, jAnimation);
  DoMessage(partOne, 0, jEquipment);

  auto sentToNeighbor = [&](MsgType type) {
    return std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                        [&](auto m) {
                          return m.j["t"] == type && m.userId == 1;
                        }) != partOne.Messages().end();
  };
  REQUIRE_FALSE(sentToNeighbor(MsgType::UpdateMovement));
  REQUIRE_FALSE(sentToNeighbor(MsgType::UpdateAnimation));
  REQUIRE_FALSE(sentToNeighbor(MsgType::UpdateEquipment));
  REQUIRE_FALSE(sentToNeighbor(MsgType::SpellCast));
  REQUIRE_FALSE(HasCreateActorFor(partOne, 1, 0));

  partOne.Messages().clear();
  GiveStoredAppearance(actor);
  REQUIRE(std::count_if(partOne.Messages().begin(), partOne.Messages().end(),
                        [](auto m) {
                          return m.j["t"] == MsgType::CreateActor &&
                            m.j["idx"] == 0 && m.userId == 1;
                        }) == 1);

  partOne.Messages().clear();
  DoMessage(partOne, 0, jMovement);
  REQUIRE(std::count_if(partOne.Messages().begin(), partOne.Messages().end(),
                        [](auto m) {
                          return m.j["t"] == MsgType::UpdateMovement &&
                            m.userId == 1;
                        }) == 1);
}
