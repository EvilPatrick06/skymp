#include "DeathEvent.h"

#include "MpActor.h"
#include "WorldState.h"
#include <spdlog/spdlog.h>

DeathEvent::DeathEvent(MpActor* actor_, MpActor* optionalKiller_,
                       float healthPercentageBeforeDeath_,
                       float magickaPercentageBeforeDeath_,
                       float staminaPercentageBeforeDeath_)
  : actorId(actor_ ? actor_->GetFormId() : 0)
  , killerId(optionalKiller_ ? optionalKiller_->GetFormId() : 0)
  , runtimeIdentity(actor_ ? actor_->GetRuntimeIdentity() : 0)
  , healthPercentageBeforeDeath(healthPercentageBeforeDeath_)
  , magickaPercentageBeforeDeath(magickaPercentageBeforeDeath_)
  , staminaPercentageBeforeDeath(staminaPercentageBeforeDeath_)
{
  if (actor_ && actor_->GetParent()) {
    actor = std::dynamic_pointer_cast<MpActor>(
      actor_->GetParent()->LookupFormById(actorId));
    if (actor.get() != actor_) actor.reset();
  }
  if (!actor_) {
    spdlog::error("DeathEvent::DeathEvent - actor is nullptr");
  }
}

const char* DeathEvent::GetName() const
{
  // keep in sync with ScampServer::IsGameModeInsideDeathEventHandler
  return "onDeath";
}

std::string DeathEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actorId);
  result += ",";
  result += std::to_string(killerId);
  result += "]";
  return result;
}

uint32_t DeathEvent::GetDyingActorId() const
{
  if (!actor) {
    spdlog::error("DeathEvent::GetDyingActorId - actor is nullptr");
    return 0;
  }
  return actorId;
}

float DeathEvent::GetHealthPercentageBeforeDeath() const noexcept
{
  return healthPercentageBeforeDeath;
}

float DeathEvent::GetMagickaPercentageBeforeDeath() const noexcept
{
  return magickaPercentageBeforeDeath;
}

float DeathEvent::GetStaminaPercentageBeforeDeath() const noexcept
{
  return staminaPercentageBeforeDeath;
}

void DeathEvent::OnFireSuccess(WorldState*)
{
  if (actor && actor->HasRuntimeIdentity(runtimeIdentity)) {
    actor->RespawnWithDelay();
  }
};
