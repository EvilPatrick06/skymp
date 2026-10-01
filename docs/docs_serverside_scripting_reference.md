# Serverside Scripting Reference

Server declares `mp` global variable that gives access to the whole API.
This page contains the list of `mp` object methods available to use in scripts.

## mp.makeProperty()

Creates a new property that would be attached to all instances of `MpActor` and `MpObjectReference`. Values are saved to database automatically. See [Properties System](docs_properties_system.md) for more information.

```typescript
/* Definition */
interface MakePropertyOptions {
  // If set to false, `updateOwner` would never be invoked
  // Player's client wouldn't see it's own value of this property
  // Reasonable for passwords and other secret values
  isVisibleByOwner: boolean;

  // If set to false, `updateNeighbor` would never be invoked
  // Player's client wouldn't see values of neighbor Actors/ObjectReferences
  isVisibleByNeighbors: boolean;

  // Body of functions that would be invoked on client every update.
  updateOwner: string; // For the PlayerCharacter
  updateNeighbor: string; // For each synchronized Actor/ObjectReference
}

interface Mp {
  // ...
  makeProperty(propertyName: string, options: MakePropertyOptions): void;
  // ...
}

/* Usage */
mp.makeProperty("playerLevel", {
    isVisibleByOwner: true,
    isVisibleByNeighbors: false,
    updateOwner: "ctx.sp.Game.setPlayerLevel(ctx.value)"
    updateNeighbor: ""
});
```

## mp.makeEventSource()

Creates a new event source allowing you to catch specific game situations and pass them to a server as events. See [Events System](docs_events_system.md) for more information.

```typescript
/* Definition */
interface Mp {
  // ...
  makeEventSource(eventName: string, functionBody: string): void;
  // ...
}

/* Usage */
mp.makeEventSource("_onLocalDeath", `
    ctx.sp.on("update", () => {
      const pl = ctx.sp.Game.getPlayer();
      const isDead = pl.getActorValuePercentage("health") === 0;
      if (ctx.state.wasDead !== isDead) {
        if (isDead) {
          ctx.sendEvent();
        }
        ctx.state.wasDead = isDead;
      }
    });
  `);
);
mp._onLocalDeath = function(pcFormId) { /* ... */ };
```

## mp.get()

Returns the actual value of a specified property. If there is no value, then `undefined` returned.

```typescript
/* Definition */
interface Mp {
  // ...
  get(formId: number, propertyName: string): void;
  // ...
}

/* Usage */
mp.get(0xff000000, "type");
mp.get(0xff000000, "pos");
mp.get(0xff000000, "myAwesomeProperty");
```

## mp.set()

Changes value of the specified property.

```typescript
/* Definition */
interface Mp {
  // ...
  set(formId: number, propertyName: string, newValue: any): void;
  // ...
}

/* Usage */
mp.set(0xff000000, "pos", [0, 0, 0]);
```

## mp.loadNpcBatch()

Loads a bounded batch of existing placed NPC references without a connected
human. It does not place new actors, change the NPC inclusion policy, or run
AI. Saved changes are applied through the normal reference-loading path.

```typescript
loadNpcBatch(cursor: number, limit: number): {
  nextCursor: number;
  total: number;
  actorIds: number[];
};
```

Start at cursor `0`. The cursor counts load-order-winning ACHR placements in
ascending global-ID order, including candidates refused by the current policy
and the vanilla human reference. `actorIds` contains only successfully loaded
NPC identities; the human reference is excluded. Continue at `nextCursor`
until it equals `total`. Repeating a batch keeps loaded identities and their
current transforms. The index is rebuilt if a different ESPM loader is attached.

Both arguments must be integers. The limit is from 1 to 128; a cursor past
`total` is rejected, while a cursor equal to `total` returns an empty batch.
Recursive discovery of neighbouring chunks is suspended during the call, so
loading one NPC cannot trigger a whole cell load. Existing subscriptions still
update, and normal streaming resumes after the call.

Call `mp.prepareNpcLoad(): number` before accepting connections to build the
placement index without instantiating references. It returns candidate count.
Subsequent calls bound candidate count rather than
elapsed time: use small batches, yield between them, and measure the actual
load order. This method supplies loading infrastructure, not movement,
navigation, combat or unattended routines.

## NPC server authority and navigation

`mp.setNpcServerControlled(formId: number, controlled: boolean): void` marks
an existing NPC as server controlled and revokes its human host. Human actors
cannot acquire this flag. The flag persists in the save, stays private, and
prevents connected clients from submitting hosted movement or actions for it.
Observers receive its complete current movement snapshot when subscribing.

`mp.updateNpcMovement(formId: number, pos: number[], angle: number[], speed:
number): void` updates a server-controlled living, enabled NPC and streams the
standard movement message. Coordinates must be finite, speed must be 0 to 300,
and a single move cannot exceed 4096 units. The caller must supply navigation,
activity and combat rules; acquiring authority alone supplies none of them.

`mp.getNavmeshRecords(cellOrWorldId: number): number[]` returns winning,
non-deleted NAVM identities belonging to that cell or world. Read each record's
NVNM field with `lookupEspmRecordById`. It does not supply pathfinding or alter
the older FindNavMeshes API.

## mp.clear()

Clears added properties and event sources.

```typescript
// Definition
clear(): void;
```

```typescript
// Usage
mp.clear();
```
