#include "TES5DamageFormula.h"

#include "HitData.h"
#include "MpActor.h"
#include "SpellCastData.h"
#include "WorldState.h"
#include "libespm/espm.h"

#include <algorithm>
#include <string>

namespace internal {

bool IsUnarmedAttack(const uint32_t sourceFormId)
{
  return sourceFormId == 0x1f4;
}

/*
  THORNSWOOD PATCH (#1384). A PIECE AT 120% CONDITION HITS AND HOLDS 5% HARDER.

  Thornswood's gamemode keeps a piece's condition on its name, the way the
  person sees it in the inventory: "Iron Sword (85%)". A plain name is 100%,
  and the most a repair puts a piece at is 120%, which only the half that
  makes the kit can do (docs/systems/durability.md in the Thornswood repo).
  That page's table: at 120% a weapon does 5% more damage and an armour
  piece gives 5% more of its own rating; clothing gets nothing.

  Nothing in this formula read an inventory entry's name before, so the
  numbers never moved. The name is read here the way the gamemode reads it
  (the SUFFIX pattern in its durability block): " (NN%)" at the end, one to
  three digits, held to 0..120. A name without one is 100.
*/
constexpr uint32_t kThornswoodConditionSound = 100;
constexpr uint32_t kThornswoodConditionHighest = 120;
constexpr float kThornswoodHighestConditionMult = 1.05f;

// Where " (NN%)" starts on the end of a name, one to three digits, or npos
// when the name does not end in one.
size_t ThornswoodConditionSuffixAt(const std::string& name)
{
  if (name.size() < 5 || name.compare(name.size() - 2, 2, "%)") != 0) {
    return std::string::npos;
  }
  const size_t open = name.rfind(" (");
  if (open == std::string::npos) {
    return std::string::npos;
  }
  const size_t first = open + 2;
  const size_t last = name.size() - 2; // one past the last digit
  if (last <= first || last - first > 3) {
    return std::string::npos;
  }
  for (size_t i = first; i < last; ++i) {
    if (name[i] < '0' || name[i] > '9') {
      return std::string::npos;
    }
  }
  return open;
}

uint32_t ThornswoodConditionOf(const Inventory::Entry& entry)
{
  if (!entry.name) {
    return kThornswoodConditionSound;
  }
  const std::string& name = *entry.name;
  const size_t open = ThornswoodConditionSuffixAt(name);
  if (open == std::string::npos) {
    return kThornswoodConditionSound;
  }
  uint32_t value = 0;
  for (size_t i = open + 2; i < name.size() - 2; ++i) {
    value = value * 10 + static_cast<uint32_t>(name[i] - '0');
  }
  return std::min(value, kThornswoodConditionHighest);
}

bool ThornswoodAtHighestCondition(const Inventory::Entry& entry)
{
  return ThornswoodConditionOf(entry) >= kThornswoodConditionHighest;
}

/*
  THORNSWOOD PATCH (#3151). A CRAFTED PIECE IS CRUDE, FINE OR MASTERWORK.

  Patrick, 22 October 2026: crafted things come in three tiers, crude, fine
  and masterwork, at minus or plus 10%. Thornswood's gamemode rolls the tier
  at the bench and writes it on the piece's name through craftOutput, right
  before any condition: "Iron Sword (Masterwork)", "Iron Sword (Crude) (85%)"
  (CRAFT_QUALITY in its gamemode.js; docs/systems/professions.md). Fine is
  the piece as its record has it and carries nothing. Crude is a tenth less of
  a weapon's damage or an armour piece's own rating, masterwork a tenth more.
*/
constexpr float kThornswoodCrudeMult = 0.9f;
constexpr float kThornswoodMasterworkMult = 1.1f;

float ThornswoodQualityMultOf(const Inventory::Entry& entry)
{
  if (!entry.name) {
    return 1.f;
  }
  std::string name = *entry.name;
  const size_t condition = ThornswoodConditionSuffixAt(name);
  if (condition != std::string::npos) {
    name.resize(condition);
  }
  auto endsWith = [&name](const char* tail) {
    const std::string t(tail);
    return name.size() > t.size() &&
      name.compare(name.size() - t.size(), t.size(), t) == 0;
  };
  if (endsWith(" (Masterwork)")) {
    return kThornswoodMasterworkMult;
  }
  if (endsWith(" (Crude)")) {
    return kThornswoodCrudeMult;
  }
  return 1.f;
}

// What a piece's name does to its one number: its condition at 120% and its
// quality, together.
float ThornswoodPieceMultOf(const Inventory::Entry& entry)
{
  return (ThornswoodAtHighestCondition(entry) ? kThornswoodHighestConditionMult
                                              : 1.f) *
    ThornswoodQualityMultOf(entry);
}

// Clothing carries no buff. BOD2 holds the armour type (0 light, 1 heavy,
// 2 clothing) after the body part flags. The BODT read in ARMO.cpp keeps its
// general flags in that slot rather than the type, so it is not asked; a
// piece of clothing has no armour rating to raise in any case.
bool ThornswoodIsClothing(const espm::ARMO::Data& armorData)
{
  return armorData.bod2.present && armorData.bod2.skill == 2;
}

class TES5DamageFormulaImpl
{
  using Effects = std::vector<espm::Effects::Effect>;

public:
  TES5DamageFormulaImpl(const MpActor& aggressor_, const MpActor& target_,
                        const HitData& hitData_);

