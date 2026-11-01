#include "BotClient.h"

#include "Config.h"
#include "MessageSerializerFactory.h"
#include "Networking.h"
#include <nlohmann/json.hpp>
#include <slikenet/BitStream.h>
#include <tuple>

BotClient::BotClient(const std::string& host, uint16_t port,
                     const std::string& serverPassword, int timeoutMs)
  : serializer(MessageSerializerFactory::CreateMessageSerializer())
{
  const std::string password = PasswordFor(serverPassword);
  cl = Networking::CreateClient(host.data(), port, timeoutMs, password.data());
}

BotClient::~BotClient() = default;

std::string BotClient::PasswordFor(const std::string& serverPassword)
{
  return std::string(kNetworkingPasswordPrefix) + serverPassword;
}

std::vector<BotClient::Event> BotClient::Tick()
{
  std::vector<Event> events;
  if (!cl) {
    return events;
  }

  std::tuple<std::vector<Event>*, MessageSerializer*> locals(&events,
                                                             serializer.get());

  cl->Tick(
    [](void* rawState, Networking::PacketType packetType,
       Networking::PacketData data, size_t length, const char* error) {
      auto& [out, messageSerializer] = *reinterpret_cast<
        std::tuple<std::vector<Event>*, MessageSerializer*>*>(rawState);

      Event event;
      event.type = packetType;
      event.error = error ? error : "";

      if (packetType == Networking::PacketType::Message && length > 0) {
        // As MpClientPlugin::Tick: the serializer first, and the text after
        // the packet id when it does not know the packet.
        auto result = messageSerializer->Deserialize(data, length);
        if (result) {
          nlohmann::json j;
          result->message->WriteJson(j);
          event.content = j.dump();
          event.deserialized = true;
        } else {
          event.content =
            std::string(reinterpret_cast<const char*>(data) + 1, length - 1);
        }
      }

      out->push_back(std::move(event));
    },
    &locals);

  return events;
}

void BotClient::Send(const std::string& jsonContent, bool reliable)
{
  if (!cl) {
    return;
  }
  SLNet::BitStream stream;
  serializer->Serialize(jsonContent.data(), stream);
  cl->Send(stream.GetData(), stream.GetNumberOfBytesUsed(), reliable);
}

bool BotClient::IsConnected() const
{
  return cl && cl->IsConnected();
}
