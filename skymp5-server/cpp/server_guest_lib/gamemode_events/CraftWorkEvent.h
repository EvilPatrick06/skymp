#pragma once
#include "GameModeEvent.h"

#include "Inventory.h"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

class MpActor;

// THORNSWOOD PATCH (Thornswood #3161). How long a craft takes at the bench.
//
// Patrick, 22 Oct 2026: crafting takes small 1 s, medium 5 s, large 15 s,
// with an animation, and moving away stops it. The game makes the item on the
// person's machine the moment they press craft, so the wait lives here: the
// server asks the gamemode this before anything else about a craft, with
// onCraft's own seven arguments, and holds the craft for the time it names
// (CraftService::OnCraftItem). The bench's own working animation plays while
// the person stays in its menu, and onCraft is asked, the inputs taken and
// the output given only when the time is up.
//
// A listener names the time by setting craftWork (CraftWorkBinding.h) for the
// actor while this event is on the stack. The default is no time, which is a
// craft made at once, exactly as before this event existed, so a gamemode
// with no onCraftWork handler changes nothing. A listener that answers false
// refuses the craft before any time passes, with nothing taken.
class CraftWorkEvent : public GameModeEvent
{
public:
  // The most a craft may be held. A bench is not a place to leave somebody
  // for an hour, and a number past this is a gamemode's mistake, refused
  // where it is set (SetWorkTime) rather than waited out.
  static constexpr std::chrono::milliseconds kMaxWorkTime{ 10 * 60 * 1000 };

  CraftWorkEvent(MpActor* actor_, uint32_t craftedItemBaseId_, uint32_t count_,
                 uint32_t recipeId_,
                 const std::vector<Inventory::Entry>& entries_,
                 uint32_t workbenchId_,
                 const std::optional<std::vector<uint32_t>>& effects_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  uint32_t GetActorId() const;
  std::chrono::milliseconds GetWorkTime() const;
  void SetWorkTime(std::chrono::milliseconds time);

private:
  void OnFireSuccess(WorldState* worldState) override;

  MpActor* actor = nullptr;
  uint32_t craftedItemBaseId = 0;
  uint32_t count = 0;
  uint32_t recipeId = 0;
  uint32_t workbenchId = 0;
  const std::vector<Inventory::Entry>& entries;
  const std::optional<std::vector<uint32_t>>& effects;

  std::chrono::milliseconds workTime{ 0 };
};
