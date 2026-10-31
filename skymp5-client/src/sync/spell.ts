import { Actor, ActorBase, Game, Spell, printConsole } from 'skyrimPlatform';

export const removeAllSpells = (actor: Actor) => {
  let spellToRemove = new Array<Spell>();

  for (let i = 0; i < actor.getSpellCount(); i++) {
    const spell = actor.getNthSpell(i);

    if (spell) {
      spellToRemove.push(spell);
    }
  }

  // THORNSWOOD (#1688). The spells on the actor's base record as well. The
  // local player is the Player record (0x7), every person on the server is
  // created on it, and its spell list is what a new game starts with: Flames
  // and Healing. getNthSpell above only walks the spells added to the actor,
  // so those two stayed in the magic menu, castable, whatever the server's
  // list said. The server no longer counts them as held (MpActor
  // IsSpellLearnedFromBase), so the list the server sends is the whole of
  // what a person casts. The record's abilities stay (PCHealRateCombat),
  // as the server keeps them: see isPlayerRecordAbility.
  for (let spell of playerRecordSpellsToDrop(actor)) {
    if (spellToRemove.some((s) => s.getFormID() === spell.getFormID())) {
      continue;
    }
    spellToRemove.push(spell);
  }

  for (let spell of spellToRemove) {
    const removeResult = actor.removeSpell(spell);
    printConsole(
      `removeResult: ${removeResult}, spellIdToRemove: ${spell
        .getFormID()
        .toString(16)}, spellName: ${spell.getName()}`,
    );
  }
};

const kPlayerRecordId = 0x7;
const kConstantEffect = 0;

// An ability is a constant effect spell, and the Skyrim Platform has no call
// for a spell's SPIT type, so a spell all of whose effects are constant
// effects is taken for one. Flames and Healing are concentration spells.
export const isPlayerRecordAbility = (spell: Spell): boolean => {
  const n = spell.getNumEffects();
  if (n <= 0) {
    return false;
  }
  for (let i = 0; i < n; i++) {
    const effect = spell.getNthEffectMagicEffect(i);
    if (!effect || effect.getCastingType() !== kConstantEffect) {
      return false;
    }
  }
  return true;
};

// The spells on the Player record a person does not hold: every one of its
// spell list but its abilities. Nothing for any other actor.
export const playerRecordSpellsToDrop = (actor: Actor): Array<Spell> => {
  const base = ActorBase.from(actor.getBaseObject());
  if (!base || base.getFormID() !== kPlayerRecordId) {
    return [];
  }
  const res = new Array<Spell>();
  const count = base.getSpellCount();
  for (let i = 0; i < count; i++) {
    const spell = base.getNthSpell(i);
    if (spell && !isPlayerRecordAbility(spell)) {
      res.push(spell);
    }
  }
  return res;
};

export const learnSpells = (actor: Actor, spellsIds: Array<number>) => {
  for (let spellId of spellsIds) {
    const spell = Spell.from(Game.getFormEx(spellId));

    if (spell) {
      const addResult = actor.addSpell(spell, false);
      printConsole(
        `addResult: ${addResult}, spellIdToLearn: ${spell
          .getFormID()
          .toString(16)}, spellName: ${spell.getName()}`,
      );
    }
  }
};
