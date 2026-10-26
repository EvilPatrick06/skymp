import {
  getExtraContainerChanges,
  getContainer,
  BaseExtraList,
  ExtraHealth,
  ObjectReference,
  TESModPlatform,
  Game,
  storage,
  ExtraCount,
  ExtraEnchantment,
  ExtraPoison,
  ExtraSoul,
  ExtraTextDisplayData,
  ExtraCharge,
  Enchantment,
  Potion,
  Actor,
  Ammo,
  printConsole,
  ActorBase,
  FormType,
  Form,
  renameInventoryItems,
  removeInventoryItems,
  createMadePotion,
} from "skyrimPlatform";

/*
  THORNSWOOD PATCH (Thornswood #1649). A brewed bottle, kept as what it is.

  A potion brewed at a lab is a form the brewing game made while it ran,
  above 0xff000000, so its id means nothing to the next session or to any
  other client. The server keeps it by its contents instead (Inventory::Made
  in the fork's server) under an id worked out from them, in the top half of
  the runtime range (0xff800000 and up), and sends it as an entry carrying
  `made`. ensureMadePotion builds a potion with exactly that id here
  (createMadePotion), so the bottle in the game, and everything this client
  sends about it afterwards (drinking it, putting it in a chest), carry the
  server's id.
*/
export interface MadeEffect {
  effectId: number; // the magic effect record (MGEF), a global form id
  magnitude: number;
  area: number;
  duration: number;
}

export interface Made {
  name: string;
  poison: boolean;
  effects: MadeEffect[];
}

export interface Extra {
  made?: Made;
  health?: number;
  enchantmentId?: number;
  maxCharge?: number;
  removeEnchantmentOnUnequip?: boolean;
  chargePercent?: number;
  name?: string;
  soul?: 0 | 1 | 2 | 3 | 4 | 5;
  poisonId?: number;
  poisonCount?: number;
  worn?: boolean;
  wornLeft?: boolean;
}

export interface BasicEntry {
  baseId: number;
  count: number;
}

export type Entry = BasicEntry & Extra;

export interface Inventory {
  entries: Entry[];
}

/*
  THORNSWOOD PATCH (Thornswood #2146). A health is compared as the game's
  reading of it, on both sides.

  extractExtraData reads the game's ExtraHealth to a tenth and leaves 1 out,
  so a fine piece reads 1.1. The server keeps health as a float
  (Inventory::ExtraData in Inventory.h) and the client gets it through
  nlohmann::json, which writes a float as the double it widens to: 1.1 goes
  out as 1.100000023841858, 1.2 as 1.2000000476837158 (measured with
  nlohmann 3.11.3, 22 Oct). The two were compared with ===, so whenever the
  server held a tempered piece, it never equalled the same piece in the
  game. Every pass took it away and added it again, the add announced
  itself, and the next pass found the same difference.

  So both sides go through healthOf, the one reading, before they are
  compared. What is sent to the game is still the server's own number.
*/
const healthOf = (e: Entry): number | undefined => {
  if (typeof e.health !== "number") {
    return undefined;
  }
  const tenths = Math.round(e.health * 10) / 10;
  // TESModPlatform::AddItemEx writes no ExtraHealth for 1, so 1 is none
  return tenths === 1 ? undefined : tenths;
};

/*
  THORNSWOOD PATCH (Thornswood #1651). What the game adds to a name, and only
  that.

  The game writes a tempered piece's level on the end of its name when it
  draws it: ExtraTextDisplayData keeps the name it was given and its length
  (customNameLength in CommonLib), and GetDisplayName adds " (Fine)" up to
  " (Legendary)" (the sHealthDataPrefixWeap and sHealthDataPrefixArmo game
  settings) for the piece's health. So "loxsword" reads back as "loxsword
  (Legendary)", and a piece with no name of its own as "Iron Sword (Fine)".
  A piece that is not tempered has a health of 1, which extractExtraData
  leaves out, and gets nothing added.

  This used to drop any trailing "(...)" from both names before comparing.
  That made "Iron Sword (85%)" equal to "Iron Sword (84%)" and to a plain
  "Iron Sword", so a piece the server renamed (a Thornswood piece carries its
  condition on its name) never reached the game: getDiff found no difference
  and applyInventory added nothing until the next login. Now a name equals
  another exactly, or, on a tempered piece, with one level added to it.
*/
const isTempered = (e: Entry): boolean => (healthOf(e) ?? 1) > 1;