  [[nodiscard]] float CalculateDamage() const;

private:
  const MpActor& aggressor;
  const MpActor& target;
  const HitData& hitData;
  WorldState* espmProvider;

private:
  [[nodiscard]] float GetBaseWeaponDamage() const;
  [[nodiscard]] float CalcWeaponPieceMult() const;
  [[nodiscard]] float CalcWeaponRating() const;
  [[nodiscard]] float CalcArmorRatingComponent(
    const Inventory::Entry& opponentEquipmentEntry) const;
  [[nodiscard]] float CalcOpponentArmorRating() const;
  [[nodiscard]] float CalcMagicEffects(const Effects& effects) const;
  [[nodiscard]] float DetermineDamageFromSource(uint32_t source) const;
  [[nodiscard]] float CalcUnarmedDamage() const;
  [[nodiscard]] float CalcArmorDamagePenalty() const;
};

TES5DamageFormulaImpl::TES5DamageFormulaImpl(const MpActor& aggressor_,
                                             const MpActor& target_,
                                             const HitData& hitData_)
  : aggressor(aggressor_)
  , target(target_)
  , hitData(hitData_)
  , espmProvider(aggressor.GetParent())
{
}

float TES5DamageFormulaImpl::GetBaseWeaponDamage() const
{
  const auto weapData =
    espm::GetData<espm::WEAP>(hitData.source, espmProvider);
  if (!weapData.weapData) {
    throw std::runtime_error(
      fmt::format("no weapData for {:#x}", hitData.source));
  }
  return weapData.weapData->damage;
}

// THORNSWOOD PATCH (#1384, #3151). The weapon the hit was landed with is the
// aggressor's worn entry of hitData.source. HitData carries no hand, so when
// both hands hold that same weapon the hit counts as the weaker of the two:
// a buff is never given for a piece that may not be the one that struck, so
// 120% needs both hands at 120%, and masterwork both hands masterwork.
float TES5DamageFormulaImpl::CalcWeaponPieceMult() const
{
  bool worn = false;
  float weakest = 1.f;
  const auto eq = aggressor.GetEquipment();
  for (const auto& entry : eq.inv.entries) {
    if (entry.baseId != hitData.source ||
        entry.GetWorn() == Inventory::Worn::None) {
      continue;
    }
    const float mult = ThornswoodPieceMultOf(entry);
    weakest = worn ? std::min(weakest, mult) : mult;
    worn = true;
  }
  return worn ? weakest : 1.f;
}

float TES5DamageFormulaImpl::CalcWeaponRating() const
{
  // TODO(#457): take other components into account
  return GetBaseWeaponDamage() * CalcWeaponPieceMult();
}

float TES5DamageFormulaImpl::CalcMagicEffects(const Effects& effects) const
{
  float armorRating = 0.f;
  for (const auto& effect : effects) {
    const auto actorValueType =
      espm::GetData<espm::MGEF>(effect.effectId, espmProvider).data.primaryAV;
    if (actorValueType == espm::ActorValue::DamageResist) {
      armorRating += effect.magnitude;
    }
  }
  return armorRating;
}

float TES5DamageFormulaImpl::CalcArmorRatingComponent(
  const Inventory::Entry& opponentEquipmentEntry) const
{
  if (opponentEquipmentEntry.GetWorn() != Inventory::Worn::None &&
      espm::GetRecordType(opponentEquipmentEntry.baseId, espmProvider) ==
        espm::ARMO::kType) {
    const auto armorData =
      espm::GetData<espm::ARMO>(opponentEquipmentEntry.baseId, espmProvider);
    // TODO(#458): take other components into account
    auto ac = static_cast<float>(armorData.baseRatingX100) / 100;
    // THORNSWOOD PATCH (#1384, #3151): 5% more of this piece's own rating at
    // 120% condition, and a tenth less or more for crude or masterwork. The
    // enchantment below is not the piece's rating.
    if (!ThornswoodIsClothing(armorData)) {
      ac *= ThornswoodPieceMultOf(opponentEquipmentEntry);
    }
    if (armorData.enchantmentFormId) {
      // TODO(#632) refactor this effect with actor effect system
      const auto enchantmentData =
        espm::GetData<espm::ENCH>(armorData.enchantmentFormId, espmProvider);
      ac += CalcMagicEffects(enchantmentData.effects);
    }

    return ac;
  }
  return 0;
}

float TES5DamageFormulaImpl::CalcOpponentArmorRating() const
{
  float combinedArmorRating = 0;
  auto eq = target.GetEquipment();
  for (auto& entry : eq.inv.entries) {
    combinedArmorRating += CalcArmorRatingComponent(entry);
  }
  return combinedArmorRating;
}

float TES5DamageFormulaImpl::CalcUnarmedDamage() const
{
  const uint32_t raceId = aggressor.GetRaceId();
  return espm::GetData<espm::RACE>(raceId, espmProvider).unarmedDamage;
}

float TES5DamageFormulaImpl::DetermineDamageFromSource(uint32_t source) const
{
  return IsUnarmedAttack(source) ? CalcUnarmedDamage() : CalcWeaponRating();
}

float TES5DamageFormulaImpl::CalcArmorDamagePenalty() const
{
  // TODO(#457): weapon rating is probably not only component of incomingDamage
  // Replace this with another issue reference upon investigation
  const float maxArmorRating =
    espm::GetData<espm::GMST>(espm::GMST::kFMaxArmorRating, espmProvider)
      .value;
  const float armorScalingFactor =
    espm::GetData<espm::GMST>(espm::GMST::kFArmorScalingFactor, espmProvider)
      .value;
  return 0.01f *
    (100.f -
     std::min<float>(CalcOpponentArmorRating() * armorScalingFactor,
                     maxArmorRating));
}

float TES5DamageFormulaImpl::CalculateDamage() const
{
  const float incomingDamage = DetermineDamageFromSource(hitData.source);

  // TODO(#461): add difficulty multiplier
  // TODO(#463): add sneak modifier
  float damage = incomingDamage * CalcArmorDamagePenalty();

  if (hitData.isPowerAttack) {
    damage *= 2.f;
  }

  if (hitData.isHitBlocked) {
    // TODO(#460): implement correct block formula
    damage *= 0.1f;
  }

  if (hitData.isSneakAttack) {
    // TODO(GM-613): get from GameSettings
    damage *= 1.3f;
  }

  return damage;
}

class TES5SpellDamageFormulaImpl
{
  using Effects = std::vector<espm::Effects::Effect>;

public:
  TES5SpellDamageFormulaImpl(const MpActor& aggressor_, const MpActor& target_,
                             const SpellCastData& spellCastData_);

