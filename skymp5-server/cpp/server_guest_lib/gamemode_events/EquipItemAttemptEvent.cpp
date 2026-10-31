#include "EquipItemAttemptEvent.h"

#include "MpActor.h"

EquipItemAttemptEvent::EquipItemAttemptEvent(MpActor* actor_,
                                             uint32_t baseId_)
  : actor(actor_)
  , baseId(baseId_)
{
}

const char* EquipItemAttemptEvent::GetName() const
{
  return "onEquipItemAttempt";
}

std::string EquipItemAttemptEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actor ? actor->GetFormId() : 0);
  result += ",";
  result += std::to_string(baseId);
  result += "]";
  return result;
}
