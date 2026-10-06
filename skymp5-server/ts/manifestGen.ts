import { Settings } from "./settings";
import * as crc32 from "crc-32";
import * as path from "path";
import * as fs from "fs";

interface ManifestModEntry {
  filename: string;
  crc32: number;
  size: number;
}

interface Manifest {
  versionMajor: number;
  // Full plugins in load order, each followed by its archive when dataDir has
  // one. The client compares this with Game.getModName.
  mods: Array<ManifestModEntry>;
  // Light plugins in light index order, compared with Game.getLightModName,
  // plugins only. The game numbers light plugins apart from full ones
  // (Thornswood #1715), so they are a list of their own: in mods every light
  // plugin would shift the full plugins after it.
  light: Array<ManifestModEntry>;
  loadOrder: Array<string>;
}

// The game's rule for what is light, the same one the server applies when it
// loads the plugins (espm::IsLightPlugin in libespm/LoadOrder.h): the ESL
// flag, 0x200, in the TES4 record header, or an .esl extension. The header's
// flags are the four bytes after the record type and size.
export const isLightPlugin = (fileName: string, content: Uint8Array): boolean => {
  if (/\.esl$/i.test(fileName)) {
    return true;
  }
  if (content.length < 12 || String.fromCharCode(...content.subarray(0, 4)) !== "TES4") {
    return false;
  }
  const flags = content[8] | (content[9] << 8) | (content[10] << 16) | (content[11] << 24);
  return (flags & 0x200) !== 0;
};

// An .esl is a plugin like any other and can ship its own .bsa. Returning null
// for anything else keeps a stray filename from taking the whole server down
// during startup, which is what throwing here used to do.
const getBsaNameByEspmName = (espmName: string): string | null => {
  if (/\.(esp|esm|esl)$/i.test(espmName)) {
    return espmName.replace(/\.[^.]+$/, "") + ".bsa";
  }
  return null;
};

export const generateManifest = (settings: Settings): void => {
  const manifest: Manifest = {
    mods: [],
    light: [],
    versionMajor: 1,
    loadOrder: settings.loadOrder.map(x => path.basename(x)),
  };

  settings.loadOrder.forEach((loadOrderElement) => {
    const espmName = path.isAbsolute(loadOrderElement)
      ? path.basename(loadOrderElement)
      : loadOrderElement;

    const espmPath = path.isAbsolute(loadOrderElement)
      ? loadOrderElement
      : path.join(settings.dataDir, espmName);

    if (!fs.existsSync(espmPath)) {
      console.error(`generateManifest: '${espmName}' is in the load order but not on disk, skipping`);
      return;
    }

    const buf: Uint8Array = fs.readFileSync(espmPath);
    const entry = {
      crc32: crc32.buf(buf),
      filename: espmName,
      size: buf.length,
    };
    if (isLightPlugin(espmName, buf)) {
      // The client compares this list position by position with
      // Game.getLightModName, which names plugins only, so a light plugin's
      // archive is not listed after it the way a full plugin's is in mods.
      manifest.light.push(entry);
      return;
    }
    manifest.mods.push(entry);

    const bsaName = getBsaNameByEspmName(espmName);
    if (!bsaName) {
      console.error(`generateManifest: no plugin extension on '${espmName}', not looking for a bsa`);
      return;
    }
    // Deliberately dataDir, not the plugin's own folder. loadOrderVerification
    // on the client compares this array POSITIONALLY against its own list from
    // Game.getModName, which is plugins only. Every bsa pushed in here shifts
    // the entries after it and makes the client report load order mismatches
    // that are not real. Resolving archives next to their plugins finds all of
    // them and breaks that comparison for the whole list.
    const bsaPath = path.join(settings.dataDir, bsaName);
    if (fs.existsSync(bsaPath)) {
      const buf: Uint8Array = fs.readFileSync(bsaPath);
      manifest.mods.push({
        crc32: crc32.buf(buf),
        filename: bsaName,
        size: buf.length,
      });
    }
  });

  const manifestPath = path.join(settings.dataDir, "manifest.json");
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 4));
};
