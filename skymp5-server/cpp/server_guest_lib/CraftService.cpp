#include "CraftService.h"

#include "ConditionsEvaluator.h"
#include "MpActor.h"
#include "PartOne.h"
#include "RawMessageData.h"
#include "WorldState.h"
#include "gamemode_events/CraftEvent.h"
#include <algorithm>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <vector>

CraftService::CraftService(PartOne& partOne_)
  : partOne(partOne_)
{
}

void CraftService::OnCraftItem(const RawMessageData& rawMsgData,
                               const Inventory& inputObjects,
                               uint32_t workbenchId, uint32_t resultObjectId)
{
  spdlog::info("User {} tries to craft {:#x} on workbench {:#x}",
               rawMsgData.userId, resultObjectId, workbenchId);

  MpActor* me = partOne.serverState.ActorByUser(rawMsgData.userId);
  if (!me) {
    return spdlog::error("Unable to craft without Actor attached");
  }

  /*
    THORNSWOOD PATCH. A craft that does not go through sends the client its
    inventory back.

    The game makes the thing before the server hears about it. By the time
    this runs, the client has already taken the materials out of the pack,
    put the result in, and is showing that. So when the server does not carry
    the craft out, for any reason, the person is left looking at an item the
    server does not have, and missing materials the server still has. Nothing
    told the client so. It found out only when it next re-applied the last
    inventory the server had sent, on a five second timer, and anything built
    on the phantom item in the meantime was refused as well.

    Reported by a tester on the dev server on 23 September: "Made a helmet
    twice only got one". The log showed the first helmet refused on the
    inventory check ("Source inventory doesn't have enough 0x1be1a"), and four
    seconds later that same helmet, which the server never had, sent in as the
    input to another craft and refused the same way. The helmet was made
    again, went through, and one helmet is what there was.

    So every way out of a craft that is not a success ends here: a bench that
    is not a bench, the gamemode saying no, and the removal that throws
    because the server does not hold what the client says went in (that
    removal changes nothing unless all of it succeeds, see
    Inventory::RemoveItems). What is sent is the server's own inventory, in
    the same message every other change to it sends. Nothing is added or
    taken away here; the client is only told what is true, and it drops the
    phantom the next frame it is out of the inventory and crafting menus
    (onSetInventoryMessage in remoteServer.ts) instead of on the timer.

    This does not make the refused craft succeed. Why it was refused is a
    separate question. An exception still goes on up once the inventory is
    sent, so it is logged where and how it always was.
  */
  bool crafted = false;
  try {
    crafted = CraftItem(me, inputObjects, workbenchId, resultObjectId);
  } catch (const std::exception& e) {
    SendInventoryBack(me, workbenchId, resultObjectId, e.what());
    throw;
  }

  if (!crafted) {
    SendInventoryBack(me, workbenchId, resultObjectId, "it was refused");
  }
}

void CraftService::SendInventoryBack(MpActor* me, uint32_t workbenchId,
                                     uint32_t resultObjectId,
                                     const std::string& why)
{
  spdlog::warn("CraftService::OnCraftItem - the craft of {:#x} on workbench "
               "{:#x} did not go through ({}), so actor {:#x} is sent the "
               "inventory the server holds",
               resultObjectId, workbenchId, why, me->GetFormId());

  // A failure to send must not hide the reason the craft failed, which the
  // caller throws on as soon as this returns.
  try {
    me->SendInventoryUpdate();
  } catch (const std::exception& e) {
    spdlog::error("CraftService::OnCraftItem - could not send actor {:#x} its "
                  "inventory: {}",
                  me->GetFormId(), e.what());
  }
}

