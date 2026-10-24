'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const cp = require('node:child_process');
const root = path.resolve(__dirname, '../../..');
const source = fs.readFileSync(path.join(root, 'skymp5-server/cpp/addon/ScampServer.cpp'), 'utf8').replace(/\r\n/g, '\n');
function method(name, next) {
  const start = `Napi::Value ScampServer::${name}(const Napi::CallbackInfo& info)\n{`;
  const finish = `\nNapi::Value ScampServer::${next}(const Napi::CallbackInfo& info)\n{`;
  assert.equal(source.split(start).length, 2, `Unique production ${name} envelope required`);
  const begin = source.indexOf(start), end = source.indexOf(finish, begin);
  assert.ok(end > begin, `Production ${name} end missing`);
  const body = source.slice(begin, end).trimEnd();
  assert.ok(body.endsWith('\n}'), `${name} envelope changed`);
  return body;
}
const bodies = method('CompleteSavesForShutdown', 'Tick') + '\n' + method('Tick', 'On');
assert.match(source, /InstanceMethod\("completeSavesForShutdown",\s*&ScampServer::CompleteSavesForShutdown\)/);
const header = fs.readFileSync(path.join(root, 'skymp5-server/cpp/addon/ScampServer.h'), 'utf8');
assert.match(header, /Napi::Value CompleteSavesForShutdown\(const Napi::CallbackInfo& info\);/);
const types = fs.readFileSync(path.join(root, 'skymp5-server/ts/scampNative.ts'), 'utf8');
assert.match(types, /completeSavesForShutdown\(timeoutMs: number\): SaveCompletionResult;/);
assert.match(types, /readonly phase: "writes-completed";/);
assert.match(types, /readonly supportsDurableUpsert: boolean;/);

// CI uses the configured vcpkg SDK; local source-only builds can name the same
// SDK explicitly. Never download headers/libraries or load the production addon.
const triplet = process.platform === 'win32' ? 'x64-windows-sp' : process.platform === 'darwin' ? 'x64-osx' : 'x64-linux';
const sdk = process.env.SKYMP_TEST_NODE_SDK_ROOT || path.join(root, 'build/vcpkg_installed', triplet);
const include = path.join(sdk, 'include');
const nodeInclude = ['node', 'node-embedder-api-prebuilt', 'node-embedder-api', ''].map(p => path.join(include, p))
  .find(p => fs.existsSync(path.join(p, 'node_api.h')));
assert.ok(fs.existsSync(path.join(include, 'napi.h')) && nodeInclude, 'Configured Node-API SDK missing');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-save-completion-'));
try {
  fs.writeFileSync(path.join(work, 'SaveCompletionMethods.inc'), bodies);
  const output = path.join(work, 'save_completion.node');
  const fixture = path.join(__dirname, 'test_save_completion_addon.cpp');
  const {command, env} = require('./cxx').compiler();
  const includes = [work, include, nodeInclude];
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/LD', '/MD', '/DNAPI_CPP_EXCEPTIONS',
      ...includes.map(p => '/I' + p), '/Fo:' + path.join(work, 'fixture.obj'), fixture,
      '/link', '/OUT:' + output, path.join(sdk, 'lib/node.lib')]
    : ['-std=c++17', '-shared', '-fPIC', '-DNAPI_CPP_EXCEPTIONS', ...includes.flatMap(p => ['-I', p]), fixture, '-o', output,
      ...(process.platform === 'darwin' ? ['-Wl,-undefined,dynamic_lookup'] : [])];
  const build = cp.spawnSync(command, args, {encoding: 'utf8', cwd: work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  assert.equal(build.status, 0, 'Boundary fixture compilation failed');
  // Load in a child so Windows releases the DLL before task-temp cleanup.
  const run = cp.spawnSync(process.execPath, [path.join(__dirname, 'save_completion_addon_cases.js'), output], {encoding: 'utf8'});
  process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
  if (run.error) throw run.error;
  assert.equal(run.status, 0, 'Boundary assertions failed');
} finally {
  const target = fs.realpathSync(work), parent = fs.realpathSync(os.tmpdir());
  assert.equal(path.dirname(target).toLowerCase(), parent.toLowerCase());
  assert.ok(path.basename(target).startsWith('thornswood-save-completion-'));
  fs.rmSync(target, {recursive: true, force: true});
}
