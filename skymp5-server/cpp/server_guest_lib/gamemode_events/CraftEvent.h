#pragma once
#include "GameModeEvent.h"

#include "Inventory.h"

class MpActor;

class CraftEvent : public GameModeEvent
{
public:
  CraftEvent(MpActor* actor_, uint32_t craftedItemBaseId_, uint32_t count_,
             uint32_t recipeId_,
             const std::vector<Inventory::Entry>& entries_,
             uint32_t workbenchId_ = 0);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

private:
  void OnFireSuccess(WorldState* worldState) override;

  // event arguments
  MpActor* actor = 0;
  uint32_t craftedItemBaseId = 0;
  uint32_t count = 0;
  uint32_t recipeId = 0;
  // THORNSWOOD PATCH. The bench it happened at, as a fifth argument.
  //
  // Alchemy and enchanting have no COBJ record at all: a potion is worked out
  // from the effects the ingredients share, inside a menu, with nothing in the
  // files to match against. So FindRecipe comes back empty and the craft is
  // dropped on the floor with a log line, which is why an Alchemist owns no
  // recipe in this build and can make nothing.
  //
  // With the bench named, the gamemode can tell a brew at an alchemy lab from
  // somebody claiming they made a daedric sword out of a flower. That check
  // HAS to exist somewhere, because without a COBJ the server is taking the
  // client's word for what went in and what came out, and it belongs on the
  // server side where it can be changed without a client build.
  uint32_t workbenchId = 0;

  // OnFireSuccess-specific arguments
  const std::vector<Inventory::Entry>& entries;
};
