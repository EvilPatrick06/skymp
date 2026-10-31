#include "BotClientBinding.h"

#include "BotClient.h"
#include <memory>
#include <string>

namespace {
const char* TypeName(Networking::PacketType type)
{
  switch (type) {
    case Networking::PacketType::Message:
      return "message";
    case Networking::PacketType::ClientSideConnectionAccepted:
      return "connectionAccepted";
    case Networking::PacketType::ClientSideConnectionFailed:
      return "connectionFailed";
    case Networking::PacketType::ClientSideConnectionDenied:
      return "connectionDenied";
    case Networking::PacketType::ClientSideDisconnect:
      return "disconnect";
    default:
      return "other";
  }
}

std::string Stringify(const Napi::Env& env, const Napi::Value& v)
{
  auto json = env.Global().Get("JSON").As<Napi::Object>();
  auto stringify = json.Get("stringify").As<Napi::Function>();
  auto s = stringify.Call(json, { v });
  if (!s.IsString()) {
    throw Napi::TypeError::New(env, "send: the message is not JSON");
  }
  return s.As<Napi::String>().Utf8Value();
}

Napi::Value Parse(const Napi::Env& env, const std::string& text)
{
  auto json = env.Global().Get("JSON").As<Napi::Object>();
  auto parse = json.Get("parse").As<Napi::Function>();
  return parse.Call(json, { Napi::String::New(env, text) });
}

Napi::Value CreateBotClient(const Napi::CallbackInfo& info)
{
  auto env = info.Env();
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsNumber()) {
    throw Napi::TypeError::New(
      env, "createBotClient(host, port, password = \"\", timeoutMs = 60000)");
  }
  std::string host = info[0].As<Napi::String>().Utf8Value();
  uint32_t port = info[1].As<Napi::Number>().Uint32Value();
  if (port == 0 || port > 65535) {
    throw Napi::RangeError::New(env, "createBotClient: port out of range");
  }
  std::string password;
  if (info.Length() >= 3 && info[2].IsString()) {
    password = info[2].As<Napi::String>().Utf8Value();
  }
  int timeoutMs = 60000;
  if (info.Length() >= 4 && info[3].IsNumber()) {
    timeoutMs = info[3].As<Napi::Number>().Int32Value();
  }

  std::shared_ptr<BotClient> bot;
  try {
    bot = std::make_shared<BotClient>(host, static_cast<uint16_t>(port),
                                      password, timeoutMs);
  } catch (std::exception& e) {
    throw Napi::Error::New(env, std::string("createBotClient: ") + e.what());
  }
  // destroy() lets the connection go while JavaScript still holds the
  // object, so every method checks it is there.
  auto holder = std::make_shared<std::shared_ptr<BotClient>>(bot);

  auto obj = Napi::Object::New(env);

  obj.Set("tick",
          Napi::Function::New(env, [holder](const Napi::CallbackInfo& info) {
            auto env = info.Env();
            auto arr = Napi::Array::New(env);
            if (!*holder) {
              return static_cast<Napi::Value>(arr);
            }
            std::vector<BotClient::Event> events;
            try {
              events = (*holder)->Tick();
            } catch (std::exception& e) {
              throw Napi::Error::New(env, std::string("tick: ") + e.what());
            }
            uint32_t i = 0;
            for (auto& event : events) {
              auto e = Napi::Object::New(env);
              e.Set("type", TypeName(event.type));
              if (event.type == Networking::PacketType::Message) {
                if (event.deserialized) {
                  e.Set("message", Parse(env, event.content));
                } else {
                  e.Set("raw", event.content);
                }
              }
              if (!event.error.empty()) {
                e.Set("error", event.error);
              }
              arr.Set(i++, e);
            }
            return static_cast<Napi::Value>(arr);
          }));

  obj.Set("send",
          Napi::Function::New(env, [holder](const Napi::CallbackInfo& info) {
            auto env = info.Env();
            if (!*holder) {
              throw Napi::Error::New(env, "send: the bot was destroyed");
            }
            if (info.Length() < 1 || !info[0].IsObject()) {
              throw Napi::TypeError::New(env, "send(message, reliable)");
            }
            std::string content = Stringify(env, info[0]);
            bool reliable = info.Length() < 2 || !info[1].IsBoolean() ||
              info[1].As<Napi::Boolean>().Value();
            try {
              (*holder)->Send(content, reliable);
            } catch (std::exception& e) {
              throw Napi::Error::New(env, std::string("send: ") + e.what());
            }
            return env.Undefined();
          }));

  obj.Set("isConnected",
          Napi::Function::New(env, [holder](const Napi::CallbackInfo& info) {
            return Napi::Boolean::New(info.Env(),
                                      *holder && (*holder)->IsConnected());
          }));

  obj.Set("destroy",
          Napi::Function::New(env, [holder](const Napi::CallbackInfo& info) {
            holder->reset();
            return info.Env().Undefined();
          }));

  return obj;
}
}

void BotClientBinding::Init(Napi::Env env, Napi::Object exports)
{
  exports.Set("createBotClient", Napi::Function::New(env, CreateBotClient));
}
