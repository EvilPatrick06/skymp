import { ObjectReference, Actor, Game, FormType, TextureSet, NetImmerse } from "skyrimPlatform";
import { Inventory, applyInventory } from "../sync/inventory";
import { logError, logTrace } from "../logging";
import { SetNodeScaleEntry, SetNodeTextureSetEntry } from "src/services/messages/createActorMessage";

// For 0xff000000+ used from FormView
// For objects from master files used directly from remoteServer.ts
export class ModelApplyUtils {
  static applyModelInventory(refr: ObjectReference, inventory: Inventory) {
    applyInventory(refr, inventory, false, true);
  }

  static applyModelIsOpen(refr: ObjectReference, isOpen: boolean) {
    refr.setOpen(isOpen);

    // See also objectReferenceEx.ts
    const caveGSecretDoor01 = 0x6f703;

    // TODO: add more activators to support more cells
    const parentActivatorId = 0x460ca;

    if (refr.getBaseObject()?.getFormID() === caveGSecretDoor01) {
      const openOrOpening = [1, 2].includes(refr.getOpenState());
      if (openOrOpening) {
        if (!isOpen) {
          refr.activate(ObjectReference.from(Game.getForm(parentActivatorId)), false);
        }
      }
      if (!openOrOpening) {
        if (isOpen) {
          refr.activate(ObjectReference.from(Game.getForm(parentActivatorId)), false);
        }
      }
    }
  }

  static applyModelIsDisabled(refr: ObjectReference, disabled: boolean): void {
    const wasDisabled: boolean = refr.isDisabled();
    if (wasDisabled == disabled) {
      return;
    }

    if (disabled) {
      refr.disable(false);
    } else {
      refr.enable(true);
    }
  }

  /*
    THORNSWOOD PATCH (Thornswood #2328). Harvested and disabled are decided
    together for a reference from a master file.

    A tree or flora is harvested by its own flag, so for those the two are
    separate. Everything else is shown harvested by being disabled
    (applyModelIsHarvested below), so for those both flags are about the same
    one state. remoteServer.ts applied them one after the other: the harvested
    step saw a reference the server holds disabled (and this game already had
    disabled, from the last time it streamed) as harvested, and with
    isHarvested false it enabled it. Enable and Disable are latent (isLatent
    in skyrim-platform's FunctionsDump.txt, run later through the Papyrus VM,
    VmCall), so the disabled step then still read isDisabled() true, saw
    nothing to do, and the queued enable brought the reference back. The
    other way round did the same to a harvested item: the harvested step left
    it disabled and the disabled step, told false, enabled it.

    So for those references it is one decision: harvested goes through the
    harvested step, which disables it as before; otherwise the disabled flag
    alone says whether it is on. Each reference gets at most one enable or
    disable from here.
  */
  static applyModelIsHarvestedAndDisabled(refr: ObjectReference, isHarvested: boolean, isDisabled: boolean): void {
    if (!ModelApplyUtils.showsHarvestedByDisabled(refr)) {
      ModelApplyUtils.applyModelIsHarvested(refr, isHarvested);
      ModelApplyUtils.applyModelIsDisabled(refr, isDisabled);
      return;
    }
    if (isHarvested) {
      ModelApplyUtils.applyModelIsHarvested(refr, true);
      return;
    }
    ModelApplyUtils.applyModelIsDisabled(refr, isDisabled);
  }

  // Not a tree or flora: applyModelIsHarvested shows it harvested by disabling it.
  private static showsHarvestedByDisabled(refr: ObjectReference): boolean {
    const base = refr.getBaseObject();
    if (!base) {
      return false;
    }
    const t = base.getType();
    return t != FormType.Tree && t != FormType.Flora;
  }

  static applyModelIsHarvested(refr: ObjectReference, isHarvested: boolean) {
    const base = refr.getBaseObject();
    if (base) {
      const t = base.getType();
      if (t == FormType.Tree || t == FormType.Flora) {
        const wasHarvested = refr.isHarvested();
        if (isHarvested != wasHarvested) {
          let ac: Actor | null = null;
          if (isHarvested) {
            for (let i = 0; i < 20; ++i) {
              ac = Game.findRandomActor(
                refr.getPositionX(),
                refr.getPositionY(),
                refr.getPositionZ(),
                10000,
              );
              if (ac && ac.getFormID() !== 0x14) {
                break;
              }
            }
          }
          if (isHarvested && ac && ac.getFormID() !== 0x14) {
            refr.activate(ac, true);
          } else {
            refr.setHarvested(isHarvested);
            const id = refr.getFormID();
            refr.disable(false).then(() => {
              const restoredRefr = ObjectReference.from(Game.getFormEx(id));
              if (restoredRefr) {
                restoredRefr.enable(false);
              }
            });
          }
        }
      } else {
        const wasHarvested = refr.isDisabled();
        if (isHarvested != wasHarvested) {
          if (isHarvested) {
            const id = refr.getFormID();
            refr.disable(false).then(() => {
              const restoredRefr = ObjectReference.from(Game.getFormEx(id));
              if (restoredRefr && !restoredRefr.isDisabled()) {
                restoredRefr.delete();
                // Deletion takes time, so in practice this would be called a lot of times
              }
            });
          } else {
            refr.enable(true);
          }
        }
      }
    }
  }

  static applyModelNodeTextureSet(refr: ObjectReference, setNodeTextureSet?: SetNodeTextureSetEntry[]) {
    if (setNodeTextureSet) {
      setNodeTextureSet.forEach(element => {
        const firstPerson = false;

        const textureSet = TextureSet.from(Game.getFormEx(element.textureSetId));
        if (textureSet !== null) {
          NetImmerse.setNodeTextureSet(refr, element.nodeName, textureSet, firstPerson);
          logTrace("ModelApplyUtils", refr.getFormID().toString(16), `Applied texture set`, element.textureSetId.toString(16), `to`, element.nodeName);
        } else {
          logError("ModelApplyUtils", refr.getFormID().toString(16), `Failed to apply texture set`, element.textureSetId.toString(16), `to`, element.nodeName);
        }
      });
    }
  }

  static applyModelNodeScale(refr: ObjectReference, setNodeScale?: SetNodeScaleEntry[]) {
    if (setNodeScale) {
      setNodeScale.forEach(element => {
        const firstPerson = false;
        NetImmerse.setNodeScale(refr, element.nodeName, element.scale, firstPerson);
        logTrace("ModelApplyUtils", refr.getFormID().toString(16), `Applied node scale`, element.scale, `to`, element.nodeName);
      });
    }
  }
}
