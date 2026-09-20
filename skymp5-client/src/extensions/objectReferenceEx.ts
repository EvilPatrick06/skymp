import { Flora, Form, FormType, MotionType, ObjectReference } from "skyrimPlatform";
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
    */
    if (self.isLocked()) {
      self.blockActivation(false);
    } else if (t === FormType.Container
      || isItem
      || t === FormType.NPC
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
