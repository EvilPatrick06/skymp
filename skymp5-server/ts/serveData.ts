import * as fs from "fs";
import * as path from "path";
const resolvePath = require("resolve-path");

// The resources port's file serving: each file answered from the one handle
// it opened (Thornswood #2740).
//
// This was koa-static, serve("data"), and koa-send under it stats the path,
// sets Content-Length from that stat, and then opens the path again by name
// to stream it. A file replaced between the two, which the gamemode does to
// online.json whenever the count moves, went out with the old file's length
// and the new file's bytes: measured on #2740 with the file replaced whole
// every 20 ms, 38 of 891 HTTP answers were bad while no read of the file
// itself was. So a file is opened once, its size and time are read from that
// handle, and that handle is streamed, never more bytes than the size said.
// A file replaced after the open is the next request's; this one is the file
// that was opened, whole.
//
// It also refuses the game's own files (isRefused, below). Everything else
// is koa-static's behaviour with the options serve("data") had (koa-static
// 5.0.0, koa-send 5.0.1): GET and HEAD only; the path
// decoded, 400 when it does not decode; resolve-path's refusals, the same
// library koa-send uses, so nothing outside the folder; nothing whose path
// has a part starting with a dot; index.html for a path ending in a slash,
// and for a folder; a .br or .gz beside the file when the caller takes that
// encoding; Content-Type from the extension; Last-Modified; Cache-Control
// max-age=0; anything not found passes on to the next middleware.

const notFound = ["ENOENT", "ENAMETOOLONG", "ENOTDIR"];

type Opened = { handle: fs.promises.FileHandle; stats: fs.Stats };

// The file at full, opened, with its stats from the handle. null when there
// is none; "folder" when it is a folder.
const openFile = async (full: string): Promise<Opened | "folder" | null> => {
  let handle: fs.promises.FileHandle;
  try {
    handle = await fs.promises.open(full, "r");
  } catch (err: any) {
    if (err && err.code === "EISDIR") {
      return "folder";
    }
    if (err && notFound.includes(err.code)) {
      return null;
    }
    err.status = 500;
    throw err;
  }
  try {
    const stats = await handle.stat();
    if (stats.isDirectory()) {
      await handle.close();
      return "folder";
    }
    return { handle, stats };
  } catch (err: any) {
    await handle.close();
    err.status = 500;
    throw err;
  }
};

const isHidden = (root: string, full: string): boolean =>
  full.substr(root.length).split(path.sep).some((part) => part[0] === ".");

// Never served: the game's masters and plugins, its archives, and anything
// under a scripts folder (Thornswood #3927). This was three koa-router routes
// in ui.ts matched against the URL as it came, escaped and case and all, so
// /Skyrim.%65sm, /Data.%62sa and /%73cripts/x.pex went past them to the
// files, and on Windows, the box, so did /Skyrim.ESM. It is checked here,
// on the path this opens, after decoding. Each part is read the way Windows
// opens it, so a part is the same name in any case, without a stream after a
// colon, and without dots and spaces at its end.
const refusedExtensions = [".esm", ".esp", ".esl", ".bsa"];
const asOpened = (part: string): string =>
  part.split(":")[0].replace(/[. ]+$/, "").toLowerCase();
const isRefused = (root: string, full: string): boolean => {
  const parts = full.substr(root.length).split(path.sep).filter((part) => part !== "").map(asOpened);
  return parts.includes("scripts") ||
    refusedExtensions.includes(path.extname(parts[parts.length - 1] || ""));
};

const send = async (ctx: any, root: string): Promise<boolean> => {
  let requested: string = ctx.path;
  const trailingSlash = requested[requested.length - 1] === "/";
  requested = requested.substr(path.parse(requested).root.length);
  try {
    requested = decodeURIComponent(requested);
  } catch (err) {
    ctx.throw(400, "failed to decode");
  }
  if (trailingSlash) {
    requested += "index.html";
  }
  let full: string = resolvePath(root, requested);
  if (isHidden(root, full)) {
    return false;
  }
  if (isRefused(root, full)) {
    ctx.throw(403);
  }

  let opened: Opened | "folder" | null = null;
  let encodingExt = "";
  if (ctx.acceptsEncodings("br", "identity") === "br") {
    opened = await openFile(full + ".br");
    if (opened === "folder") {
      opened = null;
    }
    if (opened) {
      encodingExt = ".br";
    }
  }
  if (!opened && ctx.acceptsEncodings("gzip", "identity") === "gzip") {
    opened = await openFile(full + ".gz");
    if (opened === "folder") {
      opened = null;
    }
    if (opened) {
      encodingExt = ".gz";
    }
  }
  if (!opened) {
    opened = await openFile(full);
    if (opened === "folder") {
      full = full + "/index.html";
      opened = await openFile(full);
    }
  }
  if (!opened || opened === "folder") {
    return false;
  }

  const { handle, stats } = opened;
  if (encodingExt) {
    ctx.set("Content-Encoding", encodingExt === ".br" ? "br" : "gzip");
  }
  ctx.set("Content-Length", String(stats.size));
  if (!ctx.response.get("Last-Modified")) {
    ctx.set("Last-Modified", stats.mtime.toUTCString());
  }
  if (!ctx.response.get("Cache-Control")) {
    ctx.set("Cache-Control", "max-age=0");
  }
  if (!ctx.type) {
    ctx.type = path.extname(full);
  }
  if (stats.size === 0) {
    await handle.close();
    ctx.body = Buffer.alloc(0);
  } else {
    // Closed when the stream ends, or when koa destroys it because the
    // caller went away.
    ctx.body = handle.createReadStream({ start: 0, end: stats.size - 1, autoClose: true });
  }
  return true;
};

export const serveFromOneHandle = (folder: string) => {
  const root = path.normalize(path.resolve(folder));
  return async (ctx: any, next: any) => {
    let done = false;
    if (ctx.method === "HEAD" || ctx.method === "GET") {
      done = await send(ctx, root);
    }
    if (!done) {
      await next();
    }
  };
};