bool CraftService::CraftItem(MpActor* me, const Inventory& inputObjects,
                             uint32_t workbenchId, uint32_t resultObjectId)
{
  auto& workbench =
    partOne.worldState.GetFormAt<MpObjectReference>(workbenchId);

  auto& br = partOne.worldState.GetEspm().GetBrowser();
  auto& cache = partOne.worldState.GetEspmCache();
  auto workbenchBase = br.LookupById(workbench.GetBaseId());

  // THORNSWOOD PATCH: checked before it is used rather than after. The type
  // check below used to dereference it first, which is a crash, not a
  // refusal.
  if (!workbenchBase.rec) {
    spdlog::error("Workbench ref without base object {:x}",
                  workbench.GetFormId());
    return false;
  }

  bool isFurnitureOrActivator = workbenchBase.rec->GetType() == "FURN" ||
    workbenchBase.rec->GetType() == "ACTI";
  if (!isFurnitureOrActivator) {
    spdlog::error("Unable to use {} as workbench",
                  workbenchBase.rec->GetType().ToString());
    return false;
  }

  std::vector<uint32_t> workbenchKeywordIds =
    workbenchBase.rec->GetKeywordIds(cache);

  auto recipesList =
    FindRecipe(me, workbenchKeywordIds, br, inputObjects, resultObjectId);

  if (recipesList.empty()) {
    /*
      THORNSWOOD PATCH. A craft with no recipe is asked about rather than
      dropped.

      ALCHEMY AND ENCHANTING HAVE NO COBJ RECORD AT ALL. A potion is worked
      out from the effects the ingredients share, inside a menu, with nothing
      in the files to match against, and enchanting is the same shape. So
      FindRecipe comes back empty for every brew and every enchantment, and
      what happened next was this log line and nothing else: the inputs were
      never removed and the output was never added, so the potion the person
      watched themselves make was undone by the next inventory the server
      sent. Measured by reading this file on 21 September, after
      Check-Balance.ps1 reported "the Alchemist owns no recipe in this build
      at all, so there is nothing for anybody to supply it". Two of the eight
      professions could be picked and could make nothing.

      THE GAMEMODE DECIDES, NOT THIS. Without a COBJ there is no record to
      check the claim against, so the server is taking the client's word for
      what went in and what came out. That is exactly why this asks instead
      of acting: recipeId is 0 to say there was no recipe, the bench is
      named, and mp.onCraft returning false refuses it and costs the person
      nothing. A gamemode that does not answer, or answers false, leaves
      behaviour exactly as it was before this patch.

      OnFireSuccess needs no recipe either: it removes the entries and adds
      the output from the event's own data, which is all a brew has.
    */
    spdlog::info(
      "Recipe not found, asking the gamemode: inputObjects={}, "
      "workbenchId={:#x}, resultObjectId={:#x}",
      inputObjects.ToJson().dump(), workbenchId, resultObjectId);

    CraftEvent craftEvent(me, resultObjectId, 1, 0, inputObjects.entries,
                          workbenchId);
    if (!craftEvent.Fire(me->GetParent())) {
      spdlog::info("The gamemode refused the craft of {:#x} with no recipe",
                   resultObjectId);
      return false;
    }
    return true;
  }

  if (recipesList.size() > 1) {
    spdlog::warn("Found more than 1 recipe ({}), using the 1st one",
                 recipesList.size());
  }

  if (!UseCraftRecipe(me,
                      reinterpret_cast<const espm::COBJ*>(recipesList[0].rec),
                      cache, br, recipesList[0].fileIdx, workbenchId)) {
    // THORNSWOOD PATCH: UseCraftRecipe has already logged "crafted" by the
    // time the gamemode answers, so the log says when the answer was no.
    spdlog::info("The gamemode refused the craft of {:#x}", resultObjectId);
    return false;
  }
  return true;
}

bool CraftService::RecipeItemsMatch(const espm::LookupResult& lookupRes,
                                    const Inventory& inputObjects,
                                    uint32_t resultObjectId)
{
  auto recipe = reinterpret_cast<const espm::COBJ*>(lookupRes.rec);

  espm::CompressedFieldsCache dummyCache;
  auto recipeData = recipe->GetData(dummyCache);

  enum
  {
    ArmorTable = 0xadb78,
    SharpeningWheel = 0x88108
  };
  const bool isTemper = recipeData.benchKeywordId == ArmorTable ||
    recipeData.benchKeywordId == SharpeningWheel;
  if (isTemper) {
    return false;
  }

  auto thisInputObjects = recipeData.inputObjects;
  for (auto& entry : thisInputObjects) {
    auto formId = lookupRes.ToGlobalId(entry.formId);
    if (inputObjects.GetItemCount(formId) != entry.count) {
      return false;
    }
  }
  auto formId = lookupRes.ToGlobalId(recipeData.outputObjectFormId);
  if (formId != resultObjectId) {
    return false;
  }
  return true;
}

std::vector<espm::LookupResult> CraftService::FindRecipe(
  std::optional<MpActor*> me,
  std::optional<std::vector<uint32_t>> workbenchKeywordIds,
  const espm::CombineBrowser& br, const Inventory& inputObjects,
  uint32_t resultObjectId)
{
  if (allRecipes.empty()) {
    allRecipes = br.GetDistinctRecordsByType("COBJ");
  }

  std::vector<espm::LookupResult> candidatesConsideredUsable;

  for (auto& recipe : allRecipes) {
    if (!RecipeItemsMatch(recipe, inputObjects, resultObjectId)) {
      continue;
    }

    spdlog::info("CraftService::FindRecipe - Recipe candidate found: {:x}",
                 recipe.ToGlobalId(recipe.rec->GetId()));

    const bool canBeUsed =
      ConsiderRecipeCandidate(me, workbenchKeywordIds, recipe);
    if (canBeUsed) {
      candidatesConsideredUsable.push_back(recipe);
      spdlog::info("CraftService::FindRecipe - Recipe candidate usable");
    } else {
      spdlog::info("CraftService::FindRecipe - Recipe candidate not usable");
    }
  }

  return candidatesConsideredUsable;
}

