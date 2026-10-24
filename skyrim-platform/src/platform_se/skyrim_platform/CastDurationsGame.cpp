#include "CastDurationsGame.h"
#include "CastDurations.h"

/*
  THORNSWOOD PATCH (Thornswood #2848). See CastDurations.h in platform_lib.

  An active effect's duration is the time the game gave it as it started:
  the spell's own, after the caster's perks and dual casting. It counts
  elapsedSeconds up to it, so a longer duration is a longer hold and nothing
  else changes. An effect that has not run yet has elapsedSeconds 0.
*/
namespace {
RE::ActiveEffect* FindEffect(RE::Actor* target, uint16_t uniqueId)
{
  auto* magicTarget = target ? target->AsMagicTarget() : nullptr;
  auto* list = magicTarget ? magicTarget->GetActiveEffectList() : nullptr;
  if (!list) {
    return nullptr;
  }
  for (RE::ActiveEffect* eff : *list) {
    if (eff && eff->usUniqueID == uniqueId) {
      return eff;
    }
  }
  return nullptr;
}

uint32_t CasterId(RE::ActiveEffect* effect)
{
  auto caster = effect->GetCasterActor();
  return caster ? caster->GetFormID() : 0;
}
}

bool CastDurationsGame::Holds(const RE::MagicItem* spell)
{
  if (!spell ||
      spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
      spell->GetCastingType() !=
        RE::MagicSystem::CastingType::kFireAndForget) {
    return false;
  }
  bool lasts = false;
  for (auto* effect : spell->effects) {
    if (!effect) {
      continue;
    }
    auto* base = effect->baseEffect;
    if (base && base->IsDetrimental() &&
        base->data.primaryAV == RE::ActorValue::kHealth) {
      return false;
    }
    if (effect->effectItem.duration > 0) {
      lasts = true;
    }
  }
  return lasts;
}

float CastDurationsGame::OnOwnCast(RE::Actor* caster, RE::MagicItem* spell)
{
  if (!caster || !spell) {
    return 1.f;
  }
  auto& durations = CastDurations::GetSingleton();
  if (!durations.HasRule(caster->GetFormID())) {
    return 1.f;
  }
  std::vector<CastDurations::Started> started;
  const float mult = durations.OnOwnCast(caster->GetFormID(), spell->GetFormID(),
                                         Holds(spell), &started);

  // The ones the game started just before it raised this cast. One that has
  // already run belongs to an earlier cast and keeps what it was given.
  size_t given = 0;
  for (auto& s : started) {
    auto* target = RE::TESForm::LookupByID<RE::Actor>(s.targetFormId);
    auto* effect = FindEffect(target, s.uniqueId);
    if (!effect || effect->spell != spell ||
        CasterId(effect) != caster->GetFormID() ||
        effect->elapsedSeconds != 0.f) {
      continue;
    }
    effect->duration = s.gameDuration * mult;
    ++given;
  }

  if (mult != 1.f || !started.empty()) {
    logger::info("CastDurations: cast of {:x} by {:x} holds {} times as "
                 "long; {} of its effects had started before the cast "
                 "event, {} of them not yet run",
                 spell->GetFormID(), caster->GetFormID(), mult,
                 started.size(), given);
  }
  return mult;
}

void CastDurationsGame::OnEffectStart(RE::Actor* target,
                                      RE::ActiveEffect* effect)
{
  // Only a spell's effects come from a cast: not a potion's, an
  // enchantment's or a scroll's.
  if (!target || !effect || !effect->spell ||
      effect->spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
      !(effect->duration > 0.f)) {
    return;
  }
  const uint32_t casterId = CasterId(effect);
  if (!casterId) {
    return;
  }
  const float mult = CastDurations::GetSingleton().OnEffectStart(
    casterId, effect->spell->GetFormID(), target->GetFormID(),
    effect->usUniqueID, effect->duration);
  if (mult != 1.f) {
    logger::info("CastDurations: an effect of {:x} by {:x} on {:x} holds {} "
                 "seconds, {} times the game's {}",
                 effect->spell->GetFormID(), casterId, target->GetFormID(),
                 effect->duration * mult, mult, effect->duration);
    effect->duration *= mult;
  }
}
