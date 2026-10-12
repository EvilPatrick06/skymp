/*
  THORNSWOOD PATCH (Thornswood #1320). The offline login asks the gamemode, and
  the join pass is proved on the connection that logs in.

  In offline mode the client logs in with a profile number it picks, and
  login.ts handed that character over at once (spawnAllowed). Thornswood's
  gamemode checks a join pass the site signs (the join pass block in
  gamemode.js), but it arrives over a different socket, the voice relay, which
  names the same profile number and nothing else: the gamemode cannot tie it
  to this game connection, and the login itself rides the game's own UDP,
  which is not encrypted. So a pass seen on the wire could be shown under its
  profile on another connection.

  This adds two packets and one gamemode hook, and does nothing at all unless
  the gamemode sets the hook:

    mp.onThornswoodLogin(e)   e is { userId, profileId, nonce, proof }.
                              profileId is the number the client asked for,
                              nonce the one sent to this connection, proof
                              null at the login and { pass, mac } when the
                              client answers. It returns the profile to hand
                              over now (> 0), 0 to refuse, or anything else
                              to wait for the proof.

    thornswoodLoginChallenge  server to client, at the login: { n }, a fresh
                              random nonce for this connection's login.
    thornswoodLoginProof      client to server: { pass, mac }, the join pass
                              and HMAC-SHA256 of "jlp1." + n under the pass's
                              own key, which the site gave the launcher over
                              https beside the pass and which never travels
                              on this port. The gamemode derives the same key
                              from its passKey and the pass, so a pass seen on
                              the wire proves nothing on another connection:
                              that connection's nonce is a different one.

  A connection is challenged once per login and answered once. A proof with no
  challenge, from another connection than the one challenged, or a second
  answer, is not passed on. A character is handed over at most once per
  connection. Refusing is the gamemode's to say and do (it knows the person and
  the words); this only does not hand the character over.

  With no hook (an older gamemode) the login is exactly as before, and an
  older client that does not know the challenge never answers it, which the
  gamemode decides about: a gamemode that is only watching hands the character
  over at the login, as before, and logs what the proof said when one comes.
*/

export interface ThornswoodLoginServer {
  sendCustomPacket(userId: number, jsonContent: string): void;
  getUserGuid(userId: number): string;
  isConnected(userId: number): boolean;
}

export interface ThornswoodLoginProof {
  pass: string;
  mac: string;
}

export interface ThornswoodLoginEvent {
  userId: number;
  profileId: number;
  nonce: string;
  proof: ThornswoodLoginProof | null;
}

export type ThornswoodLoginHook = (e: ThornswoodLoginEvent) => unknown;

interface Challenge {
  nonce: string;
  guid: string;
  profileId: number;
  answered: boolean;
}

// Long enough for any pass the site signs, short enough that nobody can hand
// the gamemode a megabyte through this packet.
const MAX_PASS = 2048;
const MAX_MAC = 128;

export class ThornswoodLogin {
  constructor(
    private svr: ThornswoodLoginServer,
    private hook: () => ThornswoodLoginHook | undefined,
    private spawn: (userId: number, profileId: number) => void,
    private log: (...args: unknown[]) => void,
    private newNonce: () => string
  ) { }

  // The offline login. False when the gamemode set no hook, and login.ts goes
  // on as before.
  login(userId: number, profileId: number): boolean {
    const hook = this.hook();
    if (typeof hook !== "function") {
      return false;
    }
    if (this.spawned.has(userId) || this.challenges.has(userId)) {
      this.log("Thornswood login: user " + userId + " logged in again on the same connection, ignored");
      return true;
    }
    const nonce = this.newNonce();
    this.challenges.set(userId, {
      nonce,
      guid: this.svr.getUserGuid(userId),
      profileId,
      answered: false
    });
    this.svr.sendCustomPacket(userId, JSON.stringify({ customPacketType: "thornswoodLoginChallenge", n: nonce }));
    this.ask(hook, userId, profileId, nonce, null);
    return true;
  }

  // The client's answer to the challenge.
  proof(userId: number, content: Record<string, unknown>): void {
    const c = this.challenges.get(userId);
    if (!c) {
      this.log("Thornswood login: a proof from user " + userId + " with no challenge, not passed on");
      return;
    }
    if (c.answered) {
      this.log("Thornswood login: a second proof from user " + userId + ", not passed on");
      return;
    }
    if (this.svr.getUserGuid(userId) !== c.guid) {
      this.log("Thornswood login: a proof from another connection on user " + userId + ", not passed on");
      return;
    }
    c.answered = true;
    const pass = content["pass"];
    const mac = content["mac"];
    if (typeof pass !== "string" || typeof mac !== "string" ||
        pass.length > MAX_PASS || mac.length > MAX_MAC) {
      this.log("Thornswood login: a proof from user " + userId + " that is not a pass and a mac");
      this.ask(this.hook(), userId, c.profileId, c.nonce, { pass: "", mac: "" });
      return;
    }
    this.ask(this.hook(), userId, c.profileId, c.nonce, { pass, mac });
  }

  disconnect(userId: number): void {
    this.challenges.delete(userId);
    this.spawned.delete(userId);
  }

  private ask(hook: ThornswoodLoginHook | undefined, userId: number, claimed: number,
              nonce: string, proof: ThornswoodLoginProof | null): void {
    if (typeof hook !== "function") {
      // The gamemode took its hook away (a reload) between the login and the
      // proof: nobody is left to say who this is, so nobody is handed over.
      this.log("Thornswood login: no onThornswoodLogin to ask about user " + userId + ", not handed over");
      return;
    }
    let answer: unknown;
    try {
      answer = hook({ userId, profileId: claimed, nonce, proof });
    } catch (e) {
      // Refusing is the secure side (Thornswood #937): a hook that throws
      // hands nobody over, and says why.
      this.log("Thornswood login: onThornswoodLogin threw for user " + userId + ", not handed over:", e);
      return;
    }
    const profileId = typeof answer === "number" ? answer : -1;
    if (profileId === 0) {
      this.log("Thornswood login: the gamemode refused user " + userId);
      return;
    }
    if (!(profileId > 0) || Math.floor(profileId) !== profileId) {
      return; // waiting for the proof
    }
    if (this.spawned.has(userId)) {
      return; // handed over at the login already; the proof was for the gamemode's record
    }
    if (!this.svr.isConnected(userId)) {
      return;
    }
    this.spawned.add(userId);
    this.spawn(userId, profileId);
  }

  private challenges = new Map<number, Challenge>();
  private spawned = new Set<number>();
}
