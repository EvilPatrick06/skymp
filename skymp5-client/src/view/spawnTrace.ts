import { Actor, Game, ObjectReference, printConsole, storage } from "skyrimPlatform";

/*
  THORNSWOOD (Thornswood #1287). WHERE A SPAWNING NPC IS, STEP BY STEP.

  Patrick saw guards and Stormcloaks pile up on people in central Whiterun.
  A neighbour is made by player.placeAtMe, so it is born on the person, and
  SpawnProcess moves it, enables it, resurrects it and puts it back, each
  step asynchronous (see the note at the top of spawnProcess.ts). Nothing
  recorded where the actor was between those steps, or whether this client,
  hosting it, sent that place to the server as the NPC's own. This writes
  one line per step, and one per hosted movement sent while a reference is
  still spawning, so a capture on dev can say which it is.

  It only reads. It adds, moves and removes no engine call, starts no timer,
  and nothing it does can throw into the spawn: every read is caught.

  The file is Data/Platform/thornswood-spawn.log, beside thornswood-front.log
  and never under Platform/Plugins, where SkyrimPlatform reloads every plugin
  whenever a file changes. It is emptied once per game start (kept across a
  plugin hot reload through storage) and stops at SPAWN_LOG_MAX_LINES with
  one line saying so.
*/

// The client is built with webpack's node target, so require is the
// runtime's own, as thornswood-front.js uses it for its log.
declare const require: (id: string) => any;

export const SPAWN_LOG_FILE = "Data/Platform/thornswood-spawn.log";
export const SPAWN_LOG_MAX_LINES = 20000;

const STARTED_KEY = "thornswoodSpawnLogStarted";
const LINES_KEY = "thornswoodSpawnLogLines";

type Point = [number, number, number];
type Value = string | number | boolean | undefined;

interface Spawning {
  remote: number;
  target: Point | undefined;
  method: string;
  startedAt: number;
}

// Local reference id to what is known about its spawn, from chosen (or
// start) until settled or gone.
const spawning = new Map<number, Spawning>();

let fsm: any = undefined;

function getFs(): any {
  if (fsm !== undefined) return fsm;
  try {
    fsm = typeof require === "function" ? require("fs") : null;
  } catch (e) {
    fsm = null;
  }
  if (!fsm) {
    try { printConsole("[spawn] no file system here, so thornswood-spawn.log is not written"); } catch (e) { }
  }
  return fsm;
}

function two(n: number): string { return (n < 10 ? "0" : "") + n; }

export function spawnClock(d: Date): string {
  return two(d.getHours()) + ":" + two(d.getMinutes()) + ":" + two(d.getSeconds()) + "." +
    ("00" + d.getMilliseconds()).slice(-3);
}

export function spawnHex(n: number | undefined): string {
  return typeof n === "number" && isFinite(n) ? n.toString(16) : "-";
}

export function spawnPoint(p: Point | undefined): string {
  if (!p || p.length < 3) return "-";
  return Math.round(p[0]) + "," + Math.round(p[1]) + "," + Math.round(p[2]);
}

export function spawnDistance(a: Point | undefined, b: Point | undefined): number | undefined {
  if (!a || !b) return undefined;
  const x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  const d = Math.sqrt(x * x + y * y + z * z);
  return isFinite(d) ? Math.round(d) : undefined;
}

/* One line: the tag, the clock, the stage, then key=value for every field
   that has a value, in the order given. A boolean is 1 or 0. */
export function spawnTraceLine(at: Date, stage: string, fields: { [key: string]: Value }): string {
  const parts = ["[spawn]", spawnClock(at), stage];
  Object.keys(fields).forEach((k) => {
    const v = fields[k];
    if (v === undefined) return;
    parts.push(k + "=" + (typeof v === "boolean" ? (v ? "1" : "0") : String(v)));
  });
  return parts.join(" ");
}

export function spawnTraceWrite(line: string): void {
  try {
    const fs = getFs();
    if (!fs) return;
    if (storage[STARTED_KEY] !== true) {
      fs.writeFileSync(SPAWN_LOG_FILE, "");
      storage[STARTED_KEY] = true;
      storage[LINES_KEY] = 0;
    }
    const written = typeof storage[LINES_KEY] === "number" ? (storage[LINES_KEY] as number) : 0;
    if (written > SPAWN_LOG_MAX_LINES) return;
    if (written === SPAWN_LOG_MAX_LINES) {
      fs.appendFileSync(SPAWN_LOG_FILE, "[spawn] " + spawnClock(new Date()) +
        " cap reached at " + SPAWN_LOG_MAX_LINES + " lines; nothing more is written this game\n");
      storage[LINES_KEY] = written + 1;
      return;
    }
    fs.appendFileSync(SPAWN_LOG_FILE, line + "\n");
    storage[LINES_KEY] = written + 1;
  } catch (e) {
    // A full disk or a locked file loses lines, never a spawn.
  }
}

/* Whether this client hosts the remote id. The server sends a host start
   with 0x100000000 added for ids below 0xff000000 (OnHostAttempt), and the
   view may know either form, so both are asked, as formView.ts does. */
export function spawnHostedHere(remote: number): boolean {
  try {
    const hosted = storage["hosted"];
    if (!Array.isArray(hosted) || !remote) return false;
    const other = remote >= 0x100000000 ? remote - 0x100000000 : remote + 0x100000000;
    return hosted.indexOf(remote) !== -1 || hosted.indexOf(other) !== -1;
  } catch (e) {
    return false;
  }
}

