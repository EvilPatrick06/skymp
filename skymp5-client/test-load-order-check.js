// test-load-order-check.js
//
//     node skymp5-client/test-load-order-check.js
//
// Thornswood #1268. getServerMods returns null when the server never answered;
// verifyLoadOrder then shows a plain "did not answer" notice, not LOAD ORDER
// WARNING/ERROR. A real mismatch still says LOAD ORDER ERROR.
//
// The decision helpers live in loadOrderCheck.ts. This file carries the same
// rules in plain JavaScript so it runs with node alone, then checks the TypeScript
// sources still name the same messages.
'use strict';

const fs = require('fs');
const path = require('path');

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

function noticeServerModsUnreachable() {
  return {
    text:
      'The server did not answer.\nLoad order could not be checked.\nCheck console for details.',
    color: [255, 255, 0, 1],
    clearDelay: 5,
  };
}

function noticeTooManyClientMods() {
  return {
    text:
      'LOAD ORDER WARNING: you have more mods than server!\nCheck console for details.',
    color: [255, 255, 0, 1],
    clearDelay: 5,
  };
}

function noticeLoadOrderMismatch(ignoreMismatch) {
  if (ignoreMismatch) {
    return {
      text:
        'LOAD ORDER ERROR!\nHowever, ignoring it because of ignoreLoadOrderMismatch being set.' +
        '\nExpect EVERYTHING BREAK, unless you know what you are doing.\nCheck console for details.' +
        '\nThis message will disappear after 30 seconds.',
      color: [255, 0, 0, 1],
      clearDelay: 30,
    };
  }
  return {
    text: 'LOAD ORDER ERROR!\nCheck console for details.',
    color: [255, 0, 0, 1],
  };
}

function evaluateLoadOrder(clientMods, serverMods) {
  if (serverMods === null) {
    return { kind: 'unreachable' };
  }
  if (clientMods.length < serverMods.length) {
    return {
      kind: 'tooFewClientMods',
      serverCount: serverMods.length,
      clientCount: clientMods.length,
    };
  }
  if (clientMods.length > serverMods.length) {
    return { kind: 'tooManyClientMods' };
  }
  const indices = [];
  for (let i = 0; i < serverMods.length; ++i) {
    if (
      clientMods[i].filename.toLowerCase() !==
        serverMods[i].filename.toLowerCase() ||
      clientMods[i].size !== serverMods[i].size ||
      clientMods[i].crc32 !== serverMods[i].crc32
    ) {
      indices.push(i);
    }
  }
  if (indices.length !== 0) {
    return { kind: 'mismatch', indices };
  }
  return { kind: 'ok' };
}

function screenNoticeForEval(result, ignoreMismatch) {
  switch (result.kind) {
    case 'unreachable':
      return noticeServerModsUnreachable();
    case 'tooManyClientMods':
      return noticeTooManyClientMods();
    case 'tooFewClientMods':
    case 'mismatch':
      return noticeLoadOrderMismatch(ignoreMismatch);
    case 'ok':
      return null;
  }
}

const mod = (filename, size, crc32) => ({ filename, size, crc32 });

// --- unreachable vs mismatch ---
const client = [mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 2)];

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

const emptyList = evaluateLoadOrder(client, []);
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
  [mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 9)],
  [mod('Skyrim.esm', 1, 1), mod('Update.esm', 2, 2)],
);
yes('crc mismatch is mismatch', mismatch.kind === 'mismatch' && mismatch.indices[0] === 1);
const mismatchNotice = screenNoticeForEval(mismatch, false);
yes(
  'mismatch notice says LOAD ORDER ERROR',
  !!mismatchNotice && mismatchNotice.text.indexOf('LOAD ORDER ERROR') === 0,
);

const ok = evaluateLoadOrder(
  [mod('A.esp', 10, 20)],
  [mod('a.esp', 10, 20)],
);
yes('matching mods (case-insensitive name) are ok', ok.kind === 'ok');
yes('ok has no screen notice', screenNoticeForEval(ok, false) === null);

const tooFew = evaluateLoadOrder([mod('A.esp', 1, 1)], [mod('A.esp', 1, 1), mod('B.esp', 2, 2)]);
yes('fewer client mods is tooFewClientMods', tooFew.kind === 'tooFewClientMods');
yes(
  'tooFew uses LOAD ORDER ERROR text',
  screenNoticeForEval(tooFew, false).text.indexOf('LOAD ORDER ERROR') === 0,
);

// --- TypeScript sources lock the same wording ---
const checkTs = fs.readFileSync(
  path.join(ROOT, 'src/services/services/loadOrderCheck.ts'),
  'utf8',
);
const settingsTs = fs.readFileSync(
  path.join(ROOT, 'src/services/services/settingsService.ts'),
  'utf8',
);
const verifyTs = fs.readFileSync(
  path.join(ROOT, 'src/services/services/loadOrderVerificationService.ts'),
  'utf8',
);

yes(
  'loadOrderCheck.ts names the unreachable notice',
  checkTs.indexOf('The server did not answer.') !== -1 &&
    checkTs.indexOf('Load order could not be checked.') !== -1 &&
    checkTs.indexOf('LOAD ORDER ERROR') !== -1,
);
yes(
  'getServerMods return type allows null',
  /getServerMods\(\):\s*Promise<Mod\[\]\s*\|\s*null>/.test(settingsTs),
);
yes(
  'getServerMods returns null after retries',
  /Unreachable after retries[\s\S]*return null;/.test(settingsTs),
);
yes(
  'verifyLoadOrder imports evaluateLoadOrder',
  verifyTs.indexOf('evaluateLoadOrder') !== -1 &&
    verifyTs.indexOf("result.kind === 'unreachable'") !== -1,
);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);