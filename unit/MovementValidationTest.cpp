#include <catch2/catch_all.hpp>

#include "MovementValidation.h"
#include "MsgType.h"
#include "NiPoint3.h"
#include "TestUtils.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

using Catch::Matchers::ContainsSubstring;

extern PartOne& GetPartOne();

namespace {
size_t CountSent(PartOne& partOne, MsgType t, Networking::UserId userId = 0)
{
  return std::count_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](const PartOne::Message& m) {
                         return m.j["t"] == t && m.userId == userId;
                       });
}

const nlohmann::json* FindSent(PartOne& partOne, MsgType t)
{
  for (auto& m : partOne.Messages()) {
    if (m.j["t"] == t && m.userId == 0) {
      return &m.j;
    }
  }
  return nullptr;
}

void SendMovement(PartOne& partOne, MpActor& actor, uint32_t worldOrCell,
                  NiPoint3 pos, std::optional<uint32_t> teleportSeq)
{
  auto j = jMovement;
  j["idx"] = actor.GetIdx();
  j["data"]["worldOrCell"] = worldOrCell;
  j["data"]["pos"] = { pos.x, pos.y, pos.z };
  if (teleportSeq) {
    j["data"]["teleportSeq"] = *teleportSeq;
  }
  DoMessage(partOne, 0, j);
}
}

TEST_CASE("Returns true and sends nothing for normal movement",
          "[MovementValidation]")
{
  PartOne& partOne = GetPartOne();

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);

  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  partOne.Messages().clear();
  bool res = MovementValidation::Validate(
    partOne, { 0, 0, 0 }, { 0, 0, 0 }, FormDesc::Tamriel(), { 1, 1, 1 },
    FormDesc::Tamriel(), 0, &actor,
    espm::LoadOrder::FullPlugins({ "Skyrim.esm" }));
  REQUIRE(res);
  REQUIRE(partOne.Messages().empty());
}

TEST_CASE("Returns false and sends teleport packet when moving too fast",
          "[MovementValidation]")
{
  PartOne& partOne = GetPartOne();

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);

  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  partOne.Messages().clear();
  float maxLegalMove = 4096.f;
  bool res = MovementValidation::Validate(
    partOne, { 1, -1, 1 }, { 123, 111, 123 }, FormDesc::Tamriel(),
    NiPoint3{ 1, -1, 1 } + NiPoint3{ maxLegalMove + 1.f, 0, 0 },
    FormDesc::Tamriel(), 0, &actor,
    espm::LoadOrder::FullPlugins({ "Skyrim.esm" }));
  REQUIRE(!res);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j ==
          nlohmann::json{ { "t", static_cast<int>(MsgType::Teleport2) },
                          { "pos", { 1, -1, 1 } },
                          { "rot", { 123, 111, 123 } },
                          { "worldOrCell", 0x3c },
                          { "teleportSeq", 1 } });
  REQUIRE(partOne.Messages()[0].userId == 0);
}

TEST_CASE(
  "Returns false and sends teleport packet when moving between locations",
  "[MovementValidation]")
{
  PartOne& partOne = GetPartOne();

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);

  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  partOne.Messages().clear();
  bool res = MovementValidation::Validate(
    partOne, { 1, -1, 1 }, { 123, 111, 123 }, FormDesc::Tamriel(),
    { 1, -1, 1 }, FormDesc::FromString("ffffff:Skyrim.esm"), 0, &actor,
    espm::LoadOrder::FullPlugins({ "Skyrim.esm" }));
  REQUIRE(!res);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j ==
          nlohmann::json{ { "t", static_cast<int>(MsgType::Teleport2) },
                          { "pos", { 1, -1, 1 } },
                          { "rot", { 123, 111, 123 } },
                          { "worldOrCell", 0x3c },
                          { "teleportSeq", 1 } });
  REQUIRE(partOne.Messages()[0].userId == 0);
}

