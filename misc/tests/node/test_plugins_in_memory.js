'use strict';
// Thornswood #1602. The dev server's loop stopped for 25 to 29 s the first time
// somebody entered a cell. Measured 4 Oct: the main thread waited on PageIn
// with no CPU while about 380 pages a second came in from disk, with 10 GB
// of memory free. The plugins were mapped, and a cell's records were read
// from the VPS disk on the main thread the first time anything asked for
// them. This checks the server reads every plugin into memory at startup,
// and that a buffer read that way never goes back to its file.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');

let failures = 0;
function check(ok, what) { console.log((ok ? 'PASS  ' : 'FAIL  ') + what); if (!ok) failures++; }

const server = fs.readFileSync(path.join(__dirname, '../../../skymp5-server/cpp/addon/ScampServer.cpp'), 'utf8');
const loaders = server.match(/new espm::Loader\([\s\S]*?\);/g) || [];
check(loaders.length === 1, 'the server builds one plugin loader (' + loaders.length + ')');
check(loaders.length === 1 && /BufferType::AllocatedBuffer/.test(loaders[0]),
  'and it reads every plugin into memory, not a mapping read on first use');
check(/Read \{\} plugins \(\{\} MB\) into memory in/.test(server),
  'the server log says how much it read and how long it took');

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-plugins-in-memory-'));
try {
  const viet = path.join(__dirname, '../../../viet');
  const sources = [path.join(__dirname, 'test_plugins_in_memory.cpp'),
    path.join(viet, 'src', 'AllocatedBuffer.cpp'), path.join(viet, 'src', 'MappedBuffer.cpp')];
  const exe = path.join(work, process.platform === 'win32' ? 'buffers.exe' : 'buffers');
  const { command: compiler, env } = require('./cxx').compiler();
  const include = path.join(viet, 'include');
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/DWIN32', '/I' + include, '/Fe:' + exe, '/Fo:' + work + path.sep].concat(sources)
    : ['-std=c++17', '-I' + include].concat(sources, ['-o', exe]);
  const build = cp.spawnSync(compiler, args, { encoding: 'utf8', cwd: work, env });
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  check(build.status === 0, 'the buffer fixture builds');
  if (build.status === 0) {
    const run = cp.spawnSync(exe, [path.join(work, 'plugin.esp')], { encoding: 'utf8' });
    process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
    check(run.status === 0, 'the buffer fixture passes');
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-plugins-in-memory-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, { recursive: true, force: true });
}
console.log(failures ? 'FAILED ' + failures : 'OK');
process.exitCode = failures ? 1 : 0;
