import * as crypto from "crypto";

/*
  THORNSWOOD PATCH (Thornswood #1320). The client's half of proving the join
  pass on the connection that logs in (skymp5-server/ts/systems/thornswoodLogin.ts
  has the whole of it).

  Thornswood's front plugin puts the launcher's join pass and the key the site
  gave with it in storage under this name, as { pass, key }. When the server
  challenges a login with a nonce, the client answers with the pass and
  HMAC-SHA256 of "jlp1." + nonce under that key. The key itself never goes on
  the game's port.
*/
export const thornswoodJoinPassStorageKey = "thornswoodJoinPass";

export interface ThornswoodJoinPass {
  pass: string;
  key: string;
}

const PASS = /^jp1\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/;
const KEY = /^[A-Za-z0-9_-]{16,128}$/;

// What is in storage, when it is a pass and a key; null for anything else.
export const readThornswoodJoinPass = (v: unknown): ThornswoodJoinPass | null => {
  if (!v || typeof v !== "object") {
    return null;
  }
  const o = v as Record<string, unknown>;
  if (typeof o.pass !== "string" || typeof o.key !== "string") {
    return null;
  }
  if (!PASS.test(o.pass) || !KEY.test(o.key)) {
    return null;
  }
  return { pass: o.pass, key: o.key };
};

const base64url = (b: Buffer): string =>
  b.toString("base64").replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");

export const thornswoodLoginMac = (key: string, nonce: string): string =>
  base64url(crypto.createHmac("sha256", key).update("jlp1." + nonce).digest());

// A nonce as the server sends it: base64url, not empty, not long.
export const isThornswoodNonce = (n: unknown): n is string =>
  typeof n === "string" && /^[A-Za-z0-9_-]{16,128}$/.test(n);
