#include "EatItemEvent.h"

#include "MpActor.h"
#include "WorldState.h"
#include <cmath>
#include <unordered_set>
#include <vector>

EatItemEvent::EatItemEvent(MpActor* actor_, uint32_t baseId_,
                           bool isIngredient_, bool isAlchemyItem_)
  : actor(actor_)
  , baseId(baseId_)
  , isIngredient(isIngredient_)
  , isAlchemyItem(isAlchemyItem_)
{
}

EatItemEvent::EatItemEvent(MpActor* actor_, uint32_t baseId_,
                           std::vector<espm::Effects::Effect> madeEffects_)
  : actor(actor_)
  , baseId(baseId_)
  , isAlchemyItem(true)
  , madeEffects(std::move(madeEffects_))
{
}

const char* EatItemEvent::GetName() const
{
  return "onEatItem";
}

std::string EatItemEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actor->GetFormId());
  result += ",";
  result += std::to_string(baseId);
  result += "]";
  return result;
}

uint32_t EatItemEvent::GetActorId() const
{
  return actor ? actor->GetFormId() : 0;
}

float EatItemEvent::GetEffectScale() const
{
  return effectScale;
}

void EatItemEvent::SetEffectScale(float scale)
{
  if (std::isnan(scale) || scale < 0.f) {
    scale = 0.f;
  }
  if (scale > 1.f) {
    scale = 1.f;
  }
  effectScale = scale;
}

void EatItemEvent::OnFireSuccess(WorldState* worldState)
{
  if (effectScale <= 0.f) {
    return;
  }
  std::vector<espm::Effects::Effect> effects;
  if (madeEffects) {
    effects = *madeEffects;
  } else if (isAlchemyItem) {
    effects = espm::GetData<espm::ALCH>(baseId, worldState).effects;
  } else if (isIngredient) {
    // effects = espm::GetData<espm::INGR>(baseId, worldState).effects;
  } else {
    return;
  }
  std::unordered_set<std::string> modFiles = { worldState->espmFiles.begin(),
                                               worldState->espmFiles.end() };
  bool hasSweetpie = modFiles.count("SweetPie.esp");
  if (effectScale < 1.f) {
    for (auto& effect : effects) {
      effect.magnitude *= effectScale;
    }
  }
  actor->ApplyMagicEffects(effects, hasSweetpie);
}
