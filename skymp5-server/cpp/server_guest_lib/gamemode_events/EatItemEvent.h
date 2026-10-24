#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "GameModeEvent.h"
#include "libespm/Effects.h"

class MpActor;

class EatItemEvent : public GameModeEvent
{
public:
  EatItemEvent(MpActor* actor_, uint32_t baseId_, bool isIngredient_,
               bool isAlchemyItem_);

  // Thornswood #1649. A brewed bottle has no record to read its effects
  // from; it brings them (Inventory::Made, global MGEF ids).
  EatItemEvent(MpActor* actor_, uint32_t baseId_,
               std::vector<espm::Effects::Effect> madeEffects_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  // Thornswood #1382: an onEatItem listener may scale what this item does,
  // from 0 (nothing) to 1 (the record's own magnitudes, the default). The
  // scale is applied in OnFireSuccess, before any effect lands, so a server
  // can weaken a potion without taking anything back afterwards.
  uint32_t GetActorId() const;
  float GetEffectScale() const;
  void SetEffectScale(float scale);

private:
  void OnFireSuccess(WorldState* worldState) override;

  // event arguments
  MpActor* actor = nullptr;
  uint32_t baseId = 0;

  // OnFireSuccess-specific arguments
  bool isIngredient = false;
  bool isAlchemyItem = false;
  std::optional<std::vector<espm::Effects::Effect>> madeEffects;

  float effectScale = 1.f;
};
