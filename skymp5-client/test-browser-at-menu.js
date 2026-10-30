// test-browser-at-menu.js
//
//     node skymp5-client/test-browser-at-menu.js
//
// Thornswood #3129. SkyrimPlatform now shows the browser from the start
// ([Browser] ShowAtStart), so the character choice at the main menu is up
// before any plugin runs. browserService.ts hid the browser the moment the
// client loaded, and the game's own menu picture came back on screen until
// thornswood-front.js showed the page again a moment later.
//
// WHAT THIS RUNS. The real browserService.ts, its TypeScript types taken off
// (node's module.stripTypeScriptTypes, Node 22.13 or newer, or the typescript
// package), over a stand-in sp.browser that records every setVisible.
//
// What it checks:
//   1. with gameData.chooseAtMenu the client leaves the browser as it is;
//   2. without it (an older launcher, or no gameData) it hides it as before;
//   3. with no skymp5-client settings at all it hides it and does not throw;
//   4. the first update still shows it, either way.
//
// BROWSER=<file> runs it against another copy; against the code before the
// change case 1 fails.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const BROWSER = process.env.BROWSER ||
  path.join(ROOT, 'src', 'services', 'services', 'browserService.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); }
  else { fail++; console.log('FAIL  ' + what); }
}

function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); }
    finally { process.emitWarning = warn; }
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

// Same loader as test-connection-denied.js: every import comes from stubs.
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

class ClientListener { }
const stubs = {
  '../../view/formView': { FormView: { isDisplayingNicknames: false } },
  '../events/queryKeyCodeBindings': {},
  './clientListener': { ClientListener: ClientListener },
  'skyrimPlatform': {
    DxScanCode: { F1: 59, F2: 60, F6: 64, Enter: 28, Escape: 1 },
    Menu: {
      Barter: 'BarterMenu', Book: 'Book Menu', Container: 'ContainerMenu', Crafting: 'Crafting Menu',
      Gift: 'GiftMenu', Inventory: 'InventoryMenu', Journal: 'Journal Menu', Lockpicking: 'Lockpicking Menu',
      Loading: 'Loading Menu', Map: 'MapMenu', RaceSex: 'RaceSex Menu', Stats: 'StatsMenu',
      Tween: 'TweenMenu', Console: 'Console', Main: 'Main Menu', HUD: 'HUD Menu',
    },
  },
};
const BrowserService = load(BROWSER, stubs).BrowserService;

// A client loading at the main menu, the browser already shown by
// SkyrimPlatform. settings is what sp.settings holds.
function rig(settings) {
  const w = { visible: [], shown: true, onceUpdate: [] };
  const sp = {
    settings: settings,
    browser: {
      setVisible: function (v) { w.visible.push(v); w.shown = v; },
      isVisible: function () { return w.shown; },
      setFocused: function () { },
      isFocused: function () { return true; },
      executeJavaScript: function () { },
    },
  };
  const controller = {
    on: function () { },
    once: function (ev, fn) { if (ev === 'update') w.onceUpdate.push(fn); },
    emitter: { on: function () { }, emit: function () { } },
  };
  w.threw = null;
  try { w.service = new BrowserService(sp, controller); } catch (e) { w.threw = e; }
  w.update = function () { w.onceUpdate.splice(0).forEach(function (f) { f(); }); };
  return w;
}

// 1. The choice at the menu.
const a = rig({ 'skymp5-client': { gameData: { chooseAtMenu: true, profileId: 7 } } });
yes('with chooseAtMenu the client does not hide the browser when it loads', a.visible.indexOf(false) === -1);
yes('so the choice page stays on screen', a.shown === true);
a.update();
yes('and the first update shows it, as before', a.visible[a.visible.length - 1] === true);

// 2. No choice at the menu.
const b = rig({ 'skymp5-client': { gameData: { profileId: 7 } } });
yes('without chooseAtMenu it hides the browser when it loads, as before', b.visible[0] === false);
b.update();
yes('and the first update shows it again', b.visible[b.visible.length - 1] === true);
const c = rig({ 'skymp5-client': { gameData: { chooseAtMenu: false } } });
yes('chooseAtMenu false hides it too', c.visible[0] === false);
const d = rig({ 'skymp5-client': {} });
yes('no gameData hides it too', d.visible[0] === false);

// 3. No settings for the client at all.
const e = rig({});
yes('no skymp5-client settings does not throw', e.threw === null);
yes('and hides it, as before', e.visible[0] === false);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
