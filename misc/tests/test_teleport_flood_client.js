// Thornswood #1932. Replays the teleports the client received on dev on
// 5 Oct 2026 through the client code that handles them
// (RemoteServer.onTeleportMessage, RagdollService and
// SendInputsService.sendMovement, copied out of skymp5-client/src and
// compiled with TypeScript) against stand-ins for the game.
//
// Measured in skyrim-platform.log: 233 "Teleporting id 2 refrId 14" to one
// spot between 11:51:20.001 and 11:51:20.535, and nothing after 20.890.
// Each of them started a ragdoll removal (a latent Papyrus call) and, when
// that returned, a MoveTo of the own character.
//
// Usage: node test_teleport_flood_client.js <engine root> <typescript dir> [revision]
// With a revision the sources are read from git at that revision instead of
// the working tree (the "before" run of the regression).
// Exit code 0 when the counts are the ones the fix promises, 1 otherwise.
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const [root, tsDir, rev] = process.argv.slice(2);
if (!root || !tsDir) {
  console.error('usage: node test_teleport_flood_client.js <engine root> <typescript dir> [revision]');
  process.exit(2);
}
const ts = require(tsDir);
const read = (name) => {
  const rel = `skymp5-client/src/services/services/${name}`;
  const text = rev
    ? execFileSync('git', ['-C', root, 'show', `${rev}:${rel}`], { maxBuffer: 1 << 26 }).toString('utf8')
    : fs.readFileSync(path.join(root, rel), 'utf8');
  return text.replace(/\r\n/g, '\n');
};
console.log(`client code from ${root}${rev ? ' at ' + rev : ''}`);

function blockAt(source, signature, file) {
  const at = source.indexOf(signature);
  if (at < 0) return null;
  const start = source.lastIndexOf('\n', at) + 1;
  const open = source.indexOf('{', at);
  let depth = 1;
  let end = open + 1;
  while (depth > 0 && end < source.length) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
  }
  if (depth) throw new Error(`unterminated ${signature} in ${file}`);
  // an arrow-function property ends with "};"
  if (source[end] === ';') end++;
  return source.slice(start, end) + '\n';
}
const need = (x, what) => { if (x === null) throw new Error(`missing ${what}`); return x; };
const lineWith = (source, text) => {
  const at = source.indexOf(text);
  if (at < 0) return '';
  return source.slice(source.lastIndexOf('\n', at) + 1, source.indexOf('\n', at) + 1);
};
const compile = (code) => ts.transpileModule(code, {
  compilerOptions: { target: ts.ScriptTarget.ES2019, module: ts.ModuleKind.None, strict: false },
}).outputText;

const remote = read('remoteServer.ts');
const remoteClass =
  lineWith(remote, 'const carriedOutTeleportSeqKey') +
  'class RemoteServerUnderTest extends RemoteServerBase {\n' +
  lineWith(remote, 'private teleportsOnTheirWay') +
  need(blockAt(remote, '  private onTeleportMessage(', 'remoteServer.ts'), 'onTeleportMessage') +
  need(blockAt(remote, '  private handleConnectionAccepted(', 'remoteServer.ts'), 'handleConnectionAccepted') +
  (blockAt(remote, '  getCarriedOutTeleportSeq(', 'remoteServer.ts') || '') +
  '}\nreturn RemoteServerUnderTest;\n';

const ragdoll = read('ragdollService.ts');
const ragdollStart = ragdoll.indexOf('export class RagdollService');
if (ragdollStart < 0) throw new Error('missing RagdollService');
const ragdollClass = ragdoll.slice(ragdollStart).replace('export class', 'class') + '\nreturn RagdollService;\n';

const inputs = read('sendInputsService.ts');
const inputsClass =
  'class SendInputsUnderTest extends SendInputsBase {\n' +
  need(blockAt(inputs, '    private sendMovement(', 'sendInputsService.ts'), 'sendMovement') +
  '}\nreturn SendInputsUnderTest;\n';

// ---- stand-ins for the game ----
let frameQueue = [];
const once = (event, f) => { if (event !== 'update') throw new Error(event); frameQueue.push(f); };
const frame = () => { const run = frameQueue; frameQueue = []; run.forEach((f) => f()); };
const flushPromises = async () => { for (let i = 0; i < 5; i++) await Promise.resolve(); };