// Thornswood #1932. Measured on dev on 5 Oct 2026: the server stalled 48 s,
// a door it then processed moved the character, and each of the 232 movement
// packets the client had sent from the old place in the meantime got a
// TeleportMessage2 of its own: 233 teleports to one spot in 0.54 s on the
// client. Here the server move is MpActor::Teleport (no espm needed), and a
// second character stands where the first is moved to.
TEST_CASE("Movement sent before a teleport is carried out is not answered",
          "[MovementValidation]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000000));
  partOne.SetUserActor(0, 0xff000000);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000001, { 20000, 0, 0 }, 0, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000001));
  partOne.SetUserActor(1, 0xff000001);

  // A client that has carried out no teleport yet echoes 0.
  SendMovement(partOne, actor, 0x3c, { 0, 0, 0 }, 0);

  partOne.Messages().clear();
  actor.Teleport({ { 20000, 0, 0 }, { 0, 0, 0 }, FormDesc::Tamriel() });
  REQUIRE(CountSent(partOne, MsgType::Teleport) == 1);
  const uint32_t moved =
    FindSent(partOne, MsgType::Teleport)->value("teleportSeq", 0u);

  // Sent before the client had the teleport: not applied, not answered, not
  // passed on to the character standing there.
  partOne.Messages().clear();
  for (int i = 0; i < 232; ++i) {
    SendMovement(partOne, actor, 0x3c, { static_cast<float>(i % 7), 0, 0 }, 0);
  }
  CHECK(CountSent(partOne, MsgType::Teleport2) == 0);
  CHECK(CountSent(partOne, MsgType::UpdateMovement, 1) == 0);
  REQUIRE(actor.GetPos() == NiPoint3{ 20000, 0, 0 });
  REQUIRE(moved > 0);

  // Sent after carrying it out, from the new place: applied and passed on.
  SendMovement(partOne, actor, 0x3c, { 20001, 0, 0 }, moved);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 0);
  REQUIRE(CountSent(partOne, MsgType::UpdateMovement, 1) == 1);
  REQUIRE(actor.GetPos() == NiPoint3{ 20001, 0, 0 });

  // Carried out, yet still far away (the move did not take): one numbered
  // correction, and nothing more for packets sent before it arrives.
  partOne.Messages().clear();
  for (int i = 0; i < 10; ++i) {
    SendMovement(partOne, actor, 0x3c, { 0, 0, 0 }, moved);
  }
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 1);
  const auto& correction = *FindSent(partOne, MsgType::Teleport2);
  REQUIRE(correction["pos"] == nlohmann::json{ 20001, 0, 0 });
  const uint32_t corrected = correction.value("teleportSeq", 0u);
  REQUIRE(corrected > moved);

  partOne.Messages().clear();
  SendMovement(partOne, actor, 0x3c, { 20002, 0, 0 }, corrected);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 0);
  REQUIRE(actor.GetPos() == NiPoint3{ 20002, 0, 0 });

  // A client that sends no number is validated as before.
  partOne.Messages().clear();
  SendMovement(partOne, actor, 0x3c, { 0, 0, 0 }, std::nullopt);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 1);
}

TEST_CASE("A new connection to a character has no teleport outstanding",
          "[MovementValidation]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  // The game is closed before this teleport is carried out.
  actor.Teleport({ { 20000, 0, 0 }, { 0, 0, 0 }, FormDesc::Tamriel() });
  DoDisconnect(partOne, 0);

  // The new client echoes 0 and is believed.
  DoConnect(partOne, 0);
  partOne.SetUserActor(0, 0xff000000);
  partOne.Messages().clear();
  SendMovement(partOne, actor, 0x3c, { 20001, 0, 0 }, 0);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 0);
  REQUIRE(actor.GetPos() == NiPoint3{ 20001, 0, 0 });
}

// Thornswood #1932, the order the dev client sent things in on 5 Oct 2026:
// inside the Bannered Mare (1605e), 23 presses of its door 16072 with
// movement every 130 ms in between, all held by the stall. The first press
// was processed and moved the character to the door's destination outside;
// the server refused the presses after it ("WorldSpace doesn't match ...
// target is in WhiterunBanneredMare", 21 lines in the dev server.log at
// 11:51:15.865), and the 232 movement packets got 232 TeleportMessage2.
TEST_CASE("Held door presses and the movement behind them, dev 5 Oct 2026",
          "[MovementValidation][espm]")
{
  auto& partOne = GetPartOne();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { -113.6f, -810.f, 69.3f }, 0, 0x1605e);
  partOne.SetUserActor(0, 0xff000000);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  constexpr uint32_t kDoor = 0x16072;
  auto press = [&] {
    DoMessage(partOne, 0,
              nlohmann::json{ { "t", MsgType::Activate },
                              { "data",
                                { { "caster", 0x14 },
                                  { "target", kDoor },
                                  { "isSecondActivation", false } } } });
  };
  auto inside = [&](int i) {
    SendMovement(partOne, actor, 0x1605e,
                 { -113.6f + static_cast<float>(i % 7), -810.f, 69.3f }, 0);
  };

  inside(0);
  partOne.Messages().clear();
  press();
  REQUIRE(CountSent(partOne, MsgType::Teleport) == 1);
  const auto door = *FindSent(partOne, MsgType::Teleport);
  REQUIRE(door["worldOrCell"] == 0x1a26f);
  REQUIRE(std::abs(door["pos"][0].get<float>() - 25669.7f) < 1.f);
  REQUIRE(std::abs(door["pos"][1].get<float>() - -7632.93f) < 1.f);
  const uint32_t moved = door.value("teleportSeq", 0u);
  REQUIRE(actor.GetCellOrWorld().ToFormId(partOne.worldState.espmFiles) ==
          0x1a26f);

  partOne.Messages().clear();
  int presses = 1;
  for (int i = 0; i < 232; ++i) {
    inside(i);
    if (i % 10 == 5 && presses < 23) {
      REQUIRE_THROWS_WITH(press(),
                          ContainsSubstring("WorldSpace doesn't match"));
      presses++;
    }
  }
  REQUIRE(presses == 23);
  REQUIRE(CountSent(partOne, MsgType::Teleport) == 0);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 0);

  // The client carried the door teleport out and reports from outside.
  SendMovement(partOne, actor, 0x1a26f, { 25670.f, -7633.f, -3237.f }, moved);
  REQUIRE(CountSent(partOne, MsgType::Teleport2) == 0);
  REQUIRE(actor.GetPos() == NiPoint3{ 25670.f, -7633.f, -3237.f });

  DoDisconnect(partOne, 0);
  partOne.DestroyActor(0xff000000);
}
