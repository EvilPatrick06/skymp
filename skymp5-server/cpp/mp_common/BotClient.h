#pragma once
#include "NetworkingInterface.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MessageSerializer;

/*
  THORNSWOOD (Thornswood #5196). A client of the server for scripted test
  bots, outside the game.

  It talks to the server the way the game's own client does: the same
  SLikeNet connection and password (the protocol version, then the server's
  password), and every message through the same MessageSerializer that
  MpClientPlugin uses, so what a bot sends and reads is byte for byte what a
  game would. What MpClientPlugin hands Skyrim Platform as JSON text, this
  hands back as JSON text too, so a bot script reads the same messages
  skymp5-client reads.

  The Node addon exposes it as createBotClient (BotClientBinding.cpp), and
  unit/BotClientTest.cpp drives it against a real server and PartOne.
*/
class BotClient
{
public:
  struct Event
  {
    Networking::PacketType type = Networking::PacketType::Invalid;
    // For a message: its JSON when the serializer read it, or the text after
    // the packet id when it did not (the server's older JSON-text packets,
    // which skymp5-client reads the same way). Empty otherwise.
    std::string content;
    bool deserialized = false;
    // For a refused or failed connection, what SLikeNet said.
    std::string error;
  };

  // serverPassword is the server's "password" setting, empty when it has
  // none; the protocol version is put in front of it here, as the server and
  // MpClientPlugin do.
  BotClient(const std::string& host, uint16_t port,
            const std::string& serverPassword, int timeoutMs);
  ~BotClient();

  // Everything that arrived since the last call, in order.
  std::vector<Event> Tick();

  // jsonContent is a message as skymp5-client sends it ({ "t": ..., ... }).
  void Send(const std::string& jsonContent, bool reliable);

  bool IsConnected() const;

  static std::string PasswordFor(const std::string& serverPassword);

private:
  std::shared_ptr<Networking::IClient> cl;
  std::shared_ptr<MessageSerializer> serializer;
};
