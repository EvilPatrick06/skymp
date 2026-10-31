// TODO: refactor this out
import { localIdToRemoteId } from "../../view/worldViewMisc";

import { Actor, ContainerChangedEvent } from "skyrimPlatform";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { Inventory, Made, ensureMadePotion, readMadePotion } from "../../sync/inventory";
import { MsgType } from "../../messages";
import { logTrace, logError } from "../../logging";
import { ConnectionMessage } from "../events/connectionMessage";
import { CustomPacketMessage } from "../messages/customPacketMessage";

type FurnitureId = number;

// THORNSWOOD PATCH. Every craft in Skyrim, the forge, the tanning rack, the
// smelter, cooking, alchemy and enchanting, happens inside this one menu.
const CRAFTING_MENU = 'Crafting Menu';

export class CraftService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        controller.on('containerChanged', (e) => this.onContainerChanged(e));
        /*
          THORNSWOOD PATCH. A craft is only what happens while the crafting
          menu is open, and every time it opens or closes the streak starts
          again.

          Thornswood #523, "Made a helmet twice only got one". The streak was
          every item that left the pack while seated at any furniture, kept
          until a new item arrived, and nothing ever cleared it. On the dev
          server on 23 September a helmet crafted just after a respawn went up
          with thirteen input entries, sixteen iron ingots among them, where
          the recipe wants three: what was left over from before the respawn.
          No recipe matched, the server refused it, and the helmet the person
          saw was taken back.

          And the server's own inventory correction was read as crafting: an
          item it took away while somebody sat at a bench went into the
          streak, and an item it gave back closed the streak into a craft of
          that item. remoteServer defers applying inventory while this menu is
          open (isBadMenuShown), so a correction lands when it is closed, and
          counting only changes made while it is open leaves corrections out.
        */
        controller.on('menuOpen', (e) => {
            if (e.name === CRAFTING_MENU) { this.furnitureStreak.clear(); }
        });
        controller.on('menuClose', (e) => {
            if (e.name === CRAFTING_MENU) {
                this.furnitureStreak.clear();
                this.leaveBench();
            }
        });
        controller.emitter.on("customPacketMessage", (e) => this.onCustomPacketMessage(e));
    }

    /*
      THORNSWOOD PATCH (Thornswood #3161). A CRAFT TAKES TIME AT THE BENCH.

      Patrick, 22 Oct 2026: small 1 s, medium 5 s, large 15 s, with an
      animation, and moving away stops it. The game puts the made item in the
      pack the moment craft is pressed, so this takes it straight back out
      (hide) and the server holds the craft for its time while the bench's
      own working animation plays behind its menu. When the time is up the
      server makes the craft and says so with a "craftMade" packet, and the
      item goes back in here, while the menu is still open, so it can be
      worked again at the same bench (leather into strips at the tanning
      rack). Closing the menu is getting up from the bench, which stops the
      work: "craftLeave" tells the server (CraftService::StopWork), and the
      server's inventory, applied once the menu is closed, puts back every
      input it never took.

      The item hidden and the item put back are this service's own changes,
      not crafting, so they are left out of the streak (ownChanges) the way
      the server's corrections are. Only the base and count come back here;
      the entry's name or quality, which only the server knows, arrives with
      the server's inventory once the menu closes.
    */
    private hide(baseId: number, count: number) {
        const form = this.sp.Game.getFormEx(baseId);
        const pc = this.sp.Game.getPlayer();
        if (!form || !pc) {
            logError(this, `Could not hide the made item`, baseId.toString(16), `until the server makes it`);
            return;
        }
        this.ownChanges.push({ baseId, count, out: true });
        pc.removeItem(form, count, true, null);
        this.awaitingMade++;
    }

    private onCustomPacketMessage(event: ConnectionMessage<CustomPacketMessage>) {
        let content: Record<string, unknown> = {};
        try {
            content = JSON.parse(event.message.contentJsonDump);
        } catch (e) {
            if (e instanceof SyntaxError) {
                logError(this, "onCustomPacketMessage failed to parse JSON", e.message, "json:", event.message.contentJsonDump);
                return;
            }
            throw e;
        }
        if (content["customPacketType"] !== "craftMade") {
            return;
        }
        const baseId = content["baseId"];
        const count = content["count"];
        if (typeof baseId !== "number" || typeof count !== "number" || count <= 0) {
            logError(this, "craftMade expected a baseId and a count, got", event.message.contentJsonDump);
            return;
        }
        if (this.awaitingMade > 0) { this.awaitingMade--; }
        // With the menu closed the server's own inventory is applied as it
        // always is, and it already holds this.
        if (!this.sp.Ui.isMenuOpen(CRAFTING_MENU)) {
            return;
        }
        // Thornswood #1649: a brew is kept by what is in it, under a made id
        // (Inventory::MadeBaseId on the server) and not the session's bottle
        // that was hidden, so that bottle is built under the server's id
        // before it goes in, as applyInventory builds one.
        const made = content["made"];
        if (made !== undefined && !ensureMadePotion(baseId, made as Made)) {
            logError(this, `Could not build the brewed bottle`, baseId.toString(16), `before the menu closes`);
            return;
        }
        const form = this.sp.Game.getFormEx(baseId);
        const pc = this.sp.Game.getPlayer();
        if (!form || !pc) {
            logError(this, `Could not put back the made item`, baseId.toString(16), `before the menu closes`);
            return;
        }
        this.ownChanges.push({ baseId, count, out: false });
        pc.addItem(form, count, true);
    }

    private leaveBench() {
        this.ownChanges = [];
        if (this.awaitingMade <= 0) {
            return;
        }
        this.awaitingMade = 0;
        this.controller.emitter.emit("sendMessage", {
            message: {
                t: MsgType.CustomPacket,
                contentJsonDump: JSON.stringify({ customPacketType: "craftLeave" }),
            },
            reliability: "reliable"
        });
    }

    // True, and forgotten, when this change is one this service made itself.
    private isOwnChange(baseId: number, count: number, out: boolean): boolean {
        const i = this.ownChanges.findIndex((c) => c.baseId === baseId && c.count === count && c.out === out);
        if (i < 0) {
            return false;
        }
        this.ownChanges.splice(i, 1);
        return true;
    }

    private onContainerChanged(e: ContainerChangedEvent) {
        const oldContainerId = e.oldContainer ? e.oldContainer.getFormID() : 0;
        const newContainerId = e.newContainer ? e.newContainer.getFormID() : 0;
        const baseObjId = e.baseObj ? e.baseObj.getFormID() : 0;
        if (oldContainerId !== 0x14 && newContainerId !== 0x14) {
          return;
        }

        // See the constructor: outside the crafting menu nothing is a craft.
        if (!this.sp.Ui.isMenuOpen(CRAFTING_MENU)) {
          return;
        }

        const furnitureRef = (this.sp.Game.getPlayer() as Actor).getFurnitureReference();
        if (!furnitureRef) {
          return;
        }

        const furnitureId = furnitureRef.getFormID();

        // Thornswood #3161: the made item hidden or put back. See hide.
        const out = oldContainerId === 0x14 && newContainerId === 0;
        if (this.isOwnChange(baseObjId, e.numItems, out)) {
            return;
        }

        if (oldContainerId === 0x14 && newContainerId === 0) {
            let craftInputObjects = this.furnitureStreak.get(furnitureId);
            if (!craftInputObjects) {
                craftInputObjects = { entries: [] };
            }
            craftInputObjects.entries.push({
                baseId: baseObjId,
                count: e.numItems,
            });
            this.furnitureStreak.set(furnitureId, craftInputObjects);
            logTrace(this,
                `Adding baseObjId`, baseObjId.toString(16), `numItems`, e.numItems, `to craft`,
            );
        } else if (oldContainerId === 0 && newContainerId === 0x14) {
            logTrace(this, 'Finishing craft');
            const craftInputObjects = this.furnitureStreak.get(furnitureId);
            if (craftInputObjects && craftInputObjects.entries.length) {
                this.furnitureStreak.delete(furnitureId);
                const workbench = localIdToRemoteId(furnitureId);
                if (!workbench) {
                    logError(this, `localIdToRemoteId returned 0 for furnitureId`, furnitureId);
                    return;
                }

                const resultObjectId = baseObjId;

                logTrace(this, `Sending craft workbench`, workbench, `resultObjectId`, resultObjectId, `craftInputObjects`, JSON.stringify(craftInputObjects.entries));

                // THORNSWOOD PATCH (Thornswood #1649). A bottle the game
                // brewed is only good for this session, so what is in it
                // goes along and the server keeps it by that.
                const resultPotion = readMadePotion(e.baseObj);

                this.controller.emitter.emit("sendMessage", {
                    message: {
                        t: MsgType.CraftItem,
                        data: { workbench, craftInputObjects, resultObjectId, resultPotion },
                    },
                    reliability: "reliable"
                });

                // Thornswood #3161: out of the pack until the server makes it.
                this.hide(resultObjectId, e.numItems);
            }
        }
    }

    private furnitureStreak = new Map<FurnitureId, Inventory>();
    // Thornswood #3161. Crafts sent and not yet made, this time at the bench.
    private awaitingMade = 0;
    // Thornswood #3161. Changes to the pack this service made itself and has
    // not yet seen come back as events.
    private ownChanges: { baseId: number, count: number, out: boolean }[] = [];
}
