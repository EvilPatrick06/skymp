#include "CraftEvent.h"

#include "MpActor.h"

CraftEvent::CraftEvent(MpActor* actor_, uint32_t craftedItemBaseId_,
                       uint32_t count_, uint32_t recipeId_,
                       const std::vector<Inventory::Entry>& entries_,
                       uint32_t workbenchId_)
  : actor(actor_)
  , craftedItemBaseId(craftedItemBaseId_)
  , count(count_)
  , recipeId(recipeId_)
  , workbenchId(workbenchId_)
  , entries(entries_)
{
}

const char* CraftEvent::GetName() const
{
  return "onCraft";
}

std::string CraftEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actor->GetFormId());
  result += ",";
  result += std::to_string(craftedItemBaseId);
  result += ",";
  result += std::to_string(count);
  result += ",";
  result += std::to_string(recipeId);
  result += ",";
  result += std::to_string(workbenchId);
  /*
    THORNSWOOD PATCH (Thornswood #1648). The inputs, as a seventh argument.

    A soul gem is an input to an enchantment, and the rank it needs lives in
    the gamemode (docs/systems/enchantments.md: Petty from novice, Lesser at
    advanced, Common at expert, Greater, Grand and Black at master). Until
    this the inputs stayed here and never reached mp.onCraft, so nothing could
    refuse a greater gem in a novice's pack. They go over as
    [{"baseId":n,"count":n}, ...], the same entries OnFireSuccess removes, so
    what the gamemode judges is what would be taken. A refusal runs no
    OnFireSuccess, so the gem and the piece both stay in the pack.

    The sixth argument is the effects slot #1389 reserves (an array of MGEF
    ids). The engine names none yet, so it is null, which the gamemode reads
    as "not named". Positions are fixed so neither change moves the other.
  */
  result += ",null,[";
  bool first = true;
  for (auto& entry : entries) {
    if (!first) {
      result += ",";
    }
    first = false;
    result += "{\"baseId\":";
    result += std::to_string(entry.baseId);
    result += ",\"count\":";
    result += std::to_string(entry.count);
    result += "}";
  }
  result += "]";
  result += "]";
  return result;
}

void CraftEvent::OnFireSuccess(WorldState*)
{
  actor->RemoveItems(entries);
  actor->AddItem(craftedItemBaseId, count);
}
