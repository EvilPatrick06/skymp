#include "CraftService.h"

#include "ConditionsEvaluator.h"
#include "MpActor.h"
#include "PartOne.h"
#include "RawMessageData.h"
#include "WorldState.h"
#include "gamemode_events/CraftEvent.h"
#include "libespm/Convert.h"
#include "libespm/INGR.h"
#include <algorithm>
#include <map>
#include <set>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace {
/*
  THORNSWOOD PATCH (Thornswood #1618). The inputs are checked against the
  actor's own inventory before the gamemode is asked.

  The recipe is matched against what the client says went in, and
  CraftEvent::OnFireSuccess removes the inputs only after mp.onCraft has run.
  When the server did not hold them, that removal threw ("Source inventory
  doesn't have enough") after the gamemode had already acted on the craft:
  taken the charcoal and the tool handle, started the key timer, credited the
  work. So a craft whose inputs the server does not hold is refused here,
  before CraftEvent exists, and the gamemode never hears of it. Counts are
  summed per base id, so a recipe that lists one item twice needs both.

  THORNSWOOD PATCH (Thornswood #3163). And which stacks they are taken
  from (Inventory::ResolveRemoval): the plain stack first, then a stack of
  the same item that differs only in its name. Food carries its days left
  on its name, so raw meat that has sat a day is "Venison (6 days left)",
  and the client names an input by its base id alone. Until this the
  removal matched the plain entry exactly, so a named stack passed the
  count here (GetItemCount counts every stack) and threw at the removal
  after the gamemode had acted, which is the case #1618 closed. What the
  craft takes is now worked out once, here, before the gamemode is asked;
  the gamemode is told it (the seventh argument names a named input), and
  OnFireSuccess takes exactly that. A stack that differs in anything but
  its name (worn, tempered, enchanted) is still never an input.
*/
std::optional<std::vector<Inventory::Entry>> InputsTaken(
  const MpActor& me, const std::vector<Inventory::Entry>& entries)
{
  try {
    return me.GetInventory().ResolveRemoval(entries);
  } catch (const std::exception& e) {
    spdlog::warn("CraftService - actor {:#x} does not hold what the craft "
                 "needs ({}), so it is refused without asking the gamemode",
                 me.GetFormId(), e.what());
    return std::nullopt;
  }
}
}

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
    server does not have, and missing materials the server still has, until
    the client next puts the server's last inventory back.

    So every way out of a craft that is not a success ends here: a bench that
    is not a bench, the gamemode saying no, and the removal that throws
    because the server does not hold what the client says went in (that
    removal changes nothing unless all of it succeeds, see
    Inventory::RemoveItems). What is sent is the server's own inventory, in
    the same message every other change to it sends. Nothing is added or
    taken away here; the client is only told what is true. An exception still
    goes on up once the inventory is sent, so it is logged where and how it
    always was. This does not make a refused craft succeed.

    HOW LITTLE THIS CHANGES ON THE SCREEN, read off the client code rather
    than assumed. The client puts the server's inventory back in one place,
    the update handler in remoteServer.ts. That handler does nothing while
    the Crafting Menu (or the inventory, container, favourites or magic menu)
    is open, and otherwise runs at most once every five seconds, counted from
    the last time it ran. Since it never runs with a menu open, the five
    seconds are always up once a menu has been open that long. Before this
    patch, then, a refused craft was already undone on the first frame after
    the Crafting Menu closed whenever the menu had been open five seconds or
    more, which is nearly always. This message cannot bring that forward,
    because nothing is applied with the menu open. What it changes is the
    short case: a menu closed less than five seconds after it opened is now
    put right on that first frame too, instead of up to five seconds later.

    It made no difference to the case that prompted it. A tester on the dev
    server on 23 September: "Made a helmet twice only got one" (Thornswood
    To Do 347). At 20:46:05 the first helmet was refused on the inventory
    check ("Source inventory doesn't have enough 0x1be1a"). The forge was
    used at 20:45:14; the correction, which cannot run with the menu open,
    came at 20:46:09; and the forge was used again at 20:46:18, so the person
    had left it in between. The menu had been open for most of a minute, and
    the helmet was taken back on the first frame after it closed, with or
    without this.

    The line at 20:46:09, "craft 0x5ace4" with the helmet as its one input,
    was not the person doing anything with the helmet. It was that
    correction, run while the character was still seated at the forge.
    craftService.ts on the client counts everything that goes out of the
    pack while the character is at a piece of furniture as an input, and the
    next thing that comes in as the result, and applyInventory makes its
    removals first. So taking the phantom helmet out and putting the iron
    ingots back went to the server as a craft of an ingot from the helmet,
    which the server did not hold and refused.

    THE CLIENT HALF LANDED LATER. The first version of this comment said
    that in the short case the correction now runs while the character is
    still seated and so is read as a craft, and that the fix for that is on
    the client. It is: fork commit 9732d2cc (Thornswood #523) counts a
    craft only while the Crafting Menu is open and clears the streak when
    that menu opens and when it closes, and the client never applies the
    server's inventory while that menu is open. So the inventory this sends
    is applied after the menu closes and is not read as a craft, in the
    short case or the long one.
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

    auto taken = InputsTaken(*me, inputObjects.entries);
    if (!taken) {
      return false;
    }

    /*
      THORNSWOOD PATCH (Thornswood #1389). A brew names its effects.

      The gamemode holds the mixtures list (docs/systems/mixtures.md, MIXTURES
      in game/gamemode.js) and refuses a brew that carries an effect off it,
      but it can only ask the list when it is told the effects, and a brewed
      bottle is a runtime form the server has no record of. The effects are
      not the client's to claim: they follow from the ingredients, which the
      check above has just found in the server's own inventory, by the game's
      rule. So they are worked out here and handed over as the sixth
      argument. An enchantment has no ingredient in it and stays "not named"
      until the client can say what went on the piece.
    */
    auto effects = BrewEffects(br, inputObjects);
    if (effects) {
      spdlog::info("A brew of {} carries the effects {}",
                   inputObjects.ToJson().dump(), fmt::join(*effects, ","));
    }

    CraftEvent craftEvent(me, resultObjectId, 1, 0, *taken, workbenchId,
                          effects);
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

std::optional<std::vector<uint32_t>> CraftService::BrewEffects(
  const espm::CombineBrowser& br, const Inventory& inputObjects)
{
  std::vector<uint32_t> firstNamed;
  std::map<uint32_t, int> carriedBy;
  std::set<uint32_t> ingredientsSeen;
  bool anyIngredient = false;

  for (const auto& entry : inputObjects.entries) {
    // Two of one ingredient is still one ingredient's effects.
    if (!ingredientsSeen.insert(entry.baseId).second) {
      continue;
    }
    auto lookup = br.LookupById(entry.baseId);
    const auto* ingredient = espm::Convert<espm::INGR>(lookup.rec);
    if (!ingredient) {
      continue;
    }
    anyIngredient = true;

    std::set<uint32_t> carried;
    for (const auto& effect : ingredient->GetData(cache).effects) {
      const uint32_t mgef = lookup.ToGlobalId(effect.effectId);
      if (!carried.insert(mgef).second) {
        continue;
      }
      if (carriedBy[mgef]++ == 0) {
        firstNamed.push_back(mgef);
      }
    }
  }

  if (!anyIngredient) {
    return std::nullopt;
  }

  std::vector<uint32_t> shared;
  for (uint32_t mgef : firstNamed) {
    if (carriedBy[mgef] >= 2) {
      shared.push_back(mgef);
    }
  }
  return shared;
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

  auto taken = InputsTaken(*me, entries);
  if (!taken) {
    return false;
  }

  CraftEvent craftEvent(me, outputFormId, recipeData.outputCount, recipeId,
                        *taken, workbenchId);

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
