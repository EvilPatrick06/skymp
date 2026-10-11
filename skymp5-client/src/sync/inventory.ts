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
} from "skyrimPlatform";

export interface Extra {
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
const isTempered = (e: Entry): boolean => (e.health ?? 1) > 1;

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

const extrasEqual = (a: Entry, b: Entry, ignoreWorn = false) => {
  return (
    a.health === b.health &&
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
        out.health = Math.round((extra as ExtraHealth).health * 10) / 10;

        // TESModPlatform::AddItemEx makes all items at least 1.01 health
        if (out.health === 1) {
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

export const applyInventory = (
  refr: ObjectReference,
  newInventory: Inventory,
  enableCrashProtection: boolean,
  ignoreWorn = false
): boolean => {
  resetInventoryBase(refr);
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
      printConsole(`renameInventoryItems(${refr.getFormID().toString(16)}, ${added.baseId.toString(16)}, ${JSON.stringify(from)}, ${JSON.stringify(to)}, ${added.count})`);
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
  diff.forEach((e, i) => {
    if (i > 0 && enableCrashProtection) {
      res = false;
      return;
    }
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

        // Also for arrows with strange count
        if (worn && e.count < 0) {
          absCount = 0;
        }
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
