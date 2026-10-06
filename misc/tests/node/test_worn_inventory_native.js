'use strict';
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const source = fs.readFileSync(path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform/PapyrusTESModPlatform.cpp'), 'utf8');
const begin = source.indexOf('void TESModPlatform::AddItemEx('), end = source.indexOf('\nvoid TESModPlatform::UpdateEquipment(', begin);
if (begin < 0 || end < 0) throw Error('Native AddItemEx boundary missing');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-outfit-'));
try {
  fs.writeFileSync(path.join(work, 'WornInventoryUnderTest.inc'), source.slice(begin, end));
  const fixture = path.join(__dirname, 'test_worn_inventory_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'outfit.exe' : 'outfit');
  const {command: compiler, env} = require('./cxx').compiler();
  const vietHeaders = path.join(__dirname, '../../../viet/include');
  const args = process.platform === 'win32' ? ['/nologo', '/EHsc', '/std:c++17', '/I' + work, '/I' + vietHeaders, '/Fe:' + exe, '/Fo:' + path.join(work, 'outfit.obj'), fixture] : ['-std=c++17', '-I' + work, '-I' + vietHeaders, fixture, '-o', exe];
  const build = cp.spawnSync(compiler, args, {encoding:'utf8', cwd:work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  if (build.status !== 0) process.exitCode = build.status || 1;
  else {
    const run = cp.spawnSync(exe, [], {encoding:'utf8'});
    process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
    if (run.status !== 0) process.exitCode = run.status || 1;
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-outfit-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive:true, force:true});
}
