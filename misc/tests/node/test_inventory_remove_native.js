'use strict';
// Thornswood #2146. Builds test_inventory_remove_native.cpp against the real
// InventoryRemove.h and runs it.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const headers = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
if (!fs.existsSync(path.join(headers, 'InventoryRemove.h'))) throw Error('InventoryRemove.h missing');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-remove-'));
try {
  const fixture = path.join(__dirname, 'test_inventory_remove_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'remove.exe' : 'remove');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'remove.obj'), fixture]
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
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-remove-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}
