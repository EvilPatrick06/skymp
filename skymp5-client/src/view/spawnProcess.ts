import { ObjectReference, Game, Actor, MotionType } from "skyrimPlatform";
import { Appearance, applyTints } from "../sync/appearance";
import { NiPoint3 } from "../sync/movement";
import { ObjectReferenceEx } from "../extensions/objectReferenceEx";

export class SpawnProcess {
  /*
    THORNSWOOD. WHERE THE SPAWN POSITION IS KEPT, AND WHY IT IS PUT BACK.

    Patrick, 21 September, playing it: "To many fucking NPCs spawning man all at
    once group together, on top of players", and again: "why is 5 chcikens
    psawing on top of me or 3 wolfs or a whole ass giant on top of me dude".

    formView spawns every neighbour with player.placeAtMe(base, 1, true, true).
    placeAtMe always places at the caller, so every NPC in the province is born
    standing on the person, disabled, and this class is what is supposed to move
    it away before anybody sees it. The move is the first line of the
    constructor and it is correct.

    WHAT UNDOES IT IS THE LAST LINE. resurrect() on an actor resets it, and a
    reset returns an actor to its editor location. For a reference made by
    placeAtMe there is no editor location in any plugin: the engine's idea of
    where it belongs is where it was placed, which is the person. So the
    sequence was move, enable, and then snap straight back onto whoever was
    standing there, which is exactly what the clips show. It is not a race and
    it is not the server sending a bad position: the server's position was
    applied and then discarded.

    So the position is remembered and re-applied after the actor is resurrected
    and after a non-actor has its motion type set, which are the two last things
    that happen to a spawning reference. Re-applying a position that is already
    right costs one engine call and is harmless; not re-applying it put a giant
    on Patrick's head.

    It is deliberately a re-apply rather than a reordering. Enabling before
    positioning would show the NPC at the person for a frame, and resurrecting
    before enabling is not something this engine promises anything about.
  */
  private spawnPos: NiPoint3;

  constructor(
    appearance: Appearance | null,
    pos: NiPoint3,
    refrId: number,
    private callback: () => void,
  ) {
    this.spawnPos = pos;

    const refr = ObjectReference.from(Game.getFormEx(refrId));
    if (!refr || refr.getFormID() !== refrId) {
      return;
    }

    refr.setPosition(...pos).then(() => this.enable(appearance, refrId));
  }

  private enable(appearance: Appearance | null, refrId: number) {
    const refr = ObjectReference.from(Game.getFormEx(refrId));
    if (!refr || refr.getFormID() !== refrId) {
      return;
    }

    const ac = Actor.from(refr);
    if (ac && appearance) {
      applyTints(ac, appearance);
    }
    refr.enable(false).then(() => this.resurrect(refrId));
  }

  private resurrect(refrId: number) {
    const refr = ObjectReference.from(Game.getFormEx(refrId));
    if (!refr || refr.getFormID() !== refrId) {
      return;
    }

    const ac = Actor.from(refr);
    if (ac) {
      return ac.resurrect().then(() => this.settle(refrId));
    }

    ObjectReferenceEx.dealWithRef(refr, refr.getBaseObject()!);

    return refr
      .setMotionType(MotionType.Keyframed, true)
      .then(() => this.settle(refrId));
  }

  /*
    THORNSWOOD. The last word on where it stands. See the note at the top.
  */
  private settle(refrId: number) {
    const refr = ObjectReference.from(Game.getFormEx(refrId));
    if (!refr || refr.getFormID() !== refrId) {
      // Gone between calls. There is nothing to place and nothing to call back
      // about, and this is a normal outcome for somebody who walked out of
      // range while the spawn was still running.
      return;
    }

    return refr.setPosition(...this.spawnPos).then(() => {
      this.callback();
    });
  }
}