// named is plain with the game's " (...)" level on the end
const hasLevelAdded = (named: string, plain: string): boolean =>
  named.length > plain.length &&
  named.substring(0, plain.length) === plain &&
  /^ \([^()]+\)$/.test(named.substring(plain.length));

// 'aaaaaaaaaaaaaaaa' => 'aaa...'
const cropName = (s?: string): string => {
  if (!s) {
    return s as string;
  }

  const max = 128;
  return s.length >= max
    ? s
      .split("")
      .filter((x, i) => i < max)
      .join("")
      .concat("...")
    : s;
};

const namesEqual = (a: Entry, b: Entry): boolean => {
  let aStr = a.name || "";
  let bStr = b.name || "";
  if (cropName(aStr) === cropName(bStr)) {
    return true;
  }

  // No name is the base's own name: applyInventory writes the form's name
  // on a piece the server sends without one.
  if (a.baseId === b.baseId && (!aStr || !bStr)) {
    const form = Game.getFormEx(a.baseId);
    const formName = form ? form.getName() : "";
    if (formName) {
      aStr = aStr || formName;
      bStr = bStr || formName;
      if (cropName(aStr) === cropName(bStr)) {
        return true;
      }
    }
  }

  if (isTempered(a) || isTempered(b)) {
    return hasLevelAdded(aStr, bStr) || hasLevelAdded(bStr, aStr);
  }

  return false;
};

const madeEqual = (a?: Made, b?: Made): boolean =>
  (!a && !b) || (!!a && !!b && JSON.stringify(a) === JSON.stringify(b));

const extrasEqual = (a: Entry, b: Entry, ignoreWorn = false) => {
  return (
    madeEqual(a.made, b.made) &&
    healthOf(a) === healthOf(b) &&
    a.enchantmentId === b.enchantmentId &&
    a.maxCharge === b.maxCharge &&
    !!a.removeEnchantmentOnUnequip === !!b.removeEnchantmentOnUnequip &&
    (a.chargePercent ?? 0) === (b.chargePercent ?? 0) &&
    namesEqual(a, b) &&
    a.soul === b.soul &&
    a.poisonId === b.poisonId &&
    a.poisonCount === b.poisonCount &&
    ((!!a.worn === !!b.worn && !!a.wornLeft === !!b.wornLeft) || ignoreWorn)
  );
};

export const inventoryEntriesEqual = (a: Entry, b: Entry, ignoreWorn = false): boolean =>
  a.baseId === b.baseId && extrasEqual(a, b, ignoreWorn);

// Thornswood #1651. The same piece under another name: what a rename by the
// server leaves behind.
export const inventoryEntriesEqualExceptName = (a: Entry, b: Entry, ignoreWorn = false): boolean =>
  a.baseId === b.baseId &&
  extrasEqual({ ...a, name: undefined }, { ...b, name: undefined }, ignoreWorn);

export const hasExtras = (e: Entry): boolean => {
  return !extrasEqual(e, { baseId: 0, count: 0 });
};

// What this session built under which id (Thornswood #1649). In storage, so
// a hot reload of the client keeps it, as the game keeps the forms.
const madePotions = (): Map<number, Made> => {
  if (!storage) {
    return new Map<number, Made>();
  }
  if (!(storage["thornswoodMadePotions"] instanceof Map)) {
    storage["thornswoodMadePotions"] = new Map<number, Made>();
  }
  return storage["thornswoodMadePotions"] as Map<number, Made>;
};

