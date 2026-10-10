#pragma once
#include "GameModeEvent.h"
#include <cstdint>
#include <memory>

class MpActor;

class RespawnEvent : public GameModeEvent
{
public:
  explicit RespawnEvent(MpActor* actor_, bool shouldTeleport_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  void OnFireSuccess(WorldState*) override;

private:
  // event arguments
  std::shared_ptr<MpActor> actor;
  uint32_t actorId = 0;
  uint64_t runtimeIdentity = 0;

  // OnFireSuccess-specific arguments
  bool shouldTeleport = false;
};