export function spawnIsSpawning(localId: number): boolean {
  return spawning.has(localId);
}

function posOf(refr: ObjectReference | null | undefined): Point | undefined {
  if (!refr) return undefined;
  try {
    return [refr.getPositionX(), refr.getPositionY(), refr.getPositionZ()];
  } catch (e) {
    return undefined;
  }
}

function playerPos(): Point | undefined {
  try {
    return posOf(Game.getPlayer() as unknown as ObjectReference);
  } catch (e) {
    return undefined;
  }
}

function read<T>(f: () => T): T | undefined {
  try { return f(); } catch (e) { return undefined; }
}

/* What the engine says of the reference now, for a stage line. */
function refState(localId: number, entry: Spawning | undefined, now: number): { [key: string]: Value } {
  const refr = read(() => ObjectReference.from(Game.getFormEx(localId)));
  const pos = posOf(refr);
  const player = playerPos();
  const actor = refr ? read(() => Actor.from(refr)) : undefined;
  return {
    remote: entry ? spawnHex(entry.remote) : undefined,
    local: spawnHex(localId),
    method: entry ? entry.method : undefined,
    target: entry ? spawnPoint(entry.target) : undefined,
    pos: spawnPoint(pos),
    dTarget: entry ? spawnDistance(pos, entry.target) : undefined,
    dPlayer: spawnDistance(pos, player),
    disabled: refr ? read(() => refr.isDisabled()) : undefined,
    dead: actor ? read(() => actor.isDead()) : undefined,
    loaded3d: refr ? read(() => refr.is3DLoaded()) : undefined,
    hosted: entry ? spawnHostedHere(entry.remote) : undefined,
    ms: entry ? now - entry.startedAt : undefined,
  };
}

/* formView.ts, the spawn branch, once the method is picked. */
export function traceSpawnChosen(info: {
  remote: number; local: number; base: number; method: string; reason: string;
  target: Point | undefined; hostedByOther: boolean; isDead: boolean;
}): void {
  try {
    const now = Date.now();
    spawning.set(info.local, { remote: info.remote, target: info.target, method: info.method, startedAt: now });
    const player = playerPos();
    spawnTraceWrite(spawnTraceLine(new Date(now), "chosen", {
      remote: spawnHex(info.remote),
      local: spawnHex(info.local),
      base: spawnHex(info.base),
      method: info.method,
      reason: info.reason,
      target: spawnPoint(info.target),
      player: spawnPoint(player),
      dPlayerTarget: spawnDistance(info.target, player),
      hostedByOther: info.hostedByOther,
      hosted: spawnHostedHere(info.remote),
      isDead: info.isDead,
    }));
  } catch (e) { }
}

/* SpawnProcess's constructor. A spawn that formView did not announce
   (another caller) still gets an entry, with what SpawnProcess knows. */
export function traceSpawnStart(localId: number, target: Point): void {
  try {
    const now = Date.now();
    const entry = spawning.get(localId);
    if (entry) {
      entry.target = target;
    } else {
      spawning.set(localId, { remote: 0, target: target, method: "-", startedAt: now });
    }
    traceSpawnStage("start", localId);
  } catch (e) { }
}

/* A step of SpawnProcess has resolved. */
export function traceSpawnStage(stage: string, localId: number): void {
  try {
    const now = Date.now();
    spawnTraceWrite(spawnTraceLine(new Date(now), stage, refState(localId, spawning.get(localId), now)));
  } catch (e) { }
}

/* The last line of a spawn: settled, or gone at the step it stopped. */
export function traceSpawnEnd(stage: string, localId: number): void {
  try {
    traceSpawnStage(stage, localId);
  } catch (e) { }
  spawning.delete(localId);
}

/* sendInputsService.sendMovement: a hosted reference's movement going to
   the server while that reference is still spawning. Nothing otherwise. */
export function traceHostedSend(localId: number, remote: number, sentPos: Point | undefined): void {
  try {
    const entry = spawning.get(localId);
    if (!entry) return;
    const now = Date.now();
    const player = playerPos();
    spawnTraceWrite(spawnTraceLine(new Date(now), "hosted-send", {
      remote: spawnHex(remote),
      local: spawnHex(localId),
      method: entry.method,
      target: spawnPoint(entry.target),
      sent: spawnPoint(sentPos),
      dTarget: spawnDistance(sentPos, entry.target),
      dPlayer: spawnDistance(sentPos, player),
      ms: now - entry.startedAt,
    }));
  } catch (e) { }
}

/* Where a RespawnNeededError is caught and the local actor is taken away. */
export function traceRespawnNeeded(where: string, remote: number | undefined, localId: number | undefined): void {
  try {
    const refr = typeof localId === "number" ? read(() => ObjectReference.from(Game.getFormEx(localId))) : undefined;
    const pos = posOf(refr);
    spawnTraceWrite(spawnTraceLine(new Date(), "respawn-needed", {
      where: where,
      remote: spawnHex(remote),
      local: spawnHex(localId),
      pos: spawnPoint(pos),
      dPlayer: spawnDistance(pos, playerPos()),
    }));
  } catch (e) { }
}
