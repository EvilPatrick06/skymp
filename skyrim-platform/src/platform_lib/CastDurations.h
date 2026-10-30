#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

/*
  THORNSWOOD PATCH. How long the effects of each cast hold on this machine
  (Thornswood #2848, Overcharge: "one spell in five ... holds twice as long").

  A spell's effects run on the machine that runs the actor they land on, and
  the game gives each one its duration there. So the cast has to say, on every
  machine, how many times its usual time its effects hold:

  - A cast this machine makes itself (the game's cast event, for this
    machine's own character): a caster with a rule (SetEvery, which a plugin
    sets for its own character while Overcharge is held) counts its casts of
    spells that hold, and every `every`th one holds `mult` times as long. That
    is what the cast message tells the server (skymp5-client's
    magicSyncService.ts), and the server decides what everybody else's
    machine gives it.
  - A cast another machine made, played here (castSpellImmediate): the
    server's answer for it, carried in the cast message.

  An effect does not say which cast made it, only its caster and its spell.
  So an effect that starts takes what the latest cast of that spell by that
  caster holds for, on this machine. Every effect of one cast takes the same,
  however many it lands on and however late its projectile lands, until that
  caster casts that spell again.

  The game may start a cast's own effects (a spell on yourself) just before
  or just after it raises the cast event. So for a caster with a rule, the
  effects of its spell that started since its last cast of it are handed back
  by OnOwnCast, with the duration the game gave them, and the caller gives
  the ones that have not run yet this cast's answer.

  Nothing here knows the game, which is what lets unit\platform_lib_tests
  test it.
*/
class CastDurations
{
public:
  // An effect a caster with a rule started, with the duration the game gave
  // it.
  struct Started
  {
    uint32_t targetFormId = 0;
    uint16_t uniqueId = 0;
    float gameDuration = 0.f;
  };

  static CastDurations& GetSingleton();

  // Every `every`th cast of a spell that holds by this caster holds `mult`
  // times its usual time. every=0 takes the rule off, and forgets the count.
  void SetEvery(uint32_t casterFormId, uint32_t every, float mult);
  bool HasRule(uint32_t casterFormId) const;

  // A cast this machine made: returns how many times its usual time each of
  // its effects holds. A caster with no rule gets 1 and changes nothing.
  // started gets the effects of this spell by this caster that began since
  // its last cast of it.
  float OnOwnCast(uint32_t casterFormId, uint32_t spellFormId, bool holds,
                  std::vector<Started>* started);

  // A cast another machine made, played on this one, with the server's
  // answer for it.
  void OnPlayedCast(uint32_t casterFormId, uint32_t spellFormId, float mult);

  // An effect of this spell by this caster starting here, with the duration
  // the game gave it. Returns how many times that it holds.
  float OnEffectStart(uint32_t casterFormId, uint32_t spellFormId,
                      uint32_t targetFormId, uint16_t uniqueId,
                      float gameDuration);

  void Clear();

private:
  using Key = std::pair<uint32_t, uint32_t>; // caster, spell

  struct Rule
  {
    uint32_t every = 0;
    float mult = 1.f;
    uint32_t counted = 0; // casts that hold since the last one that held longer
  };

  mutable std::mutex m;
  std::map<uint32_t, Rule> rules;
  std::map<Key, float> latest;
  std::map<Key, std::vector<Started>> startedSinceCast;
};
