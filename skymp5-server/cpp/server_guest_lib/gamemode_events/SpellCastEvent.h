#pragma once

#include <cstdint>

#include "GameModeEvent.h"

class MpActor;

/*
  THORNSWOOD PATCH (Thornswood #2848). onSpellCast(caster, spell,
  askedDurationMult), raised by ActionListener::OnSpellCast for a cast it has
  checked (the sender's own actor or one it hosts, alive, with the spell
  equipped) and BEFORE the cast goes out to anybody nearby. It is not raised
  for the second message that interrupts a cast.

  A spell's effects run on the machine that runs the actor they land on, so
  how long one holds is decided there. A cast carries how many times its
  usual time its effects hold (SpellCastMsgData::durationMult): the caster's
  own machine has already given its own effects what it asked for. What the
  machines nearby give theirs is the server's answer: the
  spellCastDurationMult property of the caster, readable and writable only
  inside this event (SpellCastDurationMultBinding). It starts at 1, the
  game's own, whatever the caster asked for, so nothing a client claims
  reaches anybody else unless a listener agrees to it.

  A false from a listener refuses nothing: the cast was already made on the
  caster's own machine.
*/
class SpellCastEvent : public GameModeEvent
{
public:
  SpellCastEvent(MpActor* caster_, uint32_t spellId_,
                 float askedDurationMult_);

  const char* GetName() const override;

  std::string GetArgumentsJsonArray() const override;

  uint32_t GetCasterId() const;
  float GetDurationMult() const;
  void SetDurationMult(float mult);

private:
  void OnFireSuccess(WorldState* worldState) override {}

  MpActor* caster = nullptr;
  uint32_t spellId = 0;
  float askedDurationMult = 1.f;

  float durationMult = 1.f;
};
