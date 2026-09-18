import { ClientListener, CombinedController, Sp } from "./clientListener";
import { GameLoadEvent } from "../events/gameLoadEvent";
import { NetworkingService } from "./networkingService";

export class SinglePlayerService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        this.controller.emitter.on("gameLoad", (e) => this.onGameLoad(e));
    }

    get isSinglePlayer() {
        return this._isSinglePlayer;
    }

    private onGameLoad(event: GameLoadEvent) {
        // THORNSWOOD PATCH. The switch to single player is gone.
        //
        // What used to be here shut the connection and put a modal on the
        // screen the moment a save was loaded that SkyMP had not asked for.
        // That reads as a guard against somebody loading an old save mid
        // session. On this install it fired on every single launch instead,
        // because po3_StartOnSave loads the newest save at startup, which is
        // the only way there is to start the game already in a save. That
        // load is not caused by SkyrimPlatform, so the guard could not tell
        // it apart from the thing it was written to catch.
        //
        // The cost was the whole session: a message box that swallowed every
        // key for about thirty five seconds, and behind it a closed socket,
        // which is why the character stood there with its fists out and
        // nothing on the server ever put them away.
        //
        // Nothing is switched off now. The count is left where the front
        // plugin can read it, so the log still says it happened.
        if (!event.isCausedBySkyrimPlatform && !this._isSinglePlayer) {
            try {
                (globalThis as any).__thornswoodUncausedLoad =
                    ((globalThis as any).__thornswoodUncausedLoad || 0) + 1;
            } catch (e) { }
        }
    }

    private _isSinglePlayer = false;
}
