// TODO: refactor this out
import { localIdToRemoteId } from "../../view/worldViewMisc";

import { Actor, ContainerChangedEvent } from "skyrimPlatform";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { Inventory } from "../../sync/inventory";
import { MsgType } from "../../messages";
import { logTrace, logError } from "../../logging";

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
            if (e.name === CRAFTING_MENU) { this.furnitureStreak.clear(); }
        });
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

                this.controller.emitter.emit("sendMessage", {
                    message: {
                        t: MsgType.CraftItem,
                        data: { workbench, craftInputObjects, resultObjectId },
                    },
                    reliability: "reliable"
                });
            }
        }
    }

    private furnitureStreak = new Map<FurnitureId, Inventory>();
}