  [[nodiscard]] float CalculateDamage() const;

private:
  const MpActor& aggressor;
  const MpActor& target;
  const SpellCastData& spellCastData;
  WorldState* espmProvider;

private:
  [[nodiscard]] float GetBaseSpellDamage() const;
};

TES5SpellDamageFormulaImpl::TES5SpellDamageFormulaImpl(
  const MpActor& aggressor_, const MpActor& target_,
  const SpellCastData& spellCastData_)
  : aggressor(aggressor_)
  , target(target_)
  , spellCastData(spellCastData_)
  , espmProvider(aggressor.GetParent())
{
}

float TES5SpellDamageFormulaImpl::GetBaseSpellDamage() const
{
  const auto spellData =
    espm::GetData<espm::SPEL>(spellCastData.spell, espmProvider);

  float damage = 0.f;

  for (const auto& effect : spellData.effects) {

    if (!effect.effectItem || effect.effectFormId == 0) {
      continue;
    }

    auto magicEffect =
      espm::GetData<espm::MGEF>(effect.effectFormId, espmProvider);

    const bool needAddDamage =
      magicEffect.data.IsFlagSet(espm::MGEF::Flags::Hostile) ||
      magicEffect.data.IsFlagSet(espm::MGEF::Flags::Detrimental);

    if (needAddDamage &&
        magicEffect.data.primaryAV == espm::ActorValue::Health) {

      damage += effect.effectItem->magnitude;
    }
  }
  return damage;
}

float TES5SpellDamageFormulaImpl::CalculateDamage() const
{
  return GetBaseSpellDamage();
}

}

float TES5DamageFormula::CalculateDamage(const MpActor& aggressor,
                                         const MpActor& target,
                                         const HitData& hitData) const
{
  return internal::TES5DamageFormulaImpl(aggressor, target, hitData)
    .CalculateDamage();
}

float TES5DamageFormula::CalculateDamage(
  const MpActor& aggressor, const MpActor& target,
  const SpellCastData& spellCastData) const
{
  return internal::TES5SpellDamageFormulaImpl(aggressor, target, spellCastData)
    .CalculateDamage();
}
