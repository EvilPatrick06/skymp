#pragma once
#include "GameModeEvent.h"
#include <cstdint>

class MpActor;

// Thornswood #1626. Asked before the engine kills an actor whose health a
// write takes to nothing (MpActor::NetSetPercentages, MpActor::SetPercentage),
// before the zero or the death is sent to any machine. onDeath comes too late
// for this: Kill has told every machine by the time it runs.
//
// A listener that returns false refuses the death. Nothing about it is sent
// and the actor stays alive at the kept health, a share of its most like the
// percentages property: the health before the blow, unless the listener wrote
// another (the dyingHealth property, readable and writable only inside its
// own onDying). Arguments: the actor, the killer (0 for none), and the health
// before the blow.
class DyingEvent : public GameModeEvent
{
public:
  DyingEvent(MpActor* actor_, MpActor* optionalKiller_,
             float healthPercentageBeforeBlow_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  uint32_t GetActorId() const noexcept;
  float GetKeptHealthPercentage() const noexcept;

  // A share in (0, 1]. False, and nothing kept, for anything else: an actor
  // cannot be kept alive at nothing.
  bool SetKeptHealthPercentage(float healthPercentage);

private:
  void OnFireSuccess(WorldState*) override {}

  uint32_t actorId = 0;
  uint32_t killerId = 0;
  float healthPercentageBeforeBlow = 0.f;
  float keptHealthPercentage = 0.f;
};
