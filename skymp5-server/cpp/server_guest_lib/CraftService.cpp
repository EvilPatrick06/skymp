#include "CraftService.h"

#include "ConditionsEvaluator.h"
#include "MpActor.h"
#include "PartOne.h"
#include "RawMessageData.h"
#include "WorldState.h"
#include "gamemode_events/CraftEvent.h"
#include "gamemode_events/CraftWorkEvent.h"
#include "libespm/Convert.h"
#include "libespm/INGR.h"
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
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
  THORNSWOOD PATCH (Thornswood #3159). And where each input comes from: the
  pack first, then the containers of the stock open at this bench
  (MpActor::BenchStock), in the order the gamemode named them. The answer is
  the part the containers give, empty when the pack gives it all, and
  nullopt when the pack and the stock together fall short, which refuses the
  craft as before. OnFireSuccess takes each part from where this says.

  What counts is what a removal can take: an entry that carries nothing but
  a base and a count, because the inputs the client names carry nothing else
  and Inventory::RemoveItems matches entries exactly. This used to count
  every entry of the base (Inventory::GetItemCount), so a pack holding only a
  named or worn one passed here and threw at the removal, after the gamemode
  had acted, which is the case #1618 closed for a missing input.
*/
uint64_t PlainCount(const Inventory& inventory, uint32_t baseId)
{
  const Inventory::Entry plain(baseId, 1);
  uint64_t sum = 0;
  for (const auto& entry : inventory.entries) {
    if (entry.EqualExceptCount(plain)) {
      sum += entry.count;
    }
  }
  return sum;
}

std::optional<std::vector<CraftEvent::StockDraw>> PlanInputs(
  MpActor& me, const std::vector<Inventory::Entry>& entries,
  uint32_t workbenchId, const std::vector<Inventory::Entry>& heldBack = {})
{
  // THORNSWOOD PATCH (Thornswood #3161). heldBack is what the person's
  // crafts still being worked will take, so a new order counts only what is
  // left over after them.
  std::map<uint32_t, uint64_t> needed;
  for (const auto& entry : entries) {
    needed[entry.baseId] += entry.count;
  }
  for (const auto& entry : heldBack) {
    auto it = needed.find(entry.baseId);
    if (it != needed.end()) {
      it->second += entry.count;
    }
  }
  const auto containers = me.GetOpenBenchStock(workbenchId);
  std::vector<CraftEvent::StockDraw> draws;
  for (const auto& [baseId, count] : needed) {
    const uint64_t held = PlainCount(me.GetInventory(), baseId);
    if (held >= count) {
      continue;
    }
    uint64_t lacking = count - held;
    for (auto container : containers) {
      if (!lacking) {
        break;
      }
      const uint64_t there = PlainCount(container->GetInventory(), baseId);
      const uint64_t part = std::min(there, lacking);
      if (part) {
        draws.push_back({ container->GetFormId(),
                          Inventory::Entry(baseId,
                                           static_cast<uint32_t>(part)) });
        lacking -= part;
      }
    }
    if (lacking) {
      spdlog::warn("CraftService - actor {:#x} holds {} of {:#x} and the "
                   "bench's stock {} more, but the craft needs {}, so it is "
                   "refused without asking the gamemode",
                   me.GetFormId(), held, baseId, count - held - lacking,
                   count);
      return std::nullopt;
    }
  }
  return draws;
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
  /*
    THORNSWOOD PATCH (Thornswood #3161). A CRAFT TAKES TIME AT THE BENCH.

    Patrick, 22 Oct 2026: small 1 s, medium 5 s, large 15 s, with an
    animation, and moving away stops it. onCraftWork is asked first, with
    onCraft's own arguments, and names the time. The craft is then held:
    the bench's own working animation plays while the person stays in its
    menu, and when the time is up onCraft is asked, the inputs are taken and
    the output given, all in CraftItem, exactly as a craft made at once. A
    person's crafts are worked one after another, in the order pressed.

    The client hides the item it made the moment the craft is sent (the
    client's craftService.ts), so nothing appears before it is made, and puts
    it back when this says it was made (SendMade). Leaving the bench closes
    its menu, which the client says (StopWork); the crafts still being worked
    stop there, and since nothing was taken the person's inventory, sent
    back once the menu is closed, still holds every input.

    No time named, which is every craft under a gamemode with no onCraftWork
    handler, is a craft made now, as it always was.
  */
  bool crafted = false;
  Made made;
  try {
    std::vector<Inventory::Entry> takes;
    auto time =
      AskWorkTime(me, inputObjects, workbenchId, resultObjectId, takes);
    if (time && time->count() > 0) {
      auto& orders = work[me->GetFormId()];
      Order order;
      order.id = nextOrderId++;
      order.userId = rawMsgData.userId;
      order.inputObjects = inputObjects;
      order.workbenchId = workbenchId;
      order.resultObjectId = resultObjectId;
      order.time = *time;
      order.takes = std::move(takes);
      orders.push_back(std::move(order));
      spdlog::info("CraftService::OnCraftItem - actor {:#x}'s craft of {:#x} "
                   "takes {} ms, {} craft(s) before it",
                   me->GetFormId(), resultObjectId, time->count(),
                   orders.size() - 1);
      if (orders.size() == 1) {
        StartNextOrder(me->GetFormId());
      }
      return;
    }
    if (time) {
      crafted =
        CraftItem(me, inputObjects, workbenchId, resultObjectId, &made);
    }
  } catch (const std::exception& e) {
    SendInventoryBack(me, workbenchId, resultObjectId, e.what());
    throw;
  }

  if (!crafted) {
    SendInventoryBack(me, workbenchId, resultObjectId, "it was refused");
    return;
  }
  SendMade(rawMsgData.userId, made);
}

std::optional<std::chrono::milliseconds> CraftService::AskWorkTime(
  MpActor* me, const Inventory& inputObjects, uint32_t workbenchId,
  uint32_t resultObjectId, std::vector<Inventory::Entry>& takes)
{
  auto craft = WorkOut(me, inputObjects, workbenchId, resultObjectId);
  if (!craft) {
    return std::nullopt;
  }

  // What the person's earlier crafts, still being worked, will take.
  std::vector<Inventory::Entry> heldBack;
  auto it = work.find(me->GetFormId());
  if (it != work.end()) {
    for (const auto& order : it->second) {
      heldBack.insert(heldBack.end(), order.takes.begin(), order.takes.end());
    }
  }
  if (!PlanInputs(*me, craft->entries, workbenchId, heldBack)) {
    return std::nullopt;
  }

  CraftWorkEvent workEvent(me, craft->outputBaseId, craft->outputCount,
                           craft->recipeId, craft->entries, workbenchId,
                           craft->effects);
  if (!workEvent.Fire(me->GetParent())) {
    spdlog::info("The gamemode refused to start the craft of {:#x}",
                 resultObjectId);
    return std::nullopt;
  }
  takes = craft->entries;
  return workEvent.GetWorkTime();
}

void CraftService::StartNextOrder(uint32_t actorId)
{
  auto it = work.find(actorId);
  if (it == work.end()) {
    return;
  }
  if (it->second.empty()) {
    work.erase(it);
    return;
  }
  const Order& order = it->second.front();
  const uint64_t orderId = order.id;
  partOne.worldState.SetTimer(order.time).Then(
    [this, actorId, orderId](Viet::Void) { FinishOrder(actorId, orderId); });
}

void CraftService::FinishOrder(uint32_t actorId, uint64_t orderId)
{
  auto it = work.find(actorId);
  if (it == work.end() || it->second.empty() ||
      it->second.front().id != orderId) {
    // Stopped (StopWork) before its time was up.
    return;
  }
  Order order = std::move(it->second.front());
  it->second.pop_front();

  MpActor* me = partOne.serverState.ActorByUser(order.userId);
  if (!me || me->GetFormId() != actorId) {
    spdlog::info("CraftService::FinishOrder - actor {:#x} is no longer "
                 "user {}'s, so their crafts stop",
                 actorId, order.userId);
    work.erase(actorId);
    return;
  }

  bool crafted = false;
  Made made;
  try {
    if (!AtBench(*me, order.workbenchId)) {
      spdlog::info("CraftService::FinishOrder - actor {:#x} moved away from "
                   "bench {:#x}, so {} craft(s) stop and nothing is taken",
                   actorId, order.workbenchId, it->second.size() + 1);
      work.erase(actorId);
      SendInventoryBack(me, order.workbenchId, order.resultObjectId,
                        "they moved away from the bench");
      return;
    }
    crafted = CraftItem(me, order.inputObjects, order.workbenchId,
                        order.resultObjectId, &made);
  } catch (const std::exception& e) {
    // A timer has nobody above it to throw to, so this is where the reason
    // is said; the inventory goes back as for any craft that failed.
    spdlog::error("CraftService::FinishOrder - the craft of {:#x} on "
                  "workbench {:#x} threw: {}",
                  order.resultObjectId, order.workbenchId, e.what());
  }

  if (crafted) {
    SendMade(order.userId, made);
  } else {
    SendInventoryBack(me, order.workbenchId, order.resultObjectId,
                      "it was refused when its time was up");
  }

  // The gamemode's onCraft may have done anything, StopWork included, so the
  // queue is looked up again rather than trusted.
  StartNextOrder(actorId);
}

bool CraftService::AtBench(const MpActor& me, uint32_t workbenchId)
{
  auto& form = partOne.worldState.LookupFormById(workbenchId);
  auto bench = dynamic_cast<MpObjectReference*>(form.get());
  if (!bench) {
    return false;
  }
  if (bench->GetCellOrWorld() != me.GetCellOrWorld()) {
    return false;
  }
  return (bench->GetPos() - me.GetPos()).Length() <= kWorkReach;
}

void CraftService::StopWork(Networking::UserId userId)
{
  MpActor* me = partOne.serverState.ActorByUser(userId);
  if (!me) {
    return;
  }
  auto it = work.find(me->GetFormId());
  if (it == work.end()) {
    return;
  }
  spdlog::info("CraftService::StopWork - actor {:#x} left the bench, so {} "
               "craft(s) stop and nothing is taken",
               me->GetFormId(), it->second.size());
  work.erase(it);
}

size_t CraftService::GetWorkCount(uint32_t actorId) const
{
  auto it = work.find(actorId);
  return it == work.end() ? 0 : it->second.size();
}

void CraftService::SendMade(Networking::UserId userId, const Made& made)
{
  nlohmann::json content{ { "customPacketType", "craftMade" },
                          { "baseId", made.baseId },
                          { "count", made.count } };
  partOne.SendCustomPacket(userId, content.dump());
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
                             uint32_t workbenchId, uint32_t resultObjectId,
                             Made* made)
{
  auto craft = WorkOut(me, inputObjects, workbenchId, resultObjectId);
  if (!craft) {
    return false;
  }

  auto stockDraws = PlanInputs(*me, craft->entries, workbenchId);
  if (!stockDraws) {
    return false;
  }

  CraftEvent craftEvent(me, craft->outputBaseId, craft->outputCount,
                        craft->recipeId, craft->entries, workbenchId,
                        craft->effects, std::move(*stockDraws));
  if (!craftEvent.Fire(me->GetParent())) {
    // THORNSWOOD PATCH: WorkOut has already logged "crafted" for a recipe by
    // the time the gamemode answers, so the log says when the answer was no.
    if (craft->recipeId) {
      spdlog::info("The gamemode refused the craft of {:#x}", resultObjectId);
    } else {
      spdlog::info("The gamemode refused the craft of {:#x} with no recipe",
                   resultObjectId);
    }
    return false;
  }
  if (made) {
    made->baseId = craftEvent.GetOutputBaseId();
    made->count = craftEvent.GetOutputCount();
  }
  return true;
}

std::optional<CraftService::Craft> CraftService::WorkOut(
  MpActor* me, const Inventory& inputObjects, uint32_t workbenchId,
  uint32_t resultObjectId)
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
    return std::nullopt;
  }

  bool isFurnitureOrActivator = workbenchBase.rec->GetType() == "FURN" ||
    workbenchBase.rec->GetType() == "ACTI";
  if (!isFurnitureOrActivator) {
    spdlog::error("Unable to use {} as workbench",
                  workbenchBase.rec->GetType().ToString());
    return std::nullopt;
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
      until the client can say what went on the piece. The ingredients are
      found in the server's inventory by CraftItem and AskWorkTime, before
      the gamemode hears of the brew.
    */
    auto effects = BrewEffects(br, inputObjects);
    if (effects) {
      spdlog::info("A brew of {} carries the effects {}",
                   inputObjects.ToJson().dump(), fmt::join(*effects, ","));
    }

    Craft craft;
    craft.entries = inputObjects.entries;
    craft.outputBaseId = resultObjectId;
    craft.outputCount = 1;
    craft.recipeId = 0;
    craft.effects = std::move(effects);
    return craft;
  }

  if (recipesList.size() > 1) {
    spdlog::warn("Found more than 1 recipe ({}), using the 1st one",
                 recipesList.size());
  }

  return CraftFromRecipe(
    me, reinterpret_cast<const espm::COBJ*>(recipesList[0].rec), cache, br,
    recipesList[0].fileIdx);
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

