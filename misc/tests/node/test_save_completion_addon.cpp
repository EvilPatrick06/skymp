// Compile the production addon methods with Node-API and controlled native
// dependencies. No game server, sockets, worker or private world is created.
#include <napi.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

struct ControlledWorld
{
  std::string mode;
  bool durable = false;
  unsigned calls = 0;
  uint32_t timeout = 0;
  struct Result { bool supportsDurableUpsert; };
  Result CompleteSavesForShutdown(uint32_t value)
  {
    ++calls;
    timeout = value;
    if (mode == "standard") {
      mode = "failed";
      throw std::runtime_error("controlled storage error");
    }
    if (mode == "nonstandard") {
      mode = "failed";
      throw 17;
    }
    EnsureTickAllowed();
    mode = "completed";
    return { durable };
  }
  void EnsureTickAllowed()
  {
    if (mode == "completed" || mode == "failed")
      throw std::runtime_error("terminal world refuses ticking");
  }
};
struct PartOne
{
  ControlledWorld worldState;
  unsigned ticks = 0;
  static void HandlePacket() {}
  void Tick() { ++ticks; }
};
struct ControlledNetwork
{
  unsigned ticks = 0;
  template <class Callback> void Tick(Callback, void*) { ++ticks; }
};
struct ControlledLogger
{
  template <class... Args> void error(Args&&...) {}
};
namespace antigo {
struct Witness { std::string ToString() { return {}; } };
inline bool HasExceptionWitness() { return false; }
inline bool HasExceptionWitnessOrphan() { return false; }
inline Witness PopExceptionWitness() { return {}; }
inline Witness PopExceptionWitnessOrphan() { return {}; }
}
#define ANTIGO_CONTEXT_INIT(ctx)

class ScampServer : public Napi::ObjectWrap<ScampServer>
{
public:
  explicit ScampServer(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ScampServer>(info)
  {
    if (info.Length() > 0) partOne->worldState.mode = info[0].As<Napi::String>();
    if (info.Length() > 1) partOne->worldState.durable = info[1].As<Napi::Boolean>();
  }
  Napi::Value CompleteSavesForShutdown(const Napi::CallbackInfo& info);
  Napi::Value Tick(const Napi::CallbackInfo& info);
  Napi::Value Inspect(const Napi::CallbackInfo& info)
  {
    auto result = Napi::Object::New(info.Env());
    result.Set("calls", partOne->worldState.calls);
    result.Set("timeoutMs", partOne->worldState.timeout);
    result.Set("networkTicks", server->ticks);
    result.Set("gameplayTicks", partOne->ticks);
    result.Set("tickEnvSet", static_cast<napi_env>(tickEnv) != nullptr);
    return result;
  }
  static Napi::Object Init(Napi::Env env, Napi::Object exports)
  {
    exports.Set("ScampServer", DefineClass(env, "ScampServer", {
      InstanceMethod("completeSavesForShutdown", &ScampServer::CompleteSavesForShutdown),
      InstanceMethod("tick", &ScampServer::Tick),
      InstanceMethod("inspect", &ScampServer::Inspect),
    }));
    return exports;
  }
private:
  std::shared_ptr<PartOne> partOne = std::make_shared<PartOne>();
  std::shared_ptr<ControlledNetwork> server = std::make_shared<ControlledNetwork>();
  std::shared_ptr<ControlledLogger> logger = std::make_shared<ControlledLogger>();
  Napi::Env tickEnv{ nullptr };
};

#include "SaveCompletionMethods.inc"
Napi::Object InitFixture(Napi::Env env, Napi::Object exports)
{
  return ScampServer::Init(env, exports);
}
NODE_API_MODULE(save_completion_fixture, InitFixture)
