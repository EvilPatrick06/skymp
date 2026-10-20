#include "SpellCastEvent.h"

#include "MpActor.h"
#include <cmath>
#include <fmt/format.h>

namespace {
// The most a listener may hold a cast's effects for, times their usual
// time. Anything else (not a number, nothing, or less) is the game's own.
constexpr float kMaxDurationMult = 10.f;

float Checked(float mult)
{
  if (!std::isfinite(mult) || mult <= 0.f) {
    return 1.f;
  }
  return mult > kMaxDurationMult ? kMaxDurationMult : mult;
}
}

SpellCastEvent::SpellCastEvent(MpActor* caster_, uint32_t spellId_,
                               float askedDurationMult_)
  : caster(caster_)
  , spellId(spellId_)
  , askedDurationMult(Checked(askedDurationMult_))
{
}

const char* SpellCastEvent::GetName() const
{
  return "onSpellCast";
}

std::string SpellCastEvent::GetArgumentsJsonArray() const
{
  return fmt::format("[{},{},{}]", caster ? caster->GetFormId() : 0, spellId,
                     askedDurationMult);
}

uint32_t SpellCastEvent::GetCasterId() const
{
  return caster ? caster->GetFormId() : 0;
}

float SpellCastEvent::GetDurationMult() const
{
  return durationMult;
}

void SpellCastEvent::SetDurationMult(float mult)
{
  durationMult = Checked(mult);
}
