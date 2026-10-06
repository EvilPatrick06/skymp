// test-load-order-check.js
//
//     node skymp5-client/test-load-order-check.js
//
// Thornswood #1268. getServerMods returns null when the server never answered;
// verifyLoadOrder then shows a plain "did not answer" notice, not LOAD ORDER
// WARNING/ERROR. A real mismatch still says LOAD ORDER ERROR.
//
// Thornswood #1715. The game numbers full plugins (Game.getModName) and light
// plugins (Game.getLightModName) apart, and the server's manifest lists them
// apart too ("mods" and "light"). Each list is compared with the server's on
// its own; a server whose manifest has no light list has none to compare.
//
// It runs the decision helpers in loadOrderCheck.ts themselves, transpiled with
// the client's own TypeScript (yarn install in skymp5-client first), then
// checks the TypeScript sources still name the same messages and lists.
'use strict';

const fs = require('fs');
const path = require('path');
const Module = require('module');

const ROOT = __dirname;
let pass = 0;
let fail = 0;

function yes(what, cond) {
  if (cond) {
    pass++;
    console.log('PASS  ' + what);
  } else {
    fail++;
    console.log('FAIL  ' + what);
  }
}

function load(file) {
  const ts = require(path.join(ROOT, 'node_modules', 'typescript'));
  const source = fs.readFileSync(file, 'utf8');
  const out = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2019 },
  }).outputText;
  const m = new Module(file);
  m.filename = file;
  m.paths = Module._nodeModulePaths(path.dirname(file));
  m._compile(out, file);
  return m.exports;
}

const { evaluateLoadOrder, screenNoticeForEval } = load(
  path.join(ROOT, 'src/services/services/loadOrderCheck.ts'),
);

const mod = (filename, size, crc32) => ({ filename, size, crc32 });
const lists = (mods, light) => ({ mods, light: light || [] });

// --- unreachable vs mismatch ---
const client = lists([mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 2)]);

const unreachable = evaluateLoadOrder(client, null);
yes('null server mods is unreachable', unreachable.kind === 'unreachable');
const unreachableNotice = screenNoticeForEval(unreachable, false);
yes(
  'unreachable notice has no LOAD ORDER wording',
  !!unreachableNotice &&
    unreachableNotice.text.indexOf('LOAD ORDER') === -1 &&
    unreachableNotice.text.indexOf('did not answer') !== -1 &&
    unreachableNotice.text.indexOf('could not be checked') !== -1,
);

const emptyList = evaluateLoadOrder(client, { mods: [] });
yes(
  'empty server list is tooManyClientMods, not unreachable',
  emptyList.kind === 'tooManyClientMods',
);
const emptyNotice = screenNoticeForEval(emptyList, false);
yes(
  'empty-list notice still says LOAD ORDER WARNING',
  !!emptyNotice && emptyNotice.text.indexOf('LOAD ORDER WARNING') === 0,
);

const mismatch = evaluateLoadOrder(
  lists([mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 9)]),
  { mods: [mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 2)] },
);
yes('crc mismatch is mismatch', mismatch.kind === 'mismatch' && mismatch.indices[0] === 1);
const mismatchNotice = screenNoticeForEval(mismatch, false);
yes(
  'mismatch notice says LOAD ORDER ERROR',
  !!mismatchNotice && mismatchNotice.text.indexOf('LOAD ORDER ERROR') === 0,
);

const ok = evaluateLoadOrder(lists([mod('A.esp', 10, 20)]), { mods: [mod('a.esp', 10, 20)] });
yes('matching mods (case-insensitive name) are ok', ok.kind === 'ok');
yes('ok has no screen notice', screenNoticeForEval(ok, false) === null);

const tooFew = evaluateLoadOrder(lists([mod('A.esp', 1, 1)]), {
  mods: [mod('A.esp', 1, 1), mod('B.esp', 2, 2)],
});
yes('fewer client mods is tooFewClientMods', tooFew.kind === 'tooFewClientMods');
yes(
  'tooFew uses LOAD ORDER ERROR text',
  screenNoticeForEval(tooFew, false).text.indexOf('LOAD ORDER ERROR') === 0,
);

// --- light plugins (#1715) ---
const full = [mod('Skyrim.esm', 1, 1), mod('Thornswood-Content.esp', 3, 3)];
const light = [mod('MoreCraftableEquipment.esp', 4, 4), mod('KhisartinBeards.esp', 5, 5)];

const bothOk = evaluateLoadOrder(lists(full, light), { mods: full, light: light });
yes('the same full and light lists are ok', bothOk.kind === 'ok');

const lightSwapped = evaluateLoadOrder(lists(full, [light[1], light[0]]), {
  mods: full,
  light: light,
});
yes(
  'light plugins in another order are a light list mismatch at both places',
  lightSwapped.kind === 'mismatch' &&
    lightSwapped.list === 'light' &&
    JSON.stringify(lightSwapped.indices) === '[0,1]',
);
yes(
  'a light list mismatch says LOAD ORDER ERROR',
  screenNoticeForEval(lightSwapped, false).text.indexOf('LOAD ORDER ERROR') === 0,
);

const lightMissing = evaluateLoadOrder(lists(full, [light[0]]), { mods: full, light: light });
yes(
  'a missing light plugin is tooFewClientMods on the light list',
  lightMissing.kind === 'tooFewClientMods' &&
    lightMissing.list === 'light' &&
    lightMissing.serverCount === 2 &&
    lightMissing.clientCount === 1,
);

const fullFirst = evaluateLoadOrder(lists([full[1], full[0]], [light[1]]), {
  mods: full,
  light: light,
});
yes(
  'a full list mismatch is reported before the light list is looked at',
  fullFirst.kind === 'mismatch' && fullFirst.list === 'full',
);

const oldServer = evaluateLoadOrder(lists(full, light), { mods: full });
yes('a server manifest with no light list compares the full list only', oldServer.kind === 'ok');

// --- TypeScript sources lock the same wording and lists ---
const checkTs = fs.readFileSync(path.join(ROOT, 'src/services/services/loadOrderCheck.ts'), 'utf8');
const settingsTs = fs.readFileSync(path.join(ROOT, 'src/services/services/settingsService.ts'), 'utf8');
const verifyTs = fs.readFileSync(
  path.join(ROOT, 'src/services/services/loadOrderVerificationService.ts'),
  'utf8',
);
const manifestTs = fs.readFileSync(path.join(ROOT, 'src/services/messages_http/serverManifest.ts'), 'utf8');

yes(
  'loadOrderCheck.ts names the unreachable notice',
  checkTs.indexOf('The server did not answer.') !== -1 &&
    checkTs.indexOf('Load order could not be checked.') !== -1 &&
    checkTs.indexOf('LOAD ORDER ERROR') !== -1,
);
yes(
  'getServerMods return type allows null',
  /getServerMods\(\):\s*Promise<ServerMods\s*\|\s*null>/.test(settingsTs),
);
yes(
  'getServerMods returns null after retries',
  /Unreachable after retries[\s\S]*return null;/.test(settingsTs),
);
yes(
  'getServerMods passes the manifest light list on',
  /light:\s*manifest\.light/.test(settingsTs) && /light\?:\s*Mod\[\]/.test(manifestTs),
);
yes(
  'verifyLoadOrder imports evaluateLoadOrder',
  verifyTs.indexOf('evaluateLoadOrder') !== -1 && verifyTs.indexOf("result.kind === 'unreachable'") !== -1,
);
yes(
  'verifyLoadOrder reads the light plugins from Game.getLightModName',
  /Game\.getLightModCount,\s*Game\.getLightModName/.test(verifyTs),
);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