const kRuntimeFirst = 0xff000000;
const kMadeIdFirst = 0xff800000;

// Thornswood #1649. What is in a bottle the game brewed this session, read
// off the form, for the craft message (craftService.ts). Nothing for a form
// that is not a runtime potion, or one already built from the server's
// description (that one is the server's already).
export const readMadePotion = (form: Form | null): Made | undefined => {
  const potion = Potion.from(form);
  if (!form || !potion) {
    return undefined;
  }
  const id = form.getFormID();
  if (id < kRuntimeFirst || madePotions().has(id)) {
    return undefined;
  }
  const effects: MadeEffect[] = [];
  const n = potion.getNumEffects();
  for (let i = 0; i < n; ++i) {
    const mgef = potion.getNthEffectMagicEffect(i);
    if (!mgef) {
      return undefined;
    }
    effects.push({
      effectId: mgef.getFormID(),
      magnitude: Math.round(potion.getNthEffectMagnitude(i) * 100) / 100,
      area: Math.round(potion.getNthEffectArea(i)),
      duration: Math.round(potion.getNthEffectDuration(i)),
    });
  }
  if (!effects.length) {
    return undefined;
  }
  return { name: form.getName(), poison: potion.isPoison(), effects };
};

// Thornswood #1649. Makes sure this session has the server's bottle `id`,
// building it from `made` the first time it is seen. False when it cannot:
// an id outside the made range, a Skyrim Platform without createMadePotion,
// or another form already under that id (two contents whose ids collide,
// which the id's 23 bits make rare; the second is then left out and logged,
// and the first stays right).
export const ensureMadePotion = (id: number, made: Made): boolean => {
  const built = madePotions();
  const had = built.get(id);
  if (had) {
    if (!madeEqual(had, made)) {
      printConsole(`Made potion ${id.toString(16)} is already another brew in this session`);
      return false;
    }
    return true;
  }
  if (id < kMadeIdFirst || typeof createMadePotion !== "function") {
    printConsole(`Unable to build made potion ${id.toString(16)}`);
    return false;
  }
  const ok = createMadePotion(
    id,
    made.name || "",
    !!made.poison,
    made.effects.map((e) => e.effectId),
    made.effects.map((e) => e.magnitude),
    made.effects.map((e) => e.area),
    made.effects.map((e) => e.duration),
  );
  if (!ok) {
    printConsole(`createMadePotion failed for ${id.toString(16)}`);
    return false;
  }
  built.set(id, JSON.parse(JSON.stringify(made)));
  return true;
};

const extractExtraData = (
  refr: ObjectReference,
  extraList: BaseExtraList | null,
  out: Entry
): void => {
  // I see that ExtraWorn is not emitted for 0xFF actors when arrows are equipped. Fixing
  const item = Game.getFormEx(out.baseId);
  if (Ammo.from(item)) {
    const actor = Actor.from(refr);
    if (actor && actor.isEquipped(item)) {
      out.worn = true;
    }
  }

  (extraList || []).forEach((extra) => {
    switch (extra.type) {
      case "Health":
        out.health = healthOf({ baseId: out.baseId, count: 0, health: (extra as ExtraHealth).health });
        if (out.health === undefined) {
          delete out.health;
        }
        break;
      case "Count":
        out.count = (extra as ExtraCount).count;
        break;
      case "Enchantment":
        out.enchantmentId = (extra as ExtraEnchantment).enchantmentId;
        out.maxCharge = (extra as ExtraEnchantment).maxCharge;
        out.removeEnchantmentOnUnequip = (
          extra as ExtraEnchantment
        ).removeOnUnequip;
        break;
      case "Charge":
        out.chargePercent = (extra as ExtraCharge).charge;
        break;
      case "Poison":
        out.poisonId = (extra as ExtraPoison).poisonId;
        out.poisonCount = (extra as ExtraPoison).count;
        break;
      case "Soul":
        out.soul = (extra as ExtraSoul).soul;
        break;
      case "TextDisplayData":
        out.name = (extra as ExtraTextDisplayData).name;
        break;
      case "Worn":
        out.worn = true;
        break;
      case "WornLeft":
        out.wornLeft = true;
        break;
    }
  });
};

