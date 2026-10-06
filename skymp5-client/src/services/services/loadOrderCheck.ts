import { Mod, ServerMods } from "../messages_http/serverManifest";

export type ScreenNotice = {
  text: string;
  color: [number, number, number, number];
  clearDelay?: number;
};

// When getServerMods could not fetch a list after retries. Not a mismatch:
// the server never answered, so the check did not run.
export function noticeServerModsUnreachable(): ScreenNotice {
  return {
    text:
      "The server did not answer.\nLoad order could not be checked.\nCheck console for details.",
    color: [255, 255, 0, 1],
    clearDelay: 5,
  };
}

export function noticeTooManyClientMods(): ScreenNotice {
  return {
    text:
      "LOAD ORDER WARNING: you have more mods than server!\nCheck console for details.",
    color: [255, 255, 0, 1],
    clearDelay: 5,
  };
}

export function noticeLoadOrderMismatch(ignoreMismatch: boolean): ScreenNotice {
  if (ignoreMismatch) {
    return {
      text:
        "LOAD ORDER ERROR!\nHowever, ignoring it because of ignoreLoadOrderMismatch being set." +
        "\nExpect EVERYTHING BREAK, unless you know what you are doing.\nCheck console for details." +
        "\nThis message will disappear after 30 seconds.",
      color: [255, 0, 0, 1],
      clearDelay: 30,
    };
  }
  return {
    text: "LOAD ORDER ERROR!\nCheck console for details.",
    color: [255, 0, 0, 1],
  };
}

// Which of the game's two plugin lists a result is about. The game numbers
// full plugins (Game.getModName) and light plugins (Game.getLightModName)
// apart, and a form names its plugin by its place in one of them, so each
// list is compared with the server's position by position on its own
// (Thornswood #1715).
export type PluginList = "full" | "light";

export type LoadOrderEval =
  | { kind: "unreachable" }
  | { kind: "ok" }
  | { kind: "tooManyClientMods"; list: PluginList }
  | {
      kind: "tooFewClientMods";
      list: PluginList;
      serverCount: number;
      clientCount: number;
    }
  | { kind: "mismatch"; list: PluginList; indices: number[] };

export interface ClientMods {
  mods: Mod[];
  light: Mod[];
}

function evaluateList(
  list: PluginList,
  clientMods: Mod[],
  serverMods: Mod[],
): LoadOrderEval {
  if (clientMods.length < serverMods.length) {
    return {
      kind: "tooFewClientMods",
      list,
      serverCount: serverMods.length,
      clientCount: clientMods.length,
    };
  }
  if (clientMods.length > serverMods.length) {
    return { kind: "tooManyClientMods", list };
  }
  const indices: number[] = [];
  for (let i = 0; i < serverMods.length; ++i) {
    if (
      clientMods[i].filename.toLowerCase() !==
        serverMods[i].filename.toLowerCase() ||
      clientMods[i].size !== serverMods[i].size ||
      clientMods[i].crc32 !== serverMods[i].crc32
    ) {
      indices.push(i);
    }
  }
  if (indices.length !== 0) {
    return { kind: "mismatch", list, indices };
  }
  return { kind: "ok" };
}

// The full lists first, then the light ones. A server whose manifest has no
// light list was built before the server loaded light plugins and has none
// to compare with.
export function evaluateLoadOrder(
  clientMods: ClientMods,
  serverMods: ServerMods | null,
): LoadOrderEval {
  if (serverMods === null) {
    return { kind: "unreachable" };
  }
  const full = evaluateList("full", clientMods.mods, serverMods.mods);
  if (full.kind !== "ok" || serverMods.light === undefined) {
    return full;
  }
  return evaluateList("light", clientMods.light, serverMods.light);
}

// Screen text for a completed evaluation. Null when nothing should be shown.
export function screenNoticeForEval(
  result: LoadOrderEval,
  ignoreMismatch: boolean,
): ScreenNotice | null {
  switch (result.kind) {
    case "unreachable":
      return noticeServerModsUnreachable();
    case "tooManyClientMods":
      return noticeTooManyClientMods();
    case "tooFewClientMods":
    case "mismatch":
      return noticeLoadOrderMismatch(ignoreMismatch);
    case "ok":
      return null;
  }
}
