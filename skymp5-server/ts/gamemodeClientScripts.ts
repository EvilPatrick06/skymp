import * as crypto from "crypto";

// What a gamemode gives the games connected to it to run: each property's
// update scripts and each event source's body (Thornswood #3196).
//
// A gamemode loaded again in a running server (a hot reload, which is how
// Thornswood's gamemode-only dev deploys go) takes effect on the server at
// once, but with enableGamemodeDataUpdatesBroadcast off, as it is by default
// and on Thornswood's servers, the games already connected keep running the
// scripts they were sent when they joined. Only a new login brings the new
// ones. So after every load the server says what those scripts are, by
// digest, and whether a reload changed them, and the gamemode is told so it
// can ask the people on to log in again when it did.

export interface GamemodeLoad {
  digest: string;
  scripts: number;
  reload: boolean;
  clientScriptsChanged: boolean;
}

export interface ClientScriptsRecorder {
  // The server's clear, and the record's with it.
  clear(): void;
  // After a gamemode finished loading. A load that threw is not recorded:
  // what the next one is compared with stays the last one that loaded.
  loaded(): GamemodeLoad;
}

export const recordClientScripts = (server: any): ClientScriptsRecorder => {
  const scripts = new Map<string, string>();
  const makeProperty = server.makeProperty;
  const makeEventSource = server.makeEventSource;
  // Recorded only once the server took them, so a refused one is not.
  server.makeProperty = (name: unknown, info: any) => {
    const result = makeProperty.call(server, name, info);
    scripts.set("property " + String(name), JSON.stringify([
      !!(info && info.isVisibleByOwner), !!(info && info.isVisibleByNeighbors),
      String((info && info.updateOwner) ?? ""), String((info && info.updateNeighbor) ?? ""),
    ]));
    return result;
  };
  server.makeEventSource = (name: unknown, functionBody: unknown) => {
    const result = makeEventSource.call(server, name, functionBody);
    scripts.set("event source " + String(name), String(functionBody));
    return result;
  };

  let loads = 0;
  let last = "";
  return {
    clear() {
      server.clear();
      scripts.clear();
    },
    loaded() {
      const hash = crypto.createHash("sha256");
      for (const key of [...scripts.keys()].sort()) {
        hash.update(key).update("\0").update(scripts.get(key) as string).update("\0");
      }
      const digest = hash.digest("hex").slice(0, 12);
      const reload = loads++ > 0;
      const clientScriptsChanged = reload && digest !== last;
      last = digest;
      return { digest, scripts: scripts.size, reload, clientScriptsChanged };
    },
  };
};
