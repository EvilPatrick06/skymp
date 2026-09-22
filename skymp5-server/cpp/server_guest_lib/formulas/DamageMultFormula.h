#pragma once
#include <memory>

#include "IDamageFormula.h"
#include <nlohmann/json_fwd.hpp>

class DamageMultFormula : public IDamageFormula
{
public:
  DamageMultFormula(std::unique_ptr<IDamageFormula> baseFormula_,
                    const nlohmann::json& config);

  [[nodiscard]] float CalculateDamage(const MpActor& aggressor,
                                      const MpActor& target,
                                      const HitData& hitData) const override;

  [[nodiscard]] float CalculateDamage(
    const MpActor& aggressor, const MpActor& target,
    const SpellCastData& spellCastData) const override;

public:
  /*
    THORNSWOOD. TWO MULTIPLIERS, AND THE OLD NAME STILL MEANS WHAT IT MEANT.

    Patrick, 21 September, playing it: "enimes hit way to hard and have to much
    health or dont die at all". Both halves of that sentence are this class.

    HIT TOO HARD: this had ONE multiplier, applied when an NPC hits a person,
    and it defaults to 2. server-settings.json carries no
    damageMultFormulaSettings at all, so the server logs "Unable to get
    multiplier from config" at startup and doubles every blow landed on a
    person, and nobody put that number there on purpose.

    TOO MUCH HEALTH: the other direction was never multiplied by anything. The
    base formula is TES5DamageFormula, which reads the weapon's base damage, the
    target's armour, and the power, block and sneak flags, and NOTHING about the
    person swinging: no skill, no perks, no attackDamageMult. So a person hits
    for the number printed on the weapon while an NPC keeps its full vanilla
    health pool, and a giant takes thirty swings of an iron sword. The health is
    not wrong, the damage against it is.

    multiplier keeps its name and its meaning, so an existing settings file
    behaves exactly as it did. playerMultiplier is the new one and defaults to
    1, so a server that does not set it also behaves exactly as it did.
  */
  struct Settings
  {
    float multiplier = 2.f;
    float playerMultiplier = 1.f;
  };

private:
  std::unique_ptr<IDamageFormula> baseFormula;
  Settings settings;
};