CraftService::Craft CraftService::CraftFromRecipe(
  MpActor* me, const espm::COBJ* recipeUsed,
  espm::CompressedFieldsCache& cache, const espm::CombineBrowser& br,
  int espmIdx)
{
  auto recipeData = recipeUsed->GetData(cache);
  auto mapping = br.GetCombMapping(espmIdx);

  spdlog::info("Using craft recipe with EDID {} from espm file with index {}",
               recipeUsed->GetEditorId(cache), espmIdx);

  Craft craft;
  for (auto& entry : recipeData.inputObjects) {
    auto formId = espm::utils::GetMappedId(entry.formId, *mapping);
    craft.entries.push_back({ formId, entry.count });
  }

  craft.outputBaseId =
    espm::utils::GetMappedId(recipeData.outputObjectFormId, *mapping);
  craft.outputCount = recipeData.outputCount;

  if (spdlog::should_log(spdlog::level::info)) {
    std::string s = fmt::format("User formId={:#x} crafted", me->GetFormId());
    for (const auto& entry : craft.entries) {
      s += fmt::format(" -{:#x} x{}", entry.baseId, entry.count);
    }
    s += fmt::format(" +{:#x} x{}", craft.outputBaseId, craft.outputCount);
    spdlog::info("{}", s);
  }

  craft.recipeId = espm::utils::GetMappedId(recipeUsed->GetId(), *mapping);
  return craft;
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
