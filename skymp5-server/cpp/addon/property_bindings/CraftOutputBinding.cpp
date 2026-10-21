#include "CraftOutputBinding.h"
#include "NapiHelper.h"
#include "gamemode_events/CraftEvent.h"
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace {
CraftEvent* FindCraftEvent(ScampServer& scampServer, uint32_t formId)
{
  auto& stack =
    scampServer.GetPartOne()->worldState.currentGameModeEventsStack;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    auto craft = dynamic_cast<CraftEvent*>(*it);
    if (craft && craft->GetActorId() == formId) {
      return craft;
    }
  }
  return nullptr;
}
}

Napi::Value CraftOutputBinding::Get(Napi::Env env, ScampServer& scampServer,
                                    uint32_t formId)
{
  auto craft = FindCraftEvent(scampServer, formId);
  if (!craft) {
    return env.Undefined();
  }
  auto output = Napi::Object::New(env);
  output.Set("baseId", Napi::Number::New(env, craft->GetOutputBaseId()));
  output.Set("count", Napi::Number::New(env, craft->GetOutputCount()));
  return output;
}

void CraftOutputBinding::Set(Napi::Env env, ScampServer& scampServer,
                             uint32_t formId, Napi::Value newValue)
{
  auto craft = FindCraftEvent(scampServer, formId);
  if (!craft) {
    spdlog::warn("CraftOutputBinding::Set - {:x} is not inside its own "
                 "onCraft event",
                 formId);
    return;
  }

  auto output = NapiHelper::ExtractObject(newValue, "craftOutput");
  uint32_t baseId =
    NapiHelper::ExtractUInt32(output.Get("baseId"), "craftOutput.baseId");
  uint32_t count = craft->GetOutputCount();
  if (output.Has("count") && !output.Get("count").IsUndefined()) {
    count =
      NapiHelper::ExtractUInt32(output.Get("count"), "craftOutput.count");
  }

  // An item the server's records do not hold would land in the pack as a
  // base id nothing can show, eat or sell, so it is refused here, before the
  // craft gives anything.
  auto& worldState = scampServer.GetPartOne()->worldState;
  if (worldState.HasEspm() &&
      !worldState.GetEspm().GetBrowser().LookupById(baseId).rec) {
    throw std::runtime_error(
      fmt::format("craftOutput.baseId {:#x} is not a record", baseId));
  }

  craft->SetOutput(baseId, count);
}
