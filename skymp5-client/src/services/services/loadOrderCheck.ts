import { Mod } from "../messages_http/serverManifest";

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

export type LoadOrderEval =
  | { kind: "unreachable" }
  | { kind: "ok" }
  | { kind: "tooManyClientMods" }
  | { kind: "tooFewClientMods"; serverCount: number; clientCount: number }
  | { kind: "mismatch"; indices: number[] };

export function evaluateLoadOrder(
  clientMods: Mod[],
  serverMods: Mod[] | null,
): LoadOrderEval {
  if (serverMods === null) {
    return { kind: "unreachable" };
  }
  if (clientMods.length < serverMods.length) {
    return {
      kind: "tooFewClientMods",
      serverCount: serverMods.length,
      clientCount: clientMods.length,
    };
  }
  if (clientMods.length > serverMods.length) {
    return { kind: "tooManyClientMods" };
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
    return { kind: "mismatch", indices };
  }
  return { kind: "ok" };
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