bool CraftService::ConsiderRecipeCandidate(
  std::optional<MpActor*> me,
  std::optional<std::vector<uint32_t>> workbenchKeywordIds,
  const espm::LookupResult& lookupRes)
{
  auto cobj = reinterpret_cast<const espm::COBJ*>(lookupRes.rec);
  auto cobjData = cobj->GetData(cache);

  bool finalConsiderationResult = true;

  if (me.has_value()) {
    bool evalRes = EvaluateCraftRecipeConditions(*me, cobjData);
    if (!evalRes) {
      spdlog::info("CraftService::ConsiderRecipeCandidate - Craft recipe "
                   "conditions are not met");
      finalConsiderationResult = false;
    }
  } else {
    spdlog::info("CraftService::ConsiderRecipeCandidate - Actor not "
                 "specified, skipping conditions check");
  }

  if (workbenchKeywordIds.has_value()) {
    auto recipeBenchKeywordId = lookupRes.ToGlobalId(cobjData.benchKeywordId);

    // Note: In the original game, setting the benchmark keyword to NONE
    // removes the recipe from all crafting stations.

    bool includes =
      std::any_of(workbenchKeywordIds->begin(), workbenchKeywordIds->end(),
                  [&](uint32_t id) { return id == recipeBenchKeywordId; });

    if (!includes) {
      std::vector<std::string> hexIds;
      hexIds.reserve(workbenchKeywordIds->size());
      for (auto id : *workbenchKeywordIds) {
        hexIds.push_back(fmt::format("{:x}", id));
      }

      spdlog::info("CraftService::ConsiderRecipeCandidate - Craft recipe "
                   "workbench keywords don't match: recipe one {:x} is not in "
                   "workbench ids {}",
                   recipeBenchKeywordId, fmt::join(hexIds, ", "));
      finalConsiderationResult = false;
    }

  } else {
    spdlog::info("CraftService::ConsiderRecipeCandidate - Workbench keyword "
                 "id not specified, skipping bench keyword id check");
  }

  return finalConsiderationResult;
}

bool CraftService::UseCraftRecipe(MpActor* me, const espm::COBJ* recipeUsed,
                                  espm::CompressedFieldsCache& cache,
                                  const espm::CombineBrowser& br, int espmIdx,
                                  uint32_t workbenchId)
{
  auto recipeData = recipeUsed->GetData(cache);
  auto mapping = br.GetCombMapping(espmIdx);

  spdlog::info("Using craft recipe with EDID {} from espm file with index {}",
               recipeUsed->GetEditorId(cache), espmIdx);

  std::vector<Inventory::Entry> entries;
  for (auto& entry : recipeData.inputObjects) {
    auto formId = espm::utils::GetMappedId(entry.formId, *mapping);
    entries.push_back({ formId, entry.count });
  }

  auto outputFormId =
    espm::utils::GetMappedId(recipeData.outputObjectFormId, *mapping);

  if (spdlog::should_log(spdlog::level::info)) {
    std::string s = fmt::format("User formId={:#x} crafted", me->GetFormId());
    for (const auto& entry : entries) {
      s += fmt::format(" -{:#x} x{}", entry.baseId, entry.count);
    }
    s += fmt::format(" +{:#x} x{}", outputFormId, recipeData.outputCount);
    spdlog::info("{}", s);
  }

  auto recipeId = espm::utils::GetMappedId(recipeUsed->GetId(), *mapping);

  CraftEvent craftEvent(me, outputFormId, recipeData.outputCount, recipeId,
                        entries, workbenchId);

  return craftEvent.Fire(me->GetParent());
}

bool CraftService::EvaluateCraftRecipeConditions(
  MpActor* me, const espm::COBJ::Data& recipeData)
{
  std::vector<Condition> conditions;
  std::transform(recipeData.conditions.begin(), recipeData.conditions.end(),
                 std::back_inserter(conditions),
                 [&](const auto& ctda) { return Condition::FromCtda(ctda); });

  // TODO: aggressor and target terms are not relevant for crafting
  const MpActor& aggressor = *me;
  const MpActor& target = *me;

  bool evalRes_ = false;

  auto callback = [&](bool evalRes, std::vector<std::string>& strings) {
    evalRes_ = evalRes;

    if (!strings.empty()) {
      if (evalRes) {
        strings.insert(strings.begin(),
                       fmt::format("EvaluateConditions result is true"));
      } else {
        strings.insert(strings.begin(),
                       fmt::format("EvaluateConditions result is false"));
      }
    }
  };

  static const ConditionsEvaluatorSettings kDefaultSettings;

  static const ConditionFunctionMap kEmptyMap;

  auto worldState = me->GetParent();

  const ConditionsEvaluatorSettings& settings =
    worldState ? worldState->conditionsEvaluatorSettings : kDefaultSettings;

  const ConditionFunctionMap& conditionFunctionMap =
    worldState ? worldState->conditionFunctionMap : kEmptyMap;

  ConditionsEvaluator::EvaluateConditions(
    conditionFunctionMap, settings, ConditionsEvaluatorCaller::kCraft,
    conditions, aggressor, target, callback);

  return evalRes_;
}
