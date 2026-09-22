#include "DamageMultFormula.h"

#include "MpActor.h"
#include <nlohmann/json.hpp>

namespace {

bool IsNonPlayerBaseId(const MpActor& actor)
{
  return actor.GetBaseId() != 0x7;
}

DamageMultFormula::Settings ParseConfig(const nlohmann::json& config)
{
  DamageMultFormula::Settings settings;

  if (!config.is_object()) {
    spdlog::warn(
      "Invalid damage mult formula config format. Using default one.");
    return settings;
  }

  /*
    THORNSWOOD. Each key is read on its own, and a missing one is not a reason
    to throw the other away.

    This returned the whole default settings object the moment "multiplier" was
    absent, so a file that set only playerMultiplier would have been ignored
    entirely and silently. Read what is there, keep the default for what is not,
    and say which is which.
  */
  if (config.contains("multiplier")) {
    settings.multiplier = config.at("multiplier").get<float>();
  } else {
    spdlog::warn("Unable to get multiplier from config. NPC hits on a person "
                 "are multiplied by the default {}",
                 settings.multiplier);
  }

  if (config.contains("playerMultiplier")) {
    settings.playerMultiplier = config.at("playerMultiplier").get<float>();
  }

  spdlog::info("DamageMultFormula: an NPC hitting a person is multiplied by "
               "{}, a person hitting an NPC by {}",
               settings.multiplier, settings.playerMultiplier);

  return settings;
}

}

DamageMultFormula::DamageMultFormula(
  std::unique_ptr<IDamageFormula> baseFormula_, const nlohmann::json& config_)
  : baseFormula(std::move(baseFormula_))
{
  settings = ParseConfig(config_);
}

float DamageMultFormula::CalculateDamage(const MpActor& aggressor,
                                         const MpActor& target,
                                         const HitData& hitData) const
{
  float baseDamage = baseFormula->CalculateDamage(aggressor, target, hitData);

  auto worldState = aggressor.GetParent();
  if (!worldState) {
    return baseDamage;
  }

  /*
    THORNSWOOD. Both directions, each with its own number. See the note in the
    header for what Patrick measured and why the second one has to exist.
  */
  if (IsNonPlayerBaseId(aggressor) && !IsNonPlayerBaseId(target)) {
    baseDamage *= settings.multiplier;
  } else if (!IsNonPlayerBaseId(aggressor) && IsNonPlayerBaseId(target)) {
    baseDamage *= settings.playerMultiplier;
  }

  return baseDamage;
}

float DamageMultFormula::CalculateDamage(
  const MpActor& aggressor, const MpActor& target,
  const SpellCastData& spellCastData) const
{
  float baseDamage =
    baseFormula->CalculateDamage(aggressor, target, spellCastData);

  auto worldState = aggressor.GetParent();
  if (!worldState) {
    return baseDamage;
  }

  // THORNSWOOD. The same two directions as the weapon path above.
  if (IsNonPlayerBaseId(aggressor) && !IsNonPlayerBaseId(target)) {
    baseDamage *= settings.multiplier;
  } else if (!IsNonPlayerBaseId(aggressor) && IsNonPlayerBaseId(target)) {
    baseDamage *= settings.playerMultiplier;
  }

  return baseDamage;
}
