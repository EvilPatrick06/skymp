export interface Mod {
    filename: string;
    size: number;
    crc32: number;
};

export interface ServerManifest {
    versionMajor: number;
    // Full plugins in load order, as Game.getModName lists them
    mods: Mod[];
    // Light plugins in light index order, as Game.getLightModName lists them
    // (Thornswood #1715). Absent from a server built before the server loaded
    // light plugins, which has nothing to compare them with.
    light?: Mod[];
    loadOrder: string[];
};

// Both lists from the server's manifest
export interface ServerMods {
    mods: Mod[];
    light?: Mod[];
};
