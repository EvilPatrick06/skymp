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
