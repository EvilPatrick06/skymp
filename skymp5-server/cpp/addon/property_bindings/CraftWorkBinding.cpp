#include "CraftWorkBinding.h"
#include "NapiHelper.h"
#include "gamemode_events/CraftWorkEvent.h"
#include <spdlog/spdlog.h>

namespace {
CraftWorkEvent* FindCraftWorkEvent(ScampServer& scampServer, uint32_t formId)
{
  auto& stack =
    scampServer.GetPartOne()->worldState.currentGameModeEventsStack;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    auto work = dynamic_cast<CraftWorkEvent*>(*it);
    if (work && work->GetActorId() == formId) {
      return work;
    }
  }
  return nullptr;
}
}

Napi::Value CraftWorkBinding::Get(Napi::Env env, ScampServer& scampServer,
                                  uint32_t formId)
{
  auto work = FindCraftWorkEvent(scampServer, formId);
  if (!work) {
    return env.Undefined();
  }
  auto result = Napi::Object::New(env);
  result.Set(
    "ms",
    Napi::Number::New(env, static_cast<double>(work->GetWorkTime().count())));
  return result;
}

void CraftWorkBinding::Set(Napi::Env env, ScampServer& scampServer,
                           uint32_t formId, Napi::Value newValue)
{
  auto work = FindCraftWorkEvent(scampServer, formId);
  if (!work) {
    spdlog::warn("CraftWorkBinding::Set - {:x} is not inside its own "
                 "onCraftWork event",
                 formId);
    return;
  }

  auto value = NapiHelper::ExtractObject(newValue, "craftWork");
  auto ms = NapiHelper::ExtractUInt32(value.Get("ms"), "craftWork.ms");
  // SetWorkTime refuses a time past the most a craft may be held.
  work->SetWorkTime(std::chrono::milliseconds(ms));
}