const count = { teleportingLines: 0, latentCalls: 0, moves: 0, damageMultWrites: 0 };
let pendingLatent = [];
let lastMove = null;
const character = {
  getFormID: () => 0x14,
  forceRemoveRagdollFromWorld: () => {
    count.latentCalls++;
    return new Promise((resolve) => pendingLatent.push(resolve));
  },
};
const resolveLatent = async (n) => {
  const now = pendingLatent.splice(0, n === undefined ? pendingLatent.length : n);
  now.forEach((r) => r());
  await flushPromises();
};
const Game = {
  getPlayer: () => character,
  getFormEx: (id) => (id === 0x14 ? character : { formId: id }),
  setGameSettingFloat: () => { count.damageMultWrites++; },
};
const stubs = {
  once, Game, storage: {},
  getObjectReference: () => null,
  logTrace: (_self, ...args) => { if (String(args[0]).startsWith('Teleporting')) count.teleportingLines++; },
  logError: () => {},
  TESModPlatform: {
    moveRefrToPosition: (refr, cell, world, x, y, z) => { count.moves++; lastMove = { refr, x, y, z }; },
  },
  ObjectReference: { from: (x) => x },
  Cell: { from: (x) => x },
  WorldSpace: { from: (x) => x },
  Actor: { from: (x) => (x && x.forceRemoveRagdollFromWorld ? x : null) },
};
const controller = {
  once,
  lookupListener: (cls) => (cls === stubs.RagdollService ? ragdollService : remoteServer),
  emitter: { emit: (name, e) => { if (name === 'sendMessageWithRefrId') sentMovement.push(e.message); } },
};
class ClientListener {}
stubs.ClientListener = ClientListener;
stubs.RagdollService = new Function(...Object.keys(stubs), compile(ragdollClass))(...Object.values(stubs));
class RemoteServerBase {
  constructor() { this.controller = controller; this.worldModel = {}; }
  getIdManager() { return { getId: (idx) => idx }; }
  getMyActorIndex() { return 7; }
  cancelOwnerSpawn() {}
}
stubs.RemoteServerBase = RemoteServerBase;
const RemoteServerUnderTest = new Function(...Object.keys(stubs), compile(remoteClass))(...Object.values(stubs));
const ragdollService = new stubs.RagdollService({ Game }, controller);
const remoteServer = new RemoteServerUnderTest();
if (!remoteServer.teleportsOnTheirWay) remoteServer.teleportsOnTheirWay = new Map();
frame(); // RagdollService's first update writes the settings once
count.damageMultWrites = 0;

const sentMovement = [];
class SendInputsBase {
  constructor() { this.controller = controller; this.lastSendMovementMoment = new Map(); }
  getInputOwner(refrId) { return refrId ? { getFormID: () => refrId } : character; }
}
const inputStubs = {
  SendInputsBase,
  MsgType: { UpdateMovement: 7 },
  getMovement: () => ({ worldOrCell: 0x1a26f, pos: [0, 0, 0], rot: [0, 0, 0] }),
  RemoteServer: {},
};
const SendInputsUnderTest = new Function(...Object.keys(inputStubs), compile(inputsClass))(...Object.values(inputStubs));
const sendInputs = new SendInputsUnderTest();

// ---- the replay ----
let failures = 0;
const expect = (what, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  console.log(`${what.padEnd(78)} ${String(JSON.stringify(got)).padStart(6)} (want ${JSON.stringify(want)})${ok ? '' : '  FAIL'}`);
  if (!ok) failures++;
};
const spot = [25669.69921875, -7632.927734375, -3237.4365234375];
const teleport2 = (seq, x) => ({ t: 31, pos: [x, spot[1], spot[2]], rot: [0, 0, 0], worldOrCell: 0x1a26f, teleportSeq: seq });

(async () => {
  // The door's TeleportMessage (it names the own character's idx) and the
  // 232 TeleportMessage2 behind it, handled over several frames before any
  // ragdoll removal has returned.
  for (let i = 0; i < 233; i++) {
    const message = teleport2(1000 + i, spot[0] + (i === 232 ? 1 : 0));
    if (i === 0) message.idx = 7; // getMyActorIndex() of the stand-in
    remoteServer.onTeleportMessage({ message });
    if (i % 6 === 5) frame();
  }
  frame();
  expect('"Teleporting" lines logged (one per message received)', count.teleportingLines, 233);
  expect('ragdoll removals started (latent Papyrus calls)', count.latentCalls, 1);
  // The VM finishes the removals; the moves run on the next update.
  await resolveLatent();
  frame();
  expect('moves of the own character', count.moves, 1);
  expect('the move goes to the newest destination', lastMove && lastMove.x, spot[0] + 1);
  expect('game setting writes (6 per removal start, 6 per finish)', count.damageMultWrites, 12);
  expect('teleport number echoed in the next movement packet',
    remoteServer.getCarriedOutTeleportSeq ? remoteServer.getCarriedOutTeleportSeq() : null, 1232);

  // The movement packet carries the number for the client's own character
  // only.
  sendInputs.sendMovement(undefined, undefined);
  sendInputs.sendMovement(0xff000abc, undefined);
  expect('teleportSeq in the own character\'s movement packet',
    sentMovement[0] && sentMovement[0].data.teleportSeq, 1232);
  expect('teleportSeq in a hosted actor\'s movement packet (none)',
    sentMovement[1] ? sentMovement[1].data.teleportSeq === undefined : false, true);

  // A teleport after that move starts its own removal and move.
  remoteServer.onTeleportMessage({ message: teleport2(1300, spot[0] + 50) });
  frame();
  await resolveLatent();
  frame();
  expect('removals and moves after one more teleport', [count.latentCalls, count.moves], [2, 2]);

  // A connection change while a move is on its way: the old connection's
  // move is dropped and the echo starts again at 0.
  remoteServer.onTeleportMessage({ message: teleport2(1400, spot[0] + 99) });
  frame();
  remoteServer.handleConnectionAccepted();
  await resolveLatent();
  frame();
  expect('moves after a connection change in the middle of one', count.moves, 2);
  expect('echo after the connection change',
    remoteServer.getCarriedOutTeleportSeq ? remoteServer.getCarriedOutTeleportSeq() : null, 0);

  console.log(failures ? 'FAIL' : 'PASS');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
