// test-game-year.js
//
//     node skymp5-client/test-game-year.js
//
// Thornswood #3156. The map shows the game's date, and its year said 4E 205 in
// 2026: TimeService wrote the year as the real one minus 2020 plus 199, which
// is upstream's count. Thornswood's world is 4E 252 in 2026 (its lore/README.md),
// so the year is now the real one minus 2026 plus 252, and it still turns with
// the real year, as the day and the month turn with the real day.
//
// WHAT THIS RUNS. The real timeService.ts, with its TypeScript types taken off
// (node's own module.stripTypeScriptTypes, Node 22.13 or newer, or the
// typescript package when it is installed), over a stand-in game whose
// calendar globals record what they are set to, at a clock turned by hand.
//
// Run it against the code before the change with TIMESERVICE=<that file>: the
// year cases fail there.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const TIMESERVICE = process.env.TIMESERVICE ||
  path.join(ROOT, 'src', 'services', 'services', 'timeService.ts');

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

// TypeScript to JavaScript, types only, as test-update-churn.js does it.
function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try {
      return mod.stripTypeScriptTypes(src, { mode: 'transform' });
    } finally {
      process.emitWarning = warn;
    }
  }
  let ts = null;
  try { ts = require('typescript'); } catch (e) { ts = null; }
  if (ts && typeof ts.transpileModule === 'function') {
    return ts.transpileModule(src, {
      compilerOptions: { target: ts.ScriptTarget.ES2019, module: ts.ModuleKind.ESNext },
    }).outputText;
  }
  throw new Error('needs Node 22.13 or newer, or the typescript package (yarn install here)');
}

function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

// A game with the five globals TimeService writes, at a clock set by hand.
function rig(hoursOffset) {
  const globals = { 0x35: 0, 0x36: 0, 0x37: 0, 0x38: 0, 0x3a: 20 };
  const sp = {
    settings: { 'skymp5-client': hoursOffset === undefined ? {} : { hoursOffset: hoursOffset } },
    Game: { getFormEx: function (id) { return Object.prototype.hasOwnProperty.call(globals, id) ? { id: id } : null; } },
    GlobalVariable: {
      from: function (f) {
        if (!f) { return null; }
        return {
          getValue: function () { return globals[f.id]; },
          setValue: function (v) { globals[f.id] = v; },
        };
      },
    },
  };
  let onUpdate = null;
  const controller = { on: function (ev, fn) { if (ev === 'update') { onUpdate = fn; } } };
  const mod = load(TIMESERVICE, { './clientListener': { ClientListener: class { } } });
  const service = new mod.TimeService(sp, controller);
  return {
    globals: globals,
    // One update at the given UTC time, as the game would run it.
    at: function (iso) {
      const real = Date.now;
      const t = Date.parse(iso);
      Date.now = function () { return t; };
      try {
        service.lastTimeUpd = 0;
        onUpdate();
      } finally {
        Date.now = real;
      }
      return globals;
    },
  };
}

let r = null;
try { r = rig(); } catch (e) { console.log('could not load timeService.ts: ' + e); }
yes('timeService.ts loads and listens for update', !!r);

if (r) {
  const g = r.at('2026-10-26T15:46:00Z');
  yes('26 October 2026 is the 26th of Frostfall, 4E 252',
    g[0x37] === 26 && g[0x36] === 9 && g[0x35] === 252);
  yes('the hour is the real one in UTC', Math.abs(g[0x38] - (15 + 46 / 60)) < 1e-9);

  const n = rig().at('2027-01-01T00:30:00Z');
  yes('the year turns with the real year: 1 January 2027 is the 1st of Morning Star, 4E 253',
    n[0x37] === 1 && n[0x36] === 0 && n[0x35] === 253);

  const e = rig().at('2026-12-31T23:59:00Z');
  yes('the last minute of 2026 is still 4E 252, the 31st of Evening Star',
    e[0x37] === 31 && e[0x36] === 11 && e[0x35] === 252);

  // hoursOffset moves the whole date, so a server that sets one has the year
  // turn at its own midnight, with the day and the month.
  const o = rig(2).at('2026-12-31T23:00:00Z');
  yes('an hoursOffset of 2 turns the year with the day: 23:00 UTC on 31 December is 4E 253',
    o[0x37] === 1 && o[0x36] === 0 && o[0x35] === 253);
}

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
