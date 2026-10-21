import { Flora, Form, FormType, MotionType, ObjectReference } from "skyrimPlatform";
import * as sp from "skyrimPlatform";
import { NiPoint3 } from "../sync/movement";
import { FormTypeEx } from "./formTypeEx";

export class ObjectReferenceEx {
  static getWorldOrCell(self: ObjectReference): number {
    let world = self.getWorldSpace();
    if (world) {
      return world.getFormID();
    }

    let cell = self.getParentCell();
    if (cell) {
      return cell.getFormID();
    }

    return 0;
  }

  static getPos(self: ObjectReference): NiPoint3 {
    return [self.getPositionX(), self.getPositionY(), self.getPositionZ()];
  };

  static getDistance(a: NiPoint3, b: NiPoint3) {
    const deltaX = a[0] - b[0];
    const deltaY = a[1] - b[1];
    const deltaZ = a[2] - b[2];
    return Math.sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
  };

  static getDistanceNoZ(a: NiPoint3, b: NiPoint3) {
    const deltaX = a[0] - b[0];
    const deltaY = a[1] - b[1];
    return Math.sqrt(deltaX * deltaX + deltaY * deltaY);
  };

  static isBench(base: Form): boolean {
    const getWorkbenchType = (sp as any).getWorkbenchType as (baseFormId: number) => number;
    return getWorkbenchType(base.getFormID()) !== 0;
  }

  static dealWithRef(self: ObjectReference, base: Form): void {
    const t = base.getType();
    const isItem = FormTypeEx.isItem(t);

    // BlackFallsBarrow02, door isn't opening via SetOpen so we're hacking it.
    // Not blocking activation & asking parent to activate until will be in the correct state
    // See also modelApplyUtils.ts
    const caveGSecretDoor01 = 0x6f703;

    /*
      THORNSWOOD PATCH. A locked door or chest keeps Skyrim's own handling
      until it is open.

      Upstream unlocked every tracked reference and blocked activation on it,
      and locked plus blocked is unopenable by any means, so every lock in the
      world was simply gone. The unlock is removed and a locked reference is
      left unblocked, which is what makes the lockpicking mini game appear.

      Activators and furniture are not blocked either: levers, chains, bars,
      chairs and shrines are Skyrim's to run and the server has no scripts for
      them. Containers, doors, items and actors still go through the server
      exactly as before.

      activationService has the other half: while a reference is locked it is
      not announced to the server at all.

      THORNSWOOD PATCH (Thornswood #3080). A plant or a tree goes through the
      server too, before Skyrim picks it.

      Upstream left Flora and Tree unblocked ("I don't think it's
      necessary"). Unblocked, the presser's own Skyrim played the pick,
      emptied the plant and put the produce in its own pack with the game's
      "added" line before the server had said anything. When the server said
      no, the gamemode had to take the produce back out and tell this client
      to un-pick the plant (putBack, on a four second timer). Blocked, a press
      only sends the activation: a refused one changes nothing on this
      machine, and the server hands over what an allowed one gives.

      THORNSWOOD PATCH (Thornswood #3080). A crafting bench goes through the
      server too, before Skyrim opens its menu.

      Furniture stays Skyrim's, as above, except furniture whose record names
      a bench type (WBDT): a forge, a cookpot, a loom, an alchemy lab, an
      enchanter. Unblocked, a bench opened its crafting menu on the presser's
      machine before the server had said anything, so somebody the server
      refused the bench still stood in its menu and only each pick was
      refused. Blocked, a refused press opens nothing, and an allowed one
      comes back as the server's open, which activates the bench with
      default processing exactly as it does a container. One person works a
      bench at a time, the server's occupancy rule for furniture.

      getWorkbenchType is ours (ObjectReferenceApi) and is not in the npm
      typings this client builds against, hence the cast.
    */
    if (self.isLocked()) {
      self.blockActivation(false);
    } else if (t === FormType.Container
      || isItem
      || t === FormType.NPC
      || t === FormType.Flora
      || t === FormType.Tree
      || (t === FormType.Furniture && ObjectReferenceEx.isBench(base))
      || (t === FormType.Door && self.getBaseObject()?.getFormID() !== caveGSecretDoor01)) {
      self.blockActivation(true);
    } else {
      self.blockActivation(false);
    }

    if (isItem) {
      self.setMotionType(MotionType.Keyframed, false);
    }

    // https://github.com/skyrim-multiplayer/issue-tracker/issues/36
    if (t === FormType.Flora && Flora.from(base)?.getIngredient()) {
      self.setMotionType(MotionType.Keyframed, false);
    }
  }
}
