// Compat with pre-SP3 where storage is a Proxy, like Actor/ObjectReference/etc.
// The user code, including skymp5-client and skymp5-gamemode, relies on
// typeof storage[x] === "function" to determine whether the key x is set.
//
// THORNSWOOD PATCH (Thornswood #3333). One storage for every plugin, kept
// across hot reloads, as before SP3. This runs once for every exports object
// SkyrimPlatform makes, and it makes one for each plugin file it loads
// (LoadPluginFile, addNativeExports), so a Proxy made here each time gave
// every plugin its own empty storage: nothing skymp5-client stored (view,
// idTextNickname, ownerOutfitDiagnostic, thornswoodConnectionDenied) reached
// another plugin, and nothing another plugin stored (thornswoodJoinPass,
// authGameData) reached the client. The plugins share one JavaScript
// environment, so the one storage is kept on its global under a registered
// symbol and every exports object gets that same one.
(sp) => {
  const key = Symbol.for('skyrimPlatform.storage');
  if (!globalThis[key]) {
    globalThis[key] = new Proxy({}, {
      get(target, prop) {
        if (prop in target) {
          return target[prop];
        }

        // Safety: Return undefined for specific symbols or 'then' to prevent
        // the Proxy from breaking async flows or inspection tools.
        // This is a breaking change from pre-SP3 behavior but improves safety.
        if (prop === 'then' || prop === 'toJSON' || typeof prop === 'symbol') {
          return undefined;
        }

        return () => { throw new Error(`Property "${prop}" is not defined in storage`); };
      },

      set(target, prop, value) {
        target[prop] = value;
        return true;
      }
    });
  }
  sp.storage = globalThis[key];
};
