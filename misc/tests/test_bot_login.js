// Thornswood #5196. A scripted bot logs in to this very server over the
// network and is sent its character, through every part a game's login
// goes through: SLikeNet with the server's password, login.ts's offline
// login, the Thornswood login challenge and proof (thornswoodLogin.ts),
// spawn.ts loading an existing character, and the createActor PartOne sends
// its owner. The bot is createBotClient from this server's own
// scam_native.node, so its bytes are a game's.
//
// This file is the gamemode, as every misc/tests/test_*.js is: the server
// runs it, and it connects back to the server's own port.
const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");

const addon = require(process.cwd() + "/scam_native.node");
const settings = JSON.parse(fs.readFileSync("server-settings.json", "utf8"));
const port = Number(settings.port) || 7777;
const password = settings.password === undefined ? "" : String(settings.password);

const PROFILE = 4242;
const PASS = "bot-pass-for-test-5196";
const KEY = "bot-login-key-for-test-5196";
const POS = [133857, -61130, 14662];
const INVENTORY = { entries: [{ baseId: 0xf, count: 7 }] };

const mac = (nonce) => crypto.createHmac("sha256", KEY).update("jlp1." + nonce).digest("base64url");

const asked = [];
mp.onThornswoodLogin = (e) => {
  asked.push(e);
  if (!e.proof) {
    return -1; // wait for the proof
  }
  return e.proof.pass === PASS && e.proof.mac === mac(e.nonce) ? e.profileId : 0;
};

const until = async (bot, test, ms, what) => {
  const end = Date.now() + ms;
  const seen = [];
  for (;;) {
    for (const e of bot.tick()) {
      seen.push(e);
      const v = test(e);
      if (v) {
        return { value: v, seen };
      }
      if (e.type === "connectionDenied" || e.type === "connectionFailed") {
        throw new Error(what + ": " + e.type + " " + (e.error || ""));
      }
    }
    if (Date.now() > end) {
      throw new Error("no " + what + " in " + ms + " ms; read " + JSON.stringify(seen).slice(0, 2000));
    }
    await new Promise((r) => setTimeout(r, 5));
  }
};

const custom = (e) => {
  if (e.type !== "message" || !e.message || e.message.t !== 1) {
    return null;
  }
  try {
    return JSON.parse(e.message.contentJsonDump);
  } catch (err) {
    return null;
  }
};

const main = async () => {
  const actorId = mp.createActor(0, POS, 90, 0x3c, PROFILE);
  mp.set(actorId, "inventory", INVENTORY);

  assert.throws(() => addon.createBotClient("127.0.0.1", 0), /port/);

  const wrong = addon.createBotClient("127.0.0.1", port, password + "-not-it", 5000);
  const denied = await until(wrong, (e) => e.type === "connectionDenied" && e, 15000, "refusal of a wrong password");
  assert.equal(denied.value.error, "Invalid password");
  wrong.destroy();
  // A second connection from the same address straight after is allowed:
  // SLikeNet's limit on how often one address may connect
  // (SetLimitIPConnectionFrequency in Networking.cpp) leaves loopback out
  // (RakPeer.cpp, IsLoopbackAddress), and every bot here is on 127.0.0.1.

  const bot = addon.createBotClient("127.0.0.1", port, password, 15000);
  await until(bot, (e) => e.type === "connectionAccepted", 15000, "connection");
  assert.equal(bot.isConnected(), true);

  bot.send({
    t: 1,
    contentJsonDump: JSON.stringify({ customPacketType: "loginWithSkympIo", gameData: { profileId: PROFILE } }),
  });
  const challenge = await until(bot, (e) => {
    const c = custom(e);
    return c && c.customPacketType === "thornswoodLoginChallenge" && c;
  }, 15000, "login challenge");
  assert.match(challenge.value.n, /.+/);
  assert.equal(asked.length, 1);
  assert.equal(asked[0].profileId, PROFILE);
  assert.equal(asked[0].proof, null);

  bot.send({
    t: 1,
    contentJsonDump: JSON.stringify({ customPacketType: "thornswoodLoginProof", pass: PASS, mac: mac(challenge.value.n) }),
  });
  const me = await until(bot, (e) => e.type === "message" && e.message && e.message.t === 33 && e.message.isMe && e.message, 15000, "character");
  assert.equal(asked.length, 2);
  assert.deepEqual(asked[1].proof, { pass: PASS, mac: mac(challenge.value.n) });

  const c = me.value;
  assert.equal(mp.getUserActor(mp.getUserByActor(actorId)), actorId);
  assert.equal(c.transform.worldOrCell, 0x3c);
  assert.deepEqual(c.transform.pos.map(Math.round), POS);
  assert.deepEqual(c.props.inventory, INVENTORY);
  // No appearance yet, so spawn.ts reopens the race menu, and the first
  // packet says so.
  assert.equal(c.props.isRaceMenuOpen, true);

  bot.destroy();
  assert.deepEqual(bot.tick(), []);
};

main().then(
  () => { console.log("Test passed!"); process.exit(0); },
  (error) => { console.error(error); console.log("Test failed!"); process.exit(1); }
);
