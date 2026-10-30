import * as fs from "fs";
import * as path from "path";
import * as os from "os";

import { ClientScriptsRecorder } from "./gamemodeClientScripts";

// Loading the gamemode, at the start and at every change of the file. Out of
// index.ts so it can be tested on its own (Thornswood #3196).

const gamemodeCache = new Map<string, string>();

// True once the gamemode's top level ran to the end, false when it threw.
function requireTemp(module: string): boolean {
  // https://blog.mastykarz.nl/create-temp-directory-app-node-js/
  let tmpDir;
  const appPrefix = 'skymp5-server';
  try {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), appPrefix));

    const contents = fs.readFileSync(module, 'utf8');
    const tempPath = path.join(tmpDir, Math.random() + '-' + Date.now() + '.js');
    fs.writeFileSync(tempPath, contents);

    require(tempPath);
    return true;
  } catch (e) {
    console.error(e.stack);
    return false;
  } finally {
    try {
      if (tmpDir) {
        fs.rmSync(tmpDir, { recursive: true });
      }
    } catch (e) {
      console.error(`An error has occurred while removing the temp folder at ${tmpDir}. Please remove it manually. Error: ${e}`);
    }
  }
}

export function requireUncached(
  module: string,
  recorder: ClientScriptsRecorder,
  server: any
): void {
  let gamemodeContents = fs.readFileSync(require.resolve(module), "utf8");

  // Reload gamemode.js only if there are real changes
  const gamemodeContentsOld = gamemodeCache.get(module);
  if (gamemodeContentsOld !== gamemodeContents) {
    gamemodeCache.set(module, gamemodeContents);

    while (1) {
      try {
        recorder.clear();

        // In native module we now register mp-api methods into the ScampServer class
        // This workaround allows code that is bound to global 'mp' object to run
        // @ts-ignore
        globalThis.mp = globalThis.mp || server;
        // Each load sets its own; one an earlier load left must not answer
        // for a gamemode that no longer has it.
        delete (globalThis as { __thornswoodGamemodeLoaded?: unknown }).__thornswoodGamemodeLoaded;

        if (!requireTemp(module)) {
          // Thornswood #3196: a deploy that reloads the gamemode waits for
          // this line or the next one to know how the load went.
          console.log("[thornswood] gamemode did not load; the error is above");
          return;
        }
        const load = recorder.loaded();
        console.log(`[thornswood] gamemode client scripts ${load.digest} (${load.scripts}), ` +
          (!load.reload ? "the first load in this process" :
            load.clientScriptsChanged ? "changed since the last load" : "unchanged since the last load"));
        const loaded = (globalThis as { __thornswoodGamemodeLoaded?: (load: object) => void }).__thornswoodGamemodeLoaded;
        if (typeof loaded === "function") {
          try {
            loaded({ reload: load.reload, clientScriptsChanged: load.clientScriptsChanged });
          } catch (e) {
            console.error(`in the gamemode's __thornswoodGamemodeLoaded:\n${e.stack}`);
          }
        }
        return;
      } catch (e) {
        if (`${e}`.indexOf("'JsRun' returned error 0x30002") === -1) {
          throw e;
        } else {
          console.log("Bad syntax, ignoring");
          return;
        }
      }
    }
  }
}