const squash = (inv: Inventory, ignoreWorn = false): Inventory => {
  const res = new Array<Entry>();
  inv.entries.forEach((e) => {
    const same = res.find((x) => e.baseId === x.baseId && extrasEqual(x, e, ignoreWorn));
    if (same) {
      same.count += e.count;
    } else {
      res.push(JSON.parse(JSON.stringify(e)));
    }
  });
  return { entries: res.filter((x) => x.count !== 0) };
};

const getExtraContainerChangesAsInventory = (
  refr: ObjectReference
): Inventory => {
  const extraContainerChanges = getExtraContainerChanges(refr.getFormID());
  const entries = new Array<Entry>();

  (extraContainerChanges || []).forEach((changesEntry) => {
    const entry: Entry = {
      baseId: changesEntry.baseId,
      count: changesEntry.countDelta,
    };

    (changesEntry.extendDataList || []).forEach((extraList) => {
      const e: Entry = {
        baseId: entry.baseId,
        count: 1,
      };
      extractExtraData(refr, extraList, e);
      entries.push(e);
      entry.count -= e.count;
    });

    if (entry.count !== 0) {
      entries.push(entry);
    }
  });

  // Thornswood #1649: a bottle built from the server's description is that
  // description, the way the server holds it.
  const built = madePotions();
  if (built.size) {
    entries.forEach((e) => {
      const made = built.get(e.baseId);
      if (made) {
        e.made = JSON.parse(JSON.stringify(made));
      }
    });
  }

  let res: Inventory = { entries };
  res = squash(res);
  return res;
};

const getBaseContainerAsInventory = (refr: ObjectReference): Inventory => {
  return {
    entries: getContainer((refr.getBaseObject() as ActorBase).getFormID()),
  };
};

export const sumInventories = (lhs: Inventory, rhs: Inventory): Inventory => {
  const leftEntriesWithExtras = lhs.entries.filter((e) => hasExtras(e));
  const rightEntriesWithExtras = rhs.entries.filter((e) => hasExtras(e));
  const leftEntriesSimple = lhs.entries.filter((e) => !hasExtras(e));
  const rightEntriesSimple = rhs.entries.filter((e) => !hasExtras(e));

  leftEntriesSimple.forEach((e) => {
    const matching = rightEntriesSimple.find((x) => x.baseId === e.baseId);
    if (matching) {
      e.count += matching.count;
      matching.count = 0;
    }
  });

  return {
    entries: leftEntriesWithExtras
      .concat(rightEntriesWithExtras)
      .concat(leftEntriesSimple)
      .concat(rightEntriesSimple)
      .filter((e) => e.count !== 0),
  };
};

export const removeSimpleItemsAsManyAsPossible = (
  inv: Inventory,
  baseId: number,
  count: number
): Inventory => {
  const res: Inventory = { entries: [] };
  res.entries = JSON.parse(JSON.stringify(inv.entries));

  const entry = res.entries.find((e) => !hasExtras(e) && e.baseId === baseId);
  if (entry) {
    entry.count -= count;
  }

  res.entries = res.entries.filter((e) => e.count > 0);
  return res;
};

export const getDiff = (
  lhs: Inventory,
  rhs: Inventory,
  ignoreWorn: boolean
): Inventory => {
  const lhsCopy: Inventory = squash(JSON.parse(JSON.stringify(lhs)), ignoreWorn);
  const rhsCopy: Inventory = squash(JSON.parse(JSON.stringify(rhs)), ignoreWorn);

  rhsCopy.entries.forEach((e) => {
    const sameFromLeft = lhsCopy.entries.find(
      (x) => x.baseId === e.baseId && extrasEqual(x, e, ignoreWorn)
    );
    if (sameFromLeft) {
      sameFromLeft.count -= e.count;
    } else {
      lhsCopy.entries.push(e);
      lhsCopy.entries[lhsCopy.entries.length - 1].count *= -1;
    }
  });

  return { entries: lhsCopy.entries.filter((x) => x.count !== 0) };
};

