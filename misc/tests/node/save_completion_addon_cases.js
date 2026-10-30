'use strict';
const assert = require('node:assert/strict');
const {ScampServer} = require(process.argv[2]);
let checks = 0;
function test(name, fn) { fn(); ++checks; }
test('invalid arguments never enter native completion or coerce', () => {
  let coercions = 0;
  const object = {valueOf() { ++coercions; return 1; }, toString() { ++coercions; return '1'; }};
  const proxy = new Proxy(object, {get() { ++coercions; throw Error('coercion'); }});
  const invalid = [[], [undefined], [null], [true], [false], ['1'], [new Number(1)], [object], [proxy],
    [Symbol('timeout')], [1n], [NaN], [Infinity], [-Infinity], [0], [-0], [-1], [1.5], [30001],
    [4294967297], [Number.MAX_VALUE], [1, 2], [1, undefined]];
  for (const args of invalid) {
    const server = new ScampServer('active');
    assert.throws(() => server.completeSavesForShutdown(...args));
    assert.equal(server.inspect().calls, 0);
    server.tick();
    assert.equal(server.inspect().gameplayTicks, 1);
  }
  assert.equal(coercions, 0);
});
test('valid bounds and capabilities forward exactly once', () => {
  for (const timeoutMs of [1, 30000]) for (const durable of [false, true]) {
    const server = new ScampServer('active', durable);
    const result = server.completeSavesForShutdown(timeoutMs);
    assert.deepEqual(result, {phase: 'writes-completed', supportsDurableUpsert: durable});
    assert.equal(server.inspect().calls, 1);
    assert.equal(server.inspect().timeoutMs, timeoutMs);
    for (const key of ['phase', 'supportsDurableUpsert']) {
      const descriptor = Object.getOwnPropertyDescriptor(result, key);
      assert.equal(descriptor.enumerable, true);
      assert.equal(descriptor.writable, false);
      assert.equal(descriptor.configurable, false);
      assert.equal(descriptor.get, undefined);
    }
    assert.throws(() => server.tick(), /terminal world refuses ticking/);
    assert.deepEqual(server.inspect(), {calls: 1, timeoutMs, networkTicks: 0, gameplayTicks: 0, tickEnvSet: false});
  }
});
test('result creation cannot call inherited setters', () => {
  let setters = 0;
  const keys = ['phase', 'supportsDurableUpsert'];
  try {
    for (const key of keys) Object.defineProperty(Object.prototype, key, {configurable: true, set() { ++setters; throw Error('inherited setter'); }});
    const result = new ScampServer('active').completeSavesForShutdown(1);
    assert.equal(Object.hasOwn(result, 'phase'), true);
    assert.equal(result.phase, 'writes-completed');
    assert.equal(result.supportsDurableUpsert, false);
  } finally { for (const key of keys) delete Object.prototype[key]; }
  assert.equal(setters, 0);
});
test('standard and nonstandard errors become JavaScript errors', () => {
  for (const mode of ['standard', 'nonstandard']) {
    const server = new ScampServer(mode);
    assert.throws(() => server.completeSavesForShutdown(1), error => error instanceof Error);
    assert.equal(server.inspect().calls, 1);
    assert.throws(() => server.tick(), /terminal world refuses ticking/);
    assert.equal(server.inspect().networkTicks, 0);
    assert.equal(server.inspect().tickEnvSet, false);
  }
});
test('terminal tick refusal precedes every observable producer', () => {
  for (const mode of ['completed', 'failed']) {
    const server = new ScampServer(mode);
    assert.throws(() => server.tick(), /terminal world refuses ticking/);
    assert.deepEqual(server.inspect(), {calls: 0, timeoutMs: 0, networkTicks: 0, gameplayTicks: 0, tickEnvSet: false});
  }
});
test('active tick still runs network and gameplay', () => {
  const server = new ScampServer('active');
  assert.equal(server.tick(), undefined);
  assert.deepEqual(server.inspect(), {calls: 0, timeoutMs: 0, networkTicks: 1, gameplayTicks: 1, tickEnvSet: true});
});
console.log(`${checks} compiled production addon boundary cases passed`);
