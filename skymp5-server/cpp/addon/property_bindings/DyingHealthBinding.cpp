#include "DyingHealthBinding.h"
#include "NapiHelper.h"
#include "gamemode_events/DyingEvent.h"

DyingEvent* DyingHealthBinding::FindDyingEvent(ScampServer& scampServer,
                                               uint32_t formId)
{
  auto& stack =
    scampServer.GetPartOne()->worldState.currentGameModeEventsStack;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    auto dying = dynamic_cast<DyingEvent*>(*it);
    if (dying && dying->GetActorId() == formId) {
      return dying;
    }
  }
  return nullptr;
}

Napi::Value DyingHealthBinding::Get(Napi::Env env, ScampServer& scampServer,
                                    uint32_t formId)
{
  auto dying = FindDyingEvent(scampServer, formId);
  return Napi::Number::New(env,
                           dying ? dying->GetKeptHealthPercentage() : 0.f);
}

void DyingHealthBinding::Set(Napi::Env env, ScampServer& scampServer,
                             uint32_t formId, Napi::Value newValue)
{
  auto dying = FindDyingEvent(scampServer, formId);
  if (!dying) {
    spdlog::warn("DyingHealthBinding::Set - {:x} is not inside its own "
                 "onDying event",
                 formId);
    return;
  }
  const float health = NapiHelper::ExtractFloat(newValue, "dyingHealth");
  if (!dying->SetKeptHealthPercentage(health)) {
    throw std::runtime_error(
      "dyingHealth must be above 0 and at most 1, got " +
      std::to_string(health));
  }
}