export const getInventory = (refr: ObjectReference): Inventory => {
  return squash(
    sumInventories(
      getBaseContainerAsInventory(refr),
      getExtraContainerChangesAsInventory(refr)
    )
  );
};

const basesReset = (): Set<number> => {
  if (storage["basesResetExists"] !== true) {
    storage["basesResetExists"] = true;
    storage["basesReset"] = new Set<number>();
  }
  return storage["basesReset"] as Set<number>;
};

export const acceptPreloadedInventoryBase = (refr: ObjectReference): void => {
  const base = refr.getBaseObject();
  if (base) { basesReset().add(base.getFormID()); }
};

export const resetInventoryBase = (refr: ObjectReference): boolean => {
  const base = refr.getBaseObject();
  const baseId = base ? base.getFormID() : 0;
  if (!basesReset().has(baseId)) {
    basesReset().add(baseId);
    TESModPlatform.resetContainer(base);

    refr.removeAllItems(null, false, true);
    return true;
  }
  return false;
};

/*
  THORNSWOOD PATCH (Thornswood #2146). What the game holds is taken away from
  where it lies.

  A piece was taken away by TESModPlatform.addItemEx with a negative count,
  and the native built a NEW extra list out of the entry (its health, its
  name and the rest) and handed that to RemoveItem. That list is never one
  the game holds, so which units RemoveItem took was the game's choice, not
  the diff's, and nothing checked it. A piece tempered at a bench is where
  that shows: the temper happens in the game, the server keeps the piece
  plain (recipelessCraft refuses a temper, #1384), and the game must take
  away a tempered list that also carries ExtraWorn, which the built list
  never does. When RemoveItem takes anything else, the pass adds the plain
  piece the server holds, the add announces itself ("looting boots",
  22 September), the tempered piece stays, and the difference comes back.

  So the units are picked here, out of the game's own lists as
  getExtraContainerChanges lists them, and removeInventoryItems takes those
  lists away by the game's own pointers. Units with no list of their own
  (the base container's, and those the changes count without a list) go
  last, for an entry that has no extra. Lists that are not worn go first, so
  of two equal pieces the one on the body stays.
*/
interface InventoryLists {
  lists: Entry[];
  plain: number;
  made?: Made;
}

const getInventoryLists = (refr: ObjectReference, baseId: number): InventoryLists => {
  const lists = new Array<Entry>();
  let plain = 0;
  // Thornswood #1649: a brewed bottle's contents belong to its form, not to
  // a list, so every unit of it carries them, listed or not, the way
  // getExtraContainerChangesAsInventory reads them.
  const made = madePotions().get(baseId);

  (getExtraContainerChanges(refr.getFormID()) || []).forEach((changesEntry) => {
    if (changesEntry.baseId !== baseId) {
      return;
    }
    plain += changesEntry.countDelta;
    // the native's ListsOf skips an empty one, and so does this
    (changesEntry.extendDataList || []).forEach((extraList) => {
      if (!extraList) {
        return;
      }
      const e: Entry = { baseId, count: 1 };
      extractExtraData(refr, extraList, e);
      if (made) {
        e.made = JSON.parse(JSON.stringify(made));
      }
      lists.push(e);
      plain -= e.count;
    });
  });

  getBaseContainerAsInventory(refr).entries.forEach((e) => {
    if (e.baseId === baseId) {
      plain += e.count;
    }
  });

  return { lists, plain: Math.max(0, plain), made };
};

const describeEntry = (e: Entry): string =>
  `${e.baseId.toString(16)} ${JSON.stringify(e)}`;

