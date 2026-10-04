const assert = require("node:assert");

// Thornswood #1560. The server writes what an actor no client owns is
// wearing. A new character is dressed this way before it is handed to the
// person who made it, so it arrives wearing its clothes and nothing has to
// keep asking its game to put them on.

const main = async () => {
  const tunic = 0x1be1a;    // Roughspun Tunic, Skyrim.esm
  const footwraps = 0x1be1b; // Footwraps, Skyrim.esm
  const ironSword = 0x12eb7;

  const actorId = mp.createActor(0, [0, 0, 0], 0, 0x0000003c);
  mp.set(actorId, "inventory", {
    entries: [
      { baseId: 0xf, count: 40 },
      { baseId: tunic, count: 1 },
      { baseId: footwraps, count: 1 },
    ],
  });

  const before = mp.get(actorId, "equipment");
  assert.deepEqual(before.inv.entries, []);

  mp.set(actorId, "equipment", {
    inv: {
      entries: [
        { baseId: tunic, count: 1, worn: true },
        { baseId: footwraps, count: 1, worn: true },
      ],
    },
    numChanges: 1000,
  });

  const dressed = mp.get(actorId, "equipment");
  assert.deepEqual(
    dressed.inv.entries.map((e) => [e.baseId, e.count, e.worn]),
    [[tunic, 1, true], [footwraps, 1, true]]
  );
  // Counted by the record, not taken from the caller.
  assert.strictEqual(dressed.numChanges, before.numChanges + 1);
  // The pack is what it was: dressing hands nothing over and takes nothing.
  assert.deepEqual(mp.get(actorId, "inventory").entries.length, 3);

  // Refused, and the record is left as it was: an item the actor does not
  // have, and an entry that is not worn.
  assert.throws(
    () => mp.set(actorId, "equipment", {
      inv: { entries: [{ baseId: ironSword, count: 1, worn: true }] },
    }),
    /not in its inventory/
  );
  assert.throws(
    () => mp.set(actorId, "equipment", {
      inv: { entries: [{ baseId: tunic, count: 1 }] },
    }),
    /not marked worn/
  );
  assert.throws(() => mp.set(actorId, "equipment", {}), /inv: \{ entries/);
  assert.deepEqual(mp.get(actorId, "equipment"), dressed);

  // An empty record takes everything off.
  mp.set(actorId, "equipment", { inv: { entries: [] } });
  assert.deepEqual(mp.get(actorId, "equipment").inv.entries, []);
  assert.strictEqual(mp.get(actorId, "equipment").numChanges, dressed.numChanges + 1);
};

main().then(() => {
  console.log("Test passed!");
  process.exit(0);
}).catch((err) => {
  console.log("Test failed!");
  console.error(err);
  process.exit(1);
});
