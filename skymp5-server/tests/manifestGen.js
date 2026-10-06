'use strict';
// Thornswood #1715. The manifest the client checks its plugins against lists
// full plugins in "mods" and light plugins, apart, in "light", in light index
// order, the way the game numbers them (Game.getModName and
// Game.getLightModName). Lightness is the game's rule: the ESL flag 0x200 in
// the TES4 record header, or an .esl extension.
//
// Runs ts/manifestGen.ts (or the file MANIFEST_GEN names) on plugin files it
// writes into a folder of its own, and reads back the manifest.json it writes.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), Module = require('node:module');
const ts = require('typescript');

const file = process.env.MANIFEST_GEN || path.join(__dirname, '../ts/manifestGen.ts');
function load(p) {
  const out = ts.transpileModule(fs.readFileSync(p, 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022, esModuleInterop: true },
  }).outputText;
  const m = new Module(p);
  m.filename = p;
  m.paths = Module._nodeModulePaths(path.dirname(p)).concat(Module._nodeModulePaths(__dirname));
  m._compile(out, p);
  return m.exports;
}
const manifestGen = load(file);

// A TES4 record header with these flags and an empty HEDR-less body: enough
// for the flag test, which reads the type and the flags only.
function plugin(flags, extra = 0) {
  const b = Buffer.alloc(24 + extra);
  b.write('TES4', 0, 'latin1');
  b.writeUInt32LE(extra, 4);
  b.writeUInt32LE(flags, 8);
  return b;
}

let failed = 0;
function check(name, ok, detail) {
  console.log((ok ? 'PASS  ' : 'FAIL  ') + name + (ok || detail === undefined ? '' : '\n      ' + detail));
  if (!ok) failed++;
}

const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'manifest-gen-'));
try {
  const files = {
    'Skyrim.esm': plugin(0x1, 10),
    'KhisartinBeards.esp': plugin(0x200, 1),       // light by its flag
    'Thornswood-Content.esp': plugin(0x0, 2),      // full
    'ccQDRSSE001-SurvivalMode.esl': plugin(0x0, 3), // light by its extension
    'MoreCraftableEquipment.esp': plugin(0x200, 4),
    'Embers XD.esm': plugin(0x201, 5),             // a master with the flag is light
    'Thornswood-Content.bsa': Buffer.from('full plugin archive'),
    'KhisartinBeards.bsa': Buffer.from('light plugin archive'),
  };
  for (const [name, bytes] of Object.entries(files)) fs.writeFileSync(path.join(dataDir, name), bytes);

  const order = ['Skyrim.esm', 'KhisartinBeards.esp', 'Thornswood-Content.esp', 'ccQDRSSE001-SurvivalMode.esl',
                 'MoreCraftableEquipment.esp', 'Embers XD.esm', 'NotOnDisk.esp'];
  const errors = [];
  const realError = console.error;
  console.error = (...a) => errors.push(a.join(' '));
  try {
    manifestGen.generateManifest({ dataDir, loadOrder: order });
  } finally {
    console.error = realError;
  }
  const m = JSON.parse(fs.readFileSync(path.join(dataDir, 'manifest.json'), 'utf8'));
  const names = (list) => (list || []).map((e) => e.filename);

  check('mods holds the full plugins in load order, each followed by its archive',
        JSON.stringify(names(m.mods)) === JSON.stringify(['Skyrim.esm', 'Thornswood-Content.esp', 'Thornswood-Content.bsa']),
        JSON.stringify(names(m.mods)));
  check('light holds the light plugins in light index order, plugins only',
        JSON.stringify(names(m.light)) ===
          JSON.stringify(['KhisartinBeards.esp', 'ccQDRSSE001-SurvivalMode.esl', 'MoreCraftableEquipment.esp', 'Embers XD.esm']),
        JSON.stringify(names(m.light)));
  const beard = (m.light || []).find((e) => e.filename === 'KhisartinBeards.esp');
  check('a light entry carries the file size and crc32 the client compares',
        !!beard && beard.size === files['KhisartinBeards.esp'].length && typeof beard.crc32 === 'number',
        JSON.stringify(beard));
  check('loadOrder still names every plugin the server was given',
        JSON.stringify(m.loadOrder) === JSON.stringify(order), JSON.stringify(m.loadOrder));
  check('a plugin in the load order and not on disk is named and left out',
        errors.some((e) => e.indexOf('NotOnDisk.esp') !== -1), JSON.stringify(errors));

  const isLight = manifestGen.isLightPlugin;
  check('isLightPlugin is exported', typeof isLight === 'function');
  if (typeof isLight === 'function') {
    check('the ESL flag makes an .esp light', isLight('a.esp', plugin(0x200)) === true);
    check('an .esl is light without the flag, any case', isLight('a.ESL', plugin(0)) === true);
    check('a master flag alone is not light', isLight('a.esm', plugin(0x1)) === false);
    check('a file that is not a plugin is not light', isLight('a.esp', Buffer.from('not a plugin at all')) === false);
  }
} finally {
  fs.rmSync(dataDir, { recursive: true, force: true });
}
console.log(failed ? failed + ' failed' : 'manifestGen: all passed');
process.exitCode = failed ? 1 : 0;
