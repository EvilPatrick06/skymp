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
  mods: Array<ManifestModEntry>;
  loadOrder: Array<string>;
}

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
    manifest.mods.push({
      crc32: crc32.buf(buf),
      filename: espmName,
      size: buf.length,
    });

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
