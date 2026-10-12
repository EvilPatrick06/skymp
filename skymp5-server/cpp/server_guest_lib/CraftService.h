#pragma once
#include "libespm/Loader.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class PartOne;
class Inventory;
struct RawMessageData;
class MpActor;

class CraftService
{
public:
  explicit CraftService(PartOne& partOne_);

  void OnCraftItem(const RawMessageData& rawMsgData,
                   const Inventory& inputObjects, uint32_t workbenchId,
                   uint32_t resultObjectId);

  // public for CraftTest.cpp
  bool RecipeItemsMatch(const espm::LookupResult& lookupRes,
                        const Inventory& inputObjects,
                        uint32_t resultObjectId);

  // public for CraftTest.cpp
  std::vector<espm::LookupResult> FindRecipe(
    std::optional<MpActor*> me,
    std::optional<std::vector<uint32_t>> workbenchKeywordIds,
    const espm::CombineBrowser& br, const Inventory& inputObjects,
    uint32_t resultObjectId);

  // THORNSWOOD PATCH (Thornswood #1389). The magic effects a brew of these
  // inputs holds, worked out from the ingredient records the way the game
  // works them out in the alchemy menu: every MGEF that at least two of the
  // distinct ingredients carry, as global form ids, in the order the inputs
  // first name them. nullopt when no input is an ingredient (an enchantment,
  // or anything else with no COBJ), which onCraft passes on as "not named".
  // public for CraftTest.cpp
  std::optional<std::vector<uint32_t>> BrewEffects(
    const espm::CombineBrowser& br, const Inventory& inputObjects);

private:
  // THORNSWOOD PATCH. The craft itself, split out of OnCraftItem so that
  // every way it can fail ends in the same place. True when it went through,
  // false when it did not; it can also throw. See OnCraftItem.
  bool CraftItem(MpActor* me, const Inventory& inputObjects,
                 uint32_t workbenchId, uint32_t resultObjectId);

  // THORNSWOOD PATCH. Sends the actor's own client the inventory the server
  // holds, after a craft that did not go through. See OnCraftItem.
  void SendInventoryBack(MpActor* me, uint32_t workbenchId,
                         uint32_t resultObjectId, const std::string& why);

  bool ConsiderRecipeCandidate(
    std::optional<MpActor*> me,
    std::optional<std::vector<uint32_t>> workbenchKeywordIds,
    const espm::LookupResult& lookupRes);

  // workbenchId is carried through to the gamemode as onCraft's fifth
  // argument. See CraftEvent.h.
  // THORNSWOOD PATCH: returns false when the gamemode refused the craft.
  bool UseCraftRecipe(MpActor* me, const espm::COBJ* recipeUsed,
                      espm::CompressedFieldsCache& cache,
                      const espm::CombineBrowser& br, int espmIdx,
                      uint32_t workbenchId = 0);

  bool EvaluateCraftRecipeConditions(MpActor* me,
                                     const espm::COBJ::Data& recipeData);

  PartOne& partOne;
  std::vector<espm::LookupResult> allRecipes;
  espm::CompressedFieldsCache cache;
};