const takeAway = (refr: ObjectReference, entries: Entry[]): void => {
  const baseIds = new Set(entries.map((e) => e.baseId));
  baseIds.forEach((baseId) => {
    const { lists, plain, made } = getInventoryLists(refr, baseId);
    const left = lists.map((l) => l.count);
    let plainLeft = plain;
    let plainCount = 0;
    const picks = new Map<number, number>();

    const order = lists
      .map((l, i) => i)
      .sort((a, b) => Number(!!lists[a].worn || !!lists[a].wornLeft) - Number(!!lists[b].worn || !!lists[b].wornLeft));

    entries.filter((e) => e.baseId === baseId).forEach((e) => {
      let wanted = -e.count;
      // A unit with no list has no extra but the form's own contents (#1649).
      if (madeEqual(e.made, made) && !hasExtras({ ...e, made: undefined })) {
        const n = Math.min(wanted, plainLeft);
        plainCount += n;
        plainLeft -= n;
        wanted -= n;
      }
      order.forEach((i) => {
        if (wanted === 0 || left[i] === 0 || !inventoryEntriesEqual(lists[i], e, true)) {
          return;
        }
        const n = Math.min(wanted, left[i]);
        picks.set(i, (picks.get(i) || 0) + n);
        left[i] -= n;
        wanted -= n;
      });
      if (wanted > 0) {
        printConsole(
          `[thornswood] inventory: the game holds ${wanted} fewer than the ` +
          `difference takes away, of ${describeEntry(e)}`
        );
      }
    });

    const indexes = Array.from(picks.keys());
    const counts = indexes.map((i) => picks.get(i) as number);
    if (indexes.length === 0 && plainCount === 0) {
      return;
    }
    printConsole(
      `removeInventoryItems(${refr.getFormID().toString(16)}, ${baseId.toString(16)}, ` +
      `${lists.length}, ${JSON.stringify(indexes.map((i, k) => [i, counts[k], lists[i]]))}, ${plainCount})`
    );
    if (!removeInventoryItems(refr.getFormID(), baseId, lists.length, indexes, counts, plainCount)) {
      printConsole(
        `[thornswood] inventory: the game refused to take away ${baseId.toString(16)} ` +
        `(paused, or its lists changed since they were read)`
      );
    }
  });
};

