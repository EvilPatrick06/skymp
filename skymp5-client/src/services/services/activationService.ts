import { ActivateEvent, Actor } from "skyrimPlatform";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { MsgType } from "../../messages";
import { getInventory } from "../../sync/inventory";

// TODO: refactor this out
import { localIdToRemoteId } from "../../view/worldViewMisc";

import { LastInvService } from "./lastInvService";
import { logError, logTrace } from "../../logging";

export class ActivationService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        this.controller.on("activate", (e) => this.onActivate(e));
    }

    private onActivate(e: ActivateEvent) {
        const lastInvService = this.controller.lookupListener(LastInvService);
        lastInvService.lastInv = getInventory(this.sp.Game.getPlayer() as Actor);

        let caster = e.caster ? e.caster.getFormID() : 0;
        let target = e.target ? e.target.getFormID() : 0;

        if (!target || !caster) {
          return;
        }

        // Actors never have non-ff ids locally in skymp
        if (caster !== 0x14 && caster < 0xff000000) {
          return;
        }

        target = localIdToRemoteId(target);
        if (!target) {
            logError(this, 'localIdToRemoteId returned 0 (target) in on(\'activate\')');
            return;
        }

        caster = localIdToRemoteId(caster);
        if (!caster) {
            logError(this, 'localIdToRemoteId returned 0 (caster) in on(\'activate\')');
            return;
        }

        const openState = e.target.getOpenState();

        // TODO: add this to skyrimPlatform.ts
        const enum OpenState {
            None,
            Open,
            Opening,
            Closed,
            Closing,
        }

        if (openState === OpenState.Opening || openState === OpenState.Closing) {
            logTrace(this, "Ignoring activation of door because it's already opening or closing");
            return;
        }

        /*
          THORNSWOOD PATCH. A locked door or chest is Skyrim's until it opens.

          dealWithRef leaves a locked reference unblocked on purpose so Skyrim
          runs its own handling and the lockpicking mini game appears. This
          function did not know that and sent the activation up anyway, and the
          server has no lock state at all, so its door branch simply opened the
          door. Both happened on the one press: the mini game came up and the
          door swung open behind it, and a load door pulled the person through.

          While it is locked the server is told nothing. The moment it is not,
          this sends as it always did and the server owns it again.
        */
        try {
            if (e.target.isLocked()) {
                logTrace(this, "Not announcing a locked reference; Skyrim has it until it is open");
                return;
            }
        } catch (_thornswoodLock) { }

        this.controller.emitter.emit("sendMessage", {
            message: {
                t: MsgType.Activate,
                data: { caster, target, isSecondActivation: false }
            },
            reliability: "reliable"
        });

        logTrace(this, `Sent activation for caster=`, caster.toString(16), `and target=`, target.toString(16));
    }
};
