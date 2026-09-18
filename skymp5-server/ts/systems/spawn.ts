import { Settings } from "../settings";
import { System, Log, SystemContext } from "./system";

type Mp = any; // TODO

function randomInteger(min: number, max: number) {
  const rand = min + Math.random() * (max + 1 - min);
  return Math.floor(rand);
}

export class Spawn implements System {
  systemName = "Spawn";
  constructor(private log: Log) { }

  async initAsync(ctx: SystemContext): Promise<void> {
    const settingsObject = await Settings.get();
    const listenerFn = (userId: number, userProfileId: number, discordRoleIds: string[], discordId?: string) => {
      const { startPoints } = settingsObject;
      let actorId = ctx.svr.getActorsByProfileId(userProfileId)[0];
      if (actorId) {
        this.log("Loading character", actorId.toString(16));
        ctx.svr.setEnabled(actorId, true);
        ctx.svr.setUserActor(userId, actorId);
        // Upstream left a TODO here. Someone who disconnected during character
        // creation comes back with an actor but no appearance, and an actor
        // without an appearance is invisible, so they would be dropped into
        // the world as nothing at all with no way to fix it. The server
        // already treats an empty appearance this way in
        // MpActor::ApplyChangeForm; this just applies the same rule on the
        // path where the actor already existed.
        const appearance = (ctx.svr as unknown as Mp).get(actorId, "appearance");
        if (!appearance) {
          this.log("Character", actorId.toString(16), "has no appearance, reopening the race menu");
          ctx.svr.setRaceMenuOpen(actorId, true);
        }
      } else {
        const idx = randomInteger(0, startPoints.length - 1);
        actorId = ctx.svr.createActor(
          0,
          startPoints[idx].pos,
          startPoints[idx].angleZ,
          +startPoints[idx].worldOrCell,
          userProfileId
        );
        this.log("Creating character", actorId.toString(16));
        ctx.svr.setUserActor(userId, actorId);
        ctx.svr.setRaceMenuOpen(actorId, true);
      }

      const mp = ctx.svr as unknown as Mp;
      mp.set(actorId, "private.discordRoles", discordRoleIds);

      if (discordId !== undefined) {
        // This helps us to test if indexes registration works in LoadForm or not
        if (mp.get(actorId, "private.indexed.discordId") !== discordId) {
          mp.set(actorId, "private.indexed.discordId", discordId);
        }

        const forms = mp.findFormsByPropertyValue("private.indexed.discordId", discordId) as number[];
        console.log(`Found forms ${forms}`);
      }
    };
    ctx.gm.on("spawnAllowed", listenerFn);
    (ctx.svr as any)._onSpawnAllowed = listenerFn;
  }

  disconnect(userId: number, ctx: SystemContext): void {
    const actorId = ctx.svr.getUserActor(userId);
    if (actorId !== 0) {
      ctx.svr.setEnabled(actorId, false);
    }
  }
}
