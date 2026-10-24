#pragma once

// THORNSWOOD PATCH (Thornswood #2848). The game's side of CastDurations (in
// platform_lib, which says what it is): which spells hold, and the effects
// the game starts.
namespace CastDurationsGame {

// A spell whose effects hold for a time that Overcharge doubles: a spell
// (not a power, a shout or an ability), cast and let go (not held, like a
// ward or a stream of flame), with at least one effect that lasts a time, and
// no effect that takes health (one that is hostile and works on Health).
// Thornswood's server judges the cast by the same rule from the same record
// (the skill perks block in game/gamemode.js), so both count the same casts.
bool Holds(const RE::MagicItem* spell);

// The game's cast event, for a cast this machine made. Returns how many
// times their usual time its effects hold, and gives that to the ones of
// them the game had already started.
float OnOwnCast(RE::Actor* caster, RE::MagicItem* spell);

// The game starting an effect on an actor this machine runs.
void OnEffectStart(RE::Actor* target, RE::ActiveEffect* effect);

}
