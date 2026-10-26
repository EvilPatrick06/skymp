#include "CraftEvent.h"

#include "MpActor.h"
#include "WorldState.h"
#include <algorithm>
#include <fmt/format.h>
#include <map>
#include <spdlog/spdlog.h>
#include <stdexcept>

CraftEvent::CraftEvent(MpActor* actor_, uint32_t craftedItemBaseId_,
                       uint32_t count_, uint32_t recipeId_,
                       const std::vector<Inventory::Entry>& entries_,
                       uint32_t workbenchId_,
                       std::optional<std::vector<uint32_t>> effects_,
                       std::vector<StockDraw> stockDraws_)
  : actor(actor_)
  , craftedItemBaseId(craftedItemBaseId_)
  , count(count_)
  , recipeId(recipeId_)
  , workbenchId(workbenchId_)
  , effects(std::move(effects_))
  , entries(entries_)
  , stockDraws(std::move(stockDraws_))
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
  return ArgumentsJsonArray(actor ? actor->GetFormId() : 0, craftedItemBaseId,
                            count, recipeId, workbenchId, effects, entries,
                            &stockDraws);
}

std::string CraftEvent::ArgumentsJsonArray(
  uint32_t actorId, uint32_t craftedItemBaseId, uint32_t count,
  uint32_t recipeId, uint32_t workbenchId,
  const std::optional<std::vector<uint32_t>>& effects,
  const std::vector<Inventory::Entry>& entries,
  const std::vector<StockDraw>* stockDraws)
{
  std::string result;
  result += "[";
  result += std::to_string(actorId);
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
  /*
    THORNSWOOD PATCH (Thornswood #3159). What comes out of the bench's stock,
    as an eighth argument: [{"baseId":n,"count":n,"from":container}, ...],
    each one part of an input in the seventh, which the pack does not give.
    Empty when the pack gives it all. The gamemode judges a craft that draws
    on a chest by it (a chest it named when the bench was pressed, which may
    have changed hands since), and a refusal takes nothing from either.
  */
  if (stockDraws) {
    result += ",[";
    first = true;
    for (auto& draw : *stockDraws) {
      if (!first) {
        result += ",";
      }
      first = false;
      result += "{\"baseId\":";
      result += std::to_string(draw.entry.baseId);
      result += ",\"count\":";
      result += std::to_string(draw.entry.count);
      result += ",\"from\":";
      result += std::to_string(draw.containerId);
      result += "}";
    }
    result += "]";
  }
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

  /*
    THORNSWOOD PATCH (Thornswood #3159). The pack gives every input but what
    the bench's stock gives, and each container gives its own part. All of it
    is checked before anything is taken, so a container that no longer holds
    its part, or no longer exists, refuses the craft with nothing moved.
  */
  std::vector<Inventory::Entry> fromPack = entries;
  for (auto& draw : stockDraws) {
    uint32_t left = draw.entry.count;
    for (auto& entry : fromPack) {
      if (!left) {
        break;
      }
      if (entry.baseId == draw.entry.baseId) {
        const uint32_t part = std::min(entry.count, left);
        entry.count -= part;
        left -= part;
      }
    }
    if (left) {
      throw std::runtime_error(fmt::format(
        "CraftEvent - actor {:#x}'s craft draws {:#x} x{} from the bench's "
        "stock, more than its inputs name",
        actor->GetFormId(), draw.entry.baseId, draw.entry.count));
    }
  }
  fromPack.erase(std::remove_if(fromPack.begin(), fromPack.end(),
                                [](const Inventory::Entry& entry) {
                                  return entry.count == 0;
                                }),
                 fromPack.end());

  std::map<uint32_t, std::vector<Inventory::Entry>> fromContainer;
  for (auto& draw : stockDraws) {
    fromContainer[draw.containerId].push_back(draw.entry);
  }
  std::vector<std::pair<MpObjectReference*, std::vector<Inventory::Entry>>>
    containers;
  for (auto& [containerId, containerEntries] : fromContainer) {
    auto worldState = actor->GetParent();
    std::shared_ptr<MpForm> form;
    if (worldState) {
      form = worldState->LookupFormById(containerId);
    }
    auto refr = form ? form->AsObjectReference() : nullptr;
    if (!refr || refr->IsDeleted() || refr->IsDisabled()) {
      throw std::runtime_error(fmt::format(
        "CraftEvent - actor {:#x}'s craft draws from {:#x}, which is gone",
        actor->GetFormId(), containerId));
    }
    // Throws when it no longer holds them, and changes only the copy.
    Inventory copy = refr->GetInventory();
    copy.RemoveItems(containerEntries);
    containers.push_back({ refr, containerEntries });
  }

  // Refused before the inputs are taken, so a pack that can't hold the
  // output keeps them.
  Inventory afterInputs = actor->GetInventory();
  afterInputs.RemoveItems(fromPack);
  if (!afterInputs.CanAddItems(
        { Inventory::Entry(outputBaseId, outputCount, extraData) })) {
    throw std::runtime_error(fmt::format(
      "CraftEvent - actor {:#x} can't take {:#x} x{}: a stack would pass its "
      "limit",
      actor->GetFormId(), outputBaseId, outputCount));
  }

  actor->RemoveItems(fromPack);
  for (auto& [refr, containerEntries] : containers) {
    refr->RemoveItems(containerEntries);
    for (auto& entry : containerEntries) {
      spdlog::info("CraftEvent - actor {:#x}'s craft took {:#x} x{} from "
                   "{:#x}, a container of the bench's stock",
                   actor->GetFormId(), entry.baseId, entry.count,
                   refr->GetFormId());
    }
  }
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
