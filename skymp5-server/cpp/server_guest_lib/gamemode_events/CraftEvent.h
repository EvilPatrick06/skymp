#pragma once
#include "GameModeEvent.h"

#include "Inventory.h"
#include <optional>
#include <string>
#include <vector>

class MpActor;

class CraftEvent : public GameModeEvent
{
public:
  CraftEvent(MpActor* actor_, uint32_t craftedItemBaseId_, uint32_t count_,
             uint32_t recipeId_,
             const std::vector<Inventory::Entry>& entries_,
             uint32_t workbenchId_ = 0,
             std::optional<std::vector<uint32_t>> effects_ = std::nullopt);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  // THORNSWOOD PATCH (Thornswood #2906). An onCraft listener may name what
  // this craft gives in place of the recipe's output: a cook's burnt meal or
  // fine dish, or a master's double batch. The default is the second and
  // third arguments, craftedItemBaseId and count. OnFireSuccess adds what is
  // named, in the same step that takes the inputs, so the server never gives
  // the recipe's output and takes it back afterwards. A refused craft gives
  // nothing whatever was named.
  //
  // Thornswood #3026. The output may be named too, the name an inventory
  // entry carries (Inventory::ExtraData::name): a master weaver's garment is
  // the record with the mark of the half it carries in its name. Unnamed
  // unless a listener names it, and OnFireSuccess adds the named entry, so a
  // gamemode never renames what the craft added.
  uint32_t GetActorId() const;
  uint32_t GetCraftedItemBaseId() const;
  uint32_t GetOutputBaseId() const;
  uint32_t GetOutputCount() const;
  void SetOutput(uint32_t baseId, uint32_t count);
  const std::optional<std::string>& GetOutputName() const;
  void SetOutputName(std::optional<std::string> name);

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

  // THORNSWOOD PATCH (Thornswood #1389). The sixth argument: the MGEF form
  // ids a craft with no recipe carries, when the server can work them out (a
  // brew, CraftService::BrewEffects), and null when it cannot.
  std::optional<std::vector<uint32_t>> effects;

  // OnFireSuccess-specific arguments
  const std::vector<Inventory::Entry>& entries;

  // What OnFireSuccess adds (Thornswood #2906): the recipe's output unless a
  // listener named another.
  uint32_t outputBaseId = 0;
  uint32_t outputCount = 0;
  // And the name it is added with (Thornswood #3026): none unless named.
  std::optional<std::string> outputName;
};
