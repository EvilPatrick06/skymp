'use strict';
// Thornswood #2384. Builds test_cursor_focus_native.cpp against the real
// CursorFocus.h and runs it.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const headers = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
if (!fs.existsSync(path.join(headers, 'CursorFocus.h'))) throw Error('CursorFocus.h missing');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-cursor-'));
try {
  const fixture = path.join(__dirname, 'test_cursor_focus_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'cursor.exe' : 'cursor');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'cursor.obj'), fixture]
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
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-cursor-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}
