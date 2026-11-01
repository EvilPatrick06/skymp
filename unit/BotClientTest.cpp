#include "BotClient.h"
#include "Networking.h"
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>
#include <functional>
#include <prometheus/core.h>
#include <thread>

/*
  Thornswood #5196. BotClient is a scripted bot's connection to a server,
  over the network like a game's. These run a real SLikeNet server in front of
  a PartOne, as ScampServer does, and check that the bot's bytes are a
  game's: the server's password with the protocol version in front, a login
  packet the server reads, the character the server sends its owner, and a
  movement the server applies.
*/

using namespace std::chrono_literals;

namespace {
constexpr uint16_t kBotTestPort = 7791;

struct BotRig
{
  BotRig(const std::string& serverPassword,
         std::shared_ptr<PartOne::Listener> listener = nullptr)
    : partOne(listener ? std::make_shared<PartOne>(listener)
                       : std::make_shared<PartOne>())
  {
    server =
      Networking::CreateServer("127.0.0.1", kBotTestPort, MAX_PLAYERS,
                               BotClient::PasswordFor(serverPassword).data(),
                               std::make_shared<prometheus::Registry>());
    partOne->SetSendTarget(server.get());
  }

  ~BotRig() { partOne->SetSendTarget(nullptr); }

  // Ticks the server and the bot until done() or about four seconds, and
  // keeps everything the bot read.
  bool Pump(BotClient& bot, const std::function<bool()>& done)
  {
    for (int i = 0; i < 2000; ++i) {
      server->Tick(PartOne::HandlePacket, partOne.get());
      auto got = bot.Tick();
      events.insert(events.end(), got.begin(), got.end());
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(2ms);
    }
    return false;
  }

  bool Has(Networking::PacketType type) const
  {
    for (auto& e : events) {
      if (e.type == type) {
        return true;
      }
    }
    return false;
  }

  std::optional<nlohmann::json> FirstMessage(MsgType t) const
  {
    for (auto& e : events) {
      if (e.type != Networking::PacketType::Message || !e.deserialized) {
        continue;
      }
      auto j = nlohmann::json::parse(e.content);
      if (j.value("t", -1) == static_cast<int>(t)) {
        return j;
      }
    }
    return std::nullopt;
  }

  std::shared_ptr<PartOne> partOne;
  std::shared_ptr<Networking::IServer> server;
  std::vector<BotClient::Event> events;
};
}

TEST_CASE("A bot client is refused with the wrong password", "[BotClient]")
{
  BotRig rig("right");
  BotClient bot("127.0.0.1", kBotTestPort, "wrong", 4000);

  REQUIRE(rig.Pump(bot, [&] {
    return rig.Has(Networking::PacketType::ClientSideConnectionDenied);
  }));
  REQUIRE(!bot.IsConnected());
  bool said = false;
  for (auto& e : rig.events) {
    if (e.type == Networking::PacketType::ClientSideConnectionDenied) {
      said = e.error == "Invalid password";
    }
  }
  REQUIRE(said);
}

TEST_CASE("A bot client logs in and is sent its character over the network",
          "[BotClient]")
{
  auto listener = FakeListener::New();
  BotRig rig("pw", listener);
  rig.partOne->CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);

  BotClient bot("127.0.0.1", kBotTestPort, "pw", 4000);
  REQUIRE(rig.Pump(bot, [&] {
    return rig.Has(Networking::PacketType::ClientSideConnectionAccepted) &&
      listener->str().find("OnConnect(0)") != std::string::npos;
  }));
  REQUIRE(bot.IsConnected());

  // The login packet, as skymp5-client's authService sends it.
  nlohmann::json login{ { "t", MsgType::CustomPacket },
                        { "contentJsonDump",
                          nlohmann::json{
                            { "customPacketType", "loginWithSkympIo" },
                            { "gameData", { { "profileId", 9001 } } } }
                            .dump() } };
  bot.Send(login.dump(), true);
  REQUIRE(rig.Pump(bot, [&] {
    return listener->str().find("loginWithSkympIo") != std::string::npos;
  }));
  REQUIRE_THAT(listener->str(),
               Catch::Matchers::ContainsSubstring(
                 "OnCustomPacket(0, "
                 "{\"customPacketType\":\"loginWithSkympIo\","
                 "\"gameData\":{\"profileId\":9001}})"));

  // What the login system does once it has said yes: the character goes to
  // its owner, and the bot reads it.
  rig.partOne->SetUserActor(0, 0xff000ABC);
  REQUIRE(rig.Pump(
    bot, [&] { return rig.FirstMessage(MsgType::CreateActor).has_value(); }));
  auto me = *rig.FirstMessage(MsgType::CreateActor);
  REQUIRE(me["isMe"] == true);
  REQUIRE(me["transform"]["pos"] == nlohmann::json::array({ 1.f, 2.f, 3.f }));
  REQUIRE(me["transform"]["worldOrCell"] == 0x3c);
  REQUIRE(me["props"]["health"] == 100.f);

  // The bot moves, and the server applies it to its character.
  auto move = jMovement;
  move["idx"] = me["idx"];
  bot.Send(move.dump(), false);
  auto& actor = rig.partOne->worldState.GetFormAt<MpActor>(0xff000ABC);
  REQUIRE(rig.Pump(
    bot, [&] { return actor.GetPos() == NiPoint3{ 1.f, -1.f, 1.f }; }));
}
