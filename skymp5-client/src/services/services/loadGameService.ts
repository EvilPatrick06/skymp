import { ClientListener, CombinedController, Sp } from "./clientListener";
import { Inventory } from "../../sync/inventory";
import { ChangeFormNpc } from "skyrimPlatform";
import { logError } from "../../logging";

export class LoadGameService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        this.controller.on("loadGame", () => this.onLoadGame());
    }

    public loadGame(pos: number[], rot: number[], worldOrCell: number, changeFormNpc?: ChangeFormNpc, loadOrder?: string[], time?: { seconds: number, minutes: number, hours: number }, inventory?: Inventory, lightLoadOrder?: string[]) {
        // THORNSWOOD PATCH. The flag goes up before the call, not after.
        //
        // sp.loadGame does not return and then load later. The load happens
        // inside it, and the gameLoad event can come back out of it before
        // the next line runs, which read as a load nobody asked for and took
        // the session down with it.
        this._isCausedBySkyrimPlatform = true;
        try {
            // @ts-ignore
            this.sp.loadGame(pos, rot, worldOrCell, changeFormNpc, loadOrder, time, inventory, lightLoadOrder);
        } catch (e) {
            /*
              THORNSWOOD PATCH (#1994). A refused save stops the load and says why.

              Upstream's "Hotfix non-vanilla headparts" caught this and loaded
              again with no appearance at all, so the character arrived with
              the template's face, race and gear and nothing said why. The
              refusal comes from building the save, before the game loads
              anything: SaveFile's PluginRemap names the form and its plugin
              (a light form in a save with no light list, a plugin past the
              end of the game's lists), and LoadGameApi checks the starting
              pack. Since fork PR #36 the template is a Special Edition save
              with a light plugin list, so a face from a light plugin is named
              and none of that is refused for a game whose load order matches
              the server's.

              Nothing is loaded instead. The front takes the reason from
              __thornswoodLoadRefused, closes the connection and brings the
              character choice back with a sentence over it; the error goes on
              up so this handler does nothing more.
            */
            this._isCausedBySkyrimPlatform = false;
            const why = String((e && (e as Error).message) || e);
            logError(this, 'loadGame refused the save, so nothing loads:', why);
            (globalThis as any).__thornswoodLoadRefused = why;
            throw e;
        }
    }

    private onLoadGame() {
        try {
            const gameLoadEvent = {
                isCausedBySkyrimPlatform: this._isCausedBySkyrimPlatform
            };
            this.controller.emitter.emit("gameLoad", gameLoadEvent);
        } catch (e) {
            this.controller.once("tick", () => {
                this._isCausedBySkyrimPlatform = false;
            });
            throw e;
        }
        this.controller.once("tick", () => {
            this._isCausedBySkyrimPlatform = false;
        });
    }

    private _isCausedBySkyrimPlatform = false;
}
