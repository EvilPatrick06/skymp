#include "SpellCastDurationMultBinding.h"
#include "NapiHelper.h"
#include "gamemode_events/SpellCastEvent.h"

namespace {
SpellCastEvent* FindSpellCastEvent(ScampServer& scampServer, uint32_t formId)
{
  auto& stack = scampServer.GetPartOne()->worldState.currentGameModeEventsStack;
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    auto cast = dynamic_cast<SpellCastEvent*>(*it);
    if (cast && cast->GetCasterId() == formId) {
      return cast;
    }
  }
  return nullptr;
}
}

Napi::Value SpellCastDurationMultBinding::Get(Napi::Env env,
                                              ScampServer& scampServer,
                                              uint32_t formId)
{
  auto cast = FindSpellCastEvent(scampServer, formId);
  return Napi::Number::New(env, cast ? cast->GetDurationMult() : 1.f);
}

void SpellCastDurationMultBinding::Set(Napi::Env env,
                                       ScampServer& scampServer,
                                       uint32_t formId, Napi::Value newValue)
{
  auto cast = FindSpellCastEvent(scampServer, formId);
  if (!cast) {
    spdlog::warn("SpellCastDurationMultBinding::Set - {:x} is not inside its "
                 "own onSpellCast event",
                 formId);
    return;
  }
  cast->SetDurationMult(
    NapiHelper::ExtractFloat(newValue, "spellCastDurationMult"));
}
