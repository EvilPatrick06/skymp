'use strict';
// Thornswood #1649. Builds test_made_potion_native.cpp against the real
// MadePotion.h (the checks createMadePotion makes before it touches the game)
// and runs it. Also checks the client's made range is the server's.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const headers = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
if (!fs.existsSync(path.join(headers, 'MadePotion.h'))) throw Error('MadePotion.h missing');
const serverInventory = fs.readFileSync(path.join(__dirname, '../../../skymp5-server/cpp/server_guest_lib/Inventory.h'), 'utf8');
const clientMade = fs.readFileSync(path.join(headers, 'MadePotion.h'), 'utf8');
const first = (text) => (text.match(/kMadeIdFirst = (0x[0-9a-f]+)/) || [])[1];
if (!first(serverInventory) || first(serverInventory) !== first(clientMade)) {
  console.log(`FAIL  the client builds made potions in the server's range (${first(clientMade)} vs ${first(serverInventory)})`);
  process.exitCode = 1;
} else console.log('PASS  the client builds made potions in the server\'s range');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-made-'));
try {
  const fixture = path.join(__dirname, 'test_made_potion_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'made.exe' : 'made');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'made.obj'), fixture]
    : ['-std=c++17', '-I' + headers, fixture, '-o', exe];
  const build = cp.spawnSync(compiler, args, {encoding: 'utf8', cwd: work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  if (build.status !== 0) process.exitCode = build.status || 1;
  else {
    const run = cp.spawnSync(exe, [], {encoding: 'utf8'});
    process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
    if (run.status !== 0) process.exitCode = run.status || 1;
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-made-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}
