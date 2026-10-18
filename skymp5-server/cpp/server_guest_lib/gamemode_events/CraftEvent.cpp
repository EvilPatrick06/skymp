#include "CraftEvent.h"

#include "MpActor.h"
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

CraftEvent::CraftEvent(MpActor* actor_, uint32_t craftedItemBaseId_,
                       uint32_t count_, uint32_t recipeId_,
                       const std::vector<Inventory::Entry>& entries_,
                       uint32_t workbenchId_,
                       std::optional<std::vector<uint32_t>> effects_,
                       std::optional<Inventory::Made> made_)
  : actor(actor_)
  , craftedItemBaseId(craftedItemBaseId_)
  , count(count_)
  , recipeId(recipeId_)
  , workbenchId(workbenchId_)
  , effects(std::move(effects_))
  , made(std::move(made_))
  , entries(entries_)
  , outputBaseId(craftedItemBaseId_)
  , outputCount(count_)
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

    The sixth argument is the effects slot #1389 reserves: an array of MGEF
    ids when the server worked them out (a brew, CraftService::BrewEffects),
    and null otherwise, which the gamemode reads as "not named". Positions
    are fixed so neither change moves the other.
  */
  result += ",";
  if (effects) {
    result += "[";
    for (size_t i = 0; i < effects->size(); ++i) {
      if (i) {
        result += ",";
      }
      result += std::to_string((*effects)[i]);
    }
    result += "]";
  } else {
    result += "null";
  }
  result += ",[";
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

uint32_t CraftEvent::GetActorId() const
{
  return actor ? actor->GetFormId() : 0;
}

uint32_t CraftEvent::GetCraftedItemBaseId() const
{
  return craftedItemBaseId;
}

uint32_t CraftEvent::GetOutputBaseId() const
{
  return outputBaseId;
}

uint32_t CraftEvent::GetOutputCount() const
{
  return outputCount;
}

void CraftEvent::SetOutput(uint32_t baseId, uint32_t count_)
{
  if (baseId == 0 || count_ == 0) {
    throw std::runtime_error(
      "A craft's output needs a base id and a count of at least 1");
  }
  outputBaseId = baseId;
  outputCount = count_;
}

const std::optional<std::string>& CraftEvent::GetOutputName() const
{
  return outputName;
}

void CraftEvent::SetOutputName(std::optional<std::string> name)
{
  if (name && name->empty()) {
    throw std::runtime_error(
      "A craft's output name needs at least one character; null names none");
  }
  outputName = std::move(name);
}

void CraftEvent::OnFireSuccess(WorldState*)
{
  Inventory::ExtraData extraData;
  extraData.name = outputName;
  // Thornswood #1649: the brewed bottle goes in by its contents, see
  // CraftService. Only while the output is still that bottle: a listener that
  // names another record (#2906) gets that record, which has contents of its
  // own, and a double batch is the same bottle twice.
  if (made && outputBaseId == craftedItemBaseId) {
    extraData.made = made;
  }
  // Refused before the inputs are taken, so a pack that can't hold the
  // output keeps them.
  Inventory afterInputs = actor->GetInventory();
  afterInputs.RemoveItems(entries);
  if (!afterInputs.CanAddItems(
        { Inventory::Entry(outputBaseId, outputCount, extraData) })) {
    throw std::runtime_error(fmt::format(
      "CraftEvent - actor {:#x} can't take {:#x} x{}: a stack would pass its "
      "limit",
      actor->GetFormId(), outputBaseId, outputCount));
  }

  actor->RemoveItems(entries);
  if (outputBaseId != craftedItemBaseId || outputCount != count ||
      outputName) {
    spdlog::info("CraftEvent - the gamemode named the output of actor "
                 "{:#x}'s craft: {:#x} x{}{} in place of {:#x} x{}",
                 actor->GetFormId(), outputBaseId, outputCount,
                 outputName ? " '" + *outputName + "'" : std::string(),
                 craftedItemBaseId, count);
  }
  actor->AddItem(outputBaseId, outputCount, extraData);
}
