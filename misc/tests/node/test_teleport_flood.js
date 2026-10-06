'use strict';
// Thornswood #1932. Replays the dev teleport flood of 5 Oct 2026 through the
// server functions that produced it, copied out of the engine sources into
// test_teleport_flood.cpp's stand-ins (the whole server cannot be built
// without vcpkg; unit/MovementValidationTest.cpp runs the same sequence
// through the real PartOne).
//
// Usage: node test_teleport_flood.js [revision]
// With a revision the server sources are read from git at that revision
// instead of the working tree: the "before" run of the regression. The C++
// compiler comes from ./cxx.js (CXX, a developer prompt, or Visual Studio
// found through vswhere), so ctest can run it from a plain shell (#1970).
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');

const root = path.join(__dirname, '../../..');
const rev = process.argv[2];
const readFile = (rel) => {
  const text = rev
    ? cp.execFileSync('git', ['-C', root, 'show', `${rev}:${rel}`], { maxBuffer: 1 << 26 }).toString('utf8')
    : fs.readFileSync(path.join(root, rel), 'utf8');
  return text.replace(/\r\n/g, '\n');
};
const read = (name) => readFile(`skymp5-server/cpp/server_guest_lib/${name}`);

// From the start of the line holding `signature` to the brace closing the
// first block after it.
function functionAt(source, signature, file) {
  const at = source.indexOf(signature);
  if (at < 0) return null;
  const start = source.lastIndexOf('\n', at) + 1;
  const open = source.indexOf('{', at);
  let depth = 1;
  let end = open + 1;
  while (depth > 0 && end < source.length) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
  }
  if (depth) throw new Error(`unterminated ${signature} in ${file}`);
  return source.slice(start, end) + '\n';
}
function need(text, what) {
  if (text === null) throw new Error(`missing ${what}`);
  return text;
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-teleport-flood-'));
let status = 1;
try {
  const validation = read('MovementValidation.cpp');
  fs.writeFileSync(path.join(work, 'MovementValidationUnderTest.inc'),
    'namespace MovementValidation {\n' +
    need(functionAt(validation, 'bool Validate(PartOne& partOne,', 'MovementValidation.cpp'), 'MovementValidation::Validate') +
    '}\n');

  const listener = read('ActionListener.cpp');
  fs.writeFileSync(path.join(work, 'OnUpdateMovementUnderTest.inc'),
    need(functionAt(listener, 'void ActionListener::OnUpdateMovement(', 'ActionListener.cpp'), 'ActionListener::OnUpdateMovement'));

  // MpActor::Teleport exists on every revision; the numbering functions only
  // on revisions that have the fix, and the old code never calls them.
  const actor = read('MpActor.cpp');
  let actorParts = need(functionAt(actor, 'void MpActor::Teleport(', 'MpActor.cpp'), 'MpActor::Teleport');
  let numbered = 0;
  for (const signature of [
    'std::optional<uint32_t> MpActor::NumberTeleportForOwnClient(',
    'bool MpActor::IsSentBeforeNewestTeleport(',
    'void MpActor::ForgetTeleportsForOwnClient(',
  ]) {
    const part = functionAt(actor, signature, 'MpActor.cpp');
    if (part) { actorParts += part; numbered++; }
  }
  if (numbered !== 0 && numbered !== 3) throw new Error('only part of the teleport numbering found in MpActor.cpp');
  fs.writeFileSync(path.join(work, 'MpActorTeleportUnderTest.inc'), actorParts);

  // The door branch of MpObjectReference::ProcessActivateNormal, from the
  // message it builds to the actor's new place.
  const refr = read('MpObjectReference.cpp');
  const doorStart = refr.indexOf('      TeleportMessage msg;\n      msg.idx = activationSource.GetIdx();');
  const doorEndMark = '      activationSource.SetAngle({ rot[0], rot[1], rot[2] });\n';
  const doorEnd = refr.indexOf(doorEndMark, doorStart);
  if (doorStart < 0 || doorEnd < 0) throw new Error('door teleport block not found in MpObjectReference.cpp');
  fs.writeFileSync(path.join(work, 'DoorTeleportUnderTest.inc'), refr.slice(doorStart, doorEnd + doorEndMark.length));

  // The headers and sources the replay compiles with, from the same
  // revision. Since Thornswood #1715 the load order is an espm::LoadOrder
  // (libespm/LoadOrder.h), not a list of file names.
  const sources = [path.join(__dirname, 'test_teleport_flood.cpp')];
  for (const name of ['FormDesc.h', 'FormDesc.cpp', 'LocationalData.h', 'NiPoint3.h']) {
    fs.writeFileSync(path.join(work, name), read(name));
  }
  sources.push(path.join(work, 'FormDesc.cpp'));
  const loadOrder = /libespm\/LoadOrder\.h/.test(read('FormDesc.h'));
  if (loadOrder) {
    fs.mkdirSync(path.join(work, 'libespm'));
    fs.writeFileSync(path.join(work, 'libespm', 'LoadOrder.h'), readFile('libespm/include/libespm/LoadOrder.h'));
    fs.writeFileSync(path.join(work, 'LoadOrder.cpp'), readFile('libespm/src/LoadOrder.cpp'));
    sources.push(path.join(work, 'LoadOrder.cpp'));
  }

  fs.writeFileSync(path.join(work, 'TeleportNumbering.h'),
    `#define TELEPORT_NUMBERING_IN_SOURCE ${numbered ? 1 : 0}\n` +
    `#define ESPM_LOAD_ORDER_IN_SOURCE ${loadOrder ? 1 : 0}\n`);
  console.log(`server code from ${root}${rev ? ' at ' + rev : ''} (teleport numbering ${numbered ? 'present' : 'absent'}, ` +
    `load order ${loadOrder ? 'espm::LoadOrder' : 'a list of file names'})`);

  const exe = path.join(work, process.platform === 'win32' ? 'flood.exe' : 'flood');
  const { command: compiler, env } = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + work, '/Fe:' + exe, '/Fo:' + work + path.sep].concat(sources)
    : ['-std=c++17', '-I' + work].concat(sources, ['-o', exe]);
  const build = cp.spawnSync(compiler, args, { encoding: 'utf8', cwd: work, env });
  if (build.error) throw build.error;
  if (build.status !== 0) {
    process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
    throw new Error('the replay does not build');
  }
  const run = cp.spawnSync(exe, [], { encoding: 'utf8' });
  process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
  status = run.status;
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-teleport-flood-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, { recursive: true, force: true });
}
process.exitCode = status === 0 ? 0 : 1;