export const applyInventory = (
  refr: ObjectReference,
  newInventory: Inventory,
  enableCrashProtection: boolean,
  ignoreWorn = false
): boolean => {
  resetInventoryBase(refr);
  // Thornswood #1649: every brewed bottle the server sends exists here, under
  // the server's id, before the pack is compared with it.
  newInventory.entries.forEach((e) => {
    if (e.made) {
      ensureMadePotion(e.baseId, e.made);
    }
  });
  const actual = getInventory(refr);
  let diff = getDiff(newInventory, actual, ignoreWorn).entries;

  let res = true;

  /*
    THORNSWOOD PATCH (Thornswood #1651). A rename is applied as a rename.

    When the server renames a piece (a Thornswood piece carries its condition
    on its name, so a hit, a repair or a chest's dull clock does), the diff
    holds the piece under its old name to take away and the same piece under
    its new name to add. Taking away and adding takes a worn piece off the
    body, because the server's pack does not say what is worn (the equipment
    record does, and this applies with ignoreWorn). So each such pair is
    handed to renameInventoryItems, which renames the units where they lie.
    When no set of the game's lists holds exactly those units, the pair stays
    in the diff and is applied below as any other difference. Two entries are
    a pair only when they match in everything but the name (what is worn
    too, unless ignoreWorn).
  */
  const renamed = new Set<Entry>();
  diff.forEach((added) => {
    if (added.count <= 0 || (enableCrashProtection && renamed.size > 0)) {
      return;
    }
    const removed = diff.find((x) =>
      x.count === -added.count && !renamed.has(x) && !namesEqual(x, added) &&
      inventoryEntriesEqualExceptName(x, added, ignoreWorn));
    if (!removed) {
      return;
    }
    const f = Game.getFormEx(added.baseId);
    if (!f) {
      return;
    }
    // The old name as the game spells it. When the server still holds some
    // of the piece, getDiff hands back the server's spelling ("loxsword" for
    // the game's "loxsword (Legendary)", none for the game's "Iron Sword"),
    // and the game renames by its own.
    const held = actual.entries.find((x) => inventoryEntriesEqual(x, removed, true));
    const from = (held || removed).name || "";
    const to = added.name ? cropName(added.name) : f.getName();
    if (renameInventoryItems(refr.getFormID(), added.baseId, from, to, added.count)) {
      renamed.add(added);
      renamed.add(removed);
    }
  });
  if (renamed.size > 0) {
    diff = diff.filter((e) => !renamed.has(e));
    if (enableCrashProtection) {
      return diff.length === 0;
    }
  }

  diff.sort((a, b) => (a.count < b.count ? -1 : 1));
  if (enableCrashProtection && diff.length > 1) {
    res = false;
    diff = diff.slice(0, 1);
  }

  takeAway(refr, diff.filter((e) => e.count < 0));

  diff.filter((e) => e.count > 0).forEach((e) => {
    let absCount = Math.abs(e.count);

    let queueNiNodeUpdateNeeded = false;

    const worn = !!e.worn;
    const wornLeft = !!e.wornLeft;

    let oneStepCount = e.count / absCount;

    const f = Game.getFormEx(e.baseId);
    if (!f) {
      return printConsole(`Bad form ID ${e.baseId.toString(16)}`);
    }
    const type = f.getType();
    // For misc items, potions and ingredients we don't want to split them into multiple items
    // This was made to fix a performance issue with users having 10000+ of misc items (i.e. gold)
    if (
      type === FormType.Misc ||
      type === FormType.Potion ||
      type === FormType.Ingredient
    ) {
      absCount = 1;
      oneStepCount = e.count;
    } else {
      if (absCount > 1000) {
        absCount = 1;
        oneStepCount = 1;
      }

      if (e.count > 1 && Ammo.from(Game.getFormEx(e.baseId))) {
        absCount = 1;
        oneStepCount = e.count;
        if (e.count > 60000) {
          // Why would actor have 60k arrows?
          e.count = 1;
        }
      }
    }

    for (let i = 0; i < absCount; ++i) {
      if (worn || wornLeft) {
        TESModPlatform.pushWornState(!!worn, !!wornLeft);
        queueNiNodeUpdateNeeded = true;
      }

      let addItemExArgs: [
        ObjectReference,
        Form,
        number,
        number,
        Enchantment | null,
        number,
        boolean,
        number,
        string,
        number,
        Potion | null,
        number,
      ];

      addItemExArgs = [
        refr,
        f,
        oneStepCount,
        e.health ? e.health : 1,
        e.enchantmentId
          ? Enchantment.from(Game.getFormEx(e.enchantmentId))
          : null,
        e.maxCharge ? e.maxCharge : 0,
        !!e.removeEnchantmentOnUnequip,
        e.chargePercent ? e.chargePercent : 0,
        e.name ? cropName(e.name) : f.getName(),
        e.soul ? e.soul : 0,
        e.poisonId ? Potion.from(Game.getFormEx(e.poisonId)) : null,
        e.poisonCount ? e.poisonCount : 0
      ];

      const argsToPrint = addItemExArgs.map((arg) => {
        if (arg instanceof ObjectReference) {
          return `ObjectReference(${arg.getFormID().toString(16)})`;
        } else if (arg instanceof Form) {
          return `Form(${arg.getFormID().toString(16)})`;
        } else {
          return JSON.stringify(arg);
        }
      });

      printConsole(
        `TESModPlatform.addItemEx(${argsToPrint.join(", ")})`
      );

      TESModPlatform.addItemEx(...addItemExArgs);
    }

    if (queueNiNodeUpdateNeeded) {
      const ac = Actor.from(refr);
      if (ac) {
        ac.queueNiNodeUpdate();
      }
    }
  });

  return res;
};
