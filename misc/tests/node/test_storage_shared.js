'use strict';
// Thornswood #3333. SkyrimPlatform makes one exports object for every plugin
// file it loads and runs storageProxy.js on each, all in one JavaScript
// environment. Every one of them must get the same storage, and a hot reload
// (a new exports object in the same environment) must find what was there.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const src = fs.readFileSync(path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform/assets/storageProxy.js'), 'utf8');

// One environment, as SkyrimPlatform has: the script is run again for each
// exports object, the way Sp3Api::Register runs it.
const env = vm.createContext({});
const load = () => { const sp = {}; vm.runInContext(src, env)(sp); return sp; };

const client = load(), front = load();
assert.equal(client.storage, front.storage, 'two plugins share one storage');

const view = { getFormViews() { return 'views'; } };
client.storage.view = view;
assert.equal(front.storage.view, view, 'what one plugin stores, another reads');
assert.equal(front.storage.view.getFormViews(), 'views');

front.storage.thornswoodJoinPass = { pass: 'p', key: 'k' };
assert.deepEqual(client.storage.thornswoodJoinPass, { pass: 'p', key: 'k' }, 'and the other way');

const reloaded = load();
assert.equal(reloaded.storage.view, view, 'a hot reload finds what was stored');

// The contract the client and gamemode test with is unchanged.
assert.equal(typeof front.storage.notSet, 'function', 'an unset key is still a function');
assert.throws(() => front.storage.notSet(), /not defined in storage/);
assert.equal(front.storage.then, undefined);
delete client.storage.view;
assert.equal(typeof front.storage.view, 'function', 'a deleted key is unset for every plugin');

// A separate environment is a separate storage.
const other = {}; vm.runInNewContext(src)(other);
assert.notEqual(other.storage, client.storage);
assert.equal(typeof other.storage.thornswoodJoinPass, 'function');

console.log('PASS every plugin shares one storage, across hot reloads');
