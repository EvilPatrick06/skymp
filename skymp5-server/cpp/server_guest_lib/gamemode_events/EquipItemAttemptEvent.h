#pragma once

#include <cstdint>

#include "GameModeEvent.h"

class MpActor;

// THORNSWOOD #2262. Asked once for each item an equipment update puts on that
// the server's record does not already have on, before the update is stored.
// A listener that returns false refuses that item alone: ActionListener keeps
// the rest of the update, stores the item as in the pack but not worn, and
// sends the owner's game an UnequipItem for it. Fired only while
// WorldState::equipItemAttemptEnabled is set (server setting
// "equipItemAttemptEnabled", off by default).
class EquipItemAttemptEvent : public GameModeEvent
{
public:
  EquipItemAttemptEvent(MpActor* actor_, uint32_t baseId_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

private:
  // The refusal is carried out by ActionListener, which holds the update.
  void OnFireSuccess(WorldState*) override {}
  void OnFireBlocked(WorldState*) override {}

  MpActor* actor = nullptr;
  uint32_t baseId = 0;
};
