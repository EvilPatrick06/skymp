#include "DyingEvent.h"

#include "MpActor.h"
#include <cmath>
#include <spdlog/spdlog.h>

DyingEvent::DyingEvent(MpActor* actor_, MpActor* optionalKiller_,
                       float healthPercentageBeforeBlow_)
  : actorId(actor_ ? actor_->GetFormId() : 0)
  , killerId(optionalKiller_ ? optionalKiller_->GetFormId() : 0)
  , healthPercentageBeforeBlow(healthPercentageBeforeBlow_)
  , keptHealthPercentage(healthPercentageBeforeBlow_)
{
  if (!actor_) {
    spdlog::error("DyingEvent::DyingEvent - actor is nullptr");
  }
}

const char* DyingEvent::GetName() const
{
  return "onDying";
}

std::string DyingEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actorId);
  result += ",";
  result += std::to_string(killerId);
  result += ",";
  result += std::isfinite(healthPercentageBeforeBlow)
    ? std::to_string(healthPercentageBeforeBlow)
    : std::string("0");
  result += "]";
  return result;
}

uint32_t DyingEvent::GetActorId() const noexcept
{
  return actorId;
}

float DyingEvent::GetKeptHealthPercentage() const noexcept
{
  return keptHealthPercentage;
}

bool DyingEvent::SetKeptHealthPercentage(float healthPercentage)
{
  if (!std::isfinite(healthPercentage) || healthPercentage <= 0.f ||
      healthPercentage > 1.f) {
    return false;
  }
  keptHealthPercentage = healthPercentage;
  return true;
}
