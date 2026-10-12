#include "EatItemScaleBinding.h"
#include "NapiHelper.h"
#include "gamemode_events/EatItemEvent.h"

namespace {
EatItemEvent* FindEatItemEvent(ScampServer& scampServer, uint32_t formId)
{
  auto& stack = scampServer.GetPartOne()->worldState.currentGameModeEventsStack;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    auto eat = dynamic_cast<EatItemEvent*>(*it);
    if (eat && eat->GetActorId() == formId) {
      return eat;
    }
  }
  return nullptr;
}
}

Napi::Value EatItemScaleBinding::Get(Napi::Env env, ScampServer& scampServer,
                                     uint32_t formId)
{
  auto eat = FindEatItemEvent(scampServer, formId);
  return Napi::Number::New(env, eat ? eat->GetEffectScale() : 1.f);
}

void EatItemScaleBinding::Set(Napi::Env env, ScampServer& scampServer,
                              uint32_t formId, Napi::Value newValue)
{
  auto eat = FindEatItemEvent(scampServer, formId);
  if (!eat) {
    spdlog::warn("EatItemScaleBinding::Set - {:x} is not inside its own "
                 "onEatItem event",
                 formId);
    return;
  }
  eat->SetEffectScale(NapiHelper::ExtractFloat(newValue, "eatItemScale"));
}
