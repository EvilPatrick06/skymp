#include "CraftWorkEvent.h"

#include "CraftEvent.h"
#include "MpActor.h"
#include <fmt/format.h>
#include <stdexcept>

CraftWorkEvent::CraftWorkEvent(
  MpActor* actor_, uint32_t craftedItemBaseId_, uint32_t count_,
  uint32_t recipeId_, const std::vector<Inventory::Entry>& entries_,
  uint32_t workbenchId_, const std::optional<std::vector<uint32_t>>& effects_)
  : actor(actor_)
  , craftedItemBaseId(craftedItemBaseId_)
  , count(count_)
  , recipeId(recipeId_)
  , workbenchId(workbenchId_)
  , entries(entries_)
  , effects(effects_)
{
}

const char* CraftWorkEvent::GetName() const
{
  return "onCraftWork";
}

std::string CraftWorkEvent::GetArgumentsJsonArray() const
{
  return CraftEvent::ArgumentsJsonArray(GetActorId(), craftedItemBaseId, count,
                                        recipeId, workbenchId, effects,
                                        entries);
}

uint32_t CraftWorkEvent::GetActorId() const
{
  return actor ? actor->GetFormId() : 0;
}

std::chrono::milliseconds CraftWorkEvent::GetWorkTime() const
{
  return workTime;
}

void CraftWorkEvent::SetWorkTime(std::chrono::milliseconds time)
{
  if (time.count() < 0 || time > kMaxWorkTime) {
    throw std::runtime_error(
      fmt::format("A craft's work time is 0 to {} ms, not {} ms",
                  kMaxWorkTime.count(), time.count()));
  }
  workTime = time;
}

void CraftWorkEvent::OnFireSuccess(WorldState*)
{
  // Nothing is taken or given here. CraftService holds the craft for
  // GetWorkTime() and then asks onCraft, which is what takes and gives.
}
