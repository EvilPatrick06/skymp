#pragma once
#include "PropertyBinding.h"

// Thornswood #2848. Readable and writable only inside an onSpellCast handler
// for the same caster: how many times its usual time each effect of that
// cast holds on the machines nearby (SpellCastEvent.h). It reads 1 until a
// handler sets it. Anywhere else it reads 1 and a write is refused.
class SpellCastDurationMultBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override
  {
    return "spellCastDurationMult";
  }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
