// @ts-expect-error (TODO: Remove in 2.10.0)
import { Actor, Form, FormType, Menu, interruptCast, castSpellImmediate, printConsole, applyAnimationVariablesToActor, ActorAnimationVariables } from 'skyrimPlatform';
import {
  Ammo,
  Armor,
  Cell,
  Game,
  ObjectReference,
  TESModPlatform,
  Ui,
  Utility,
  WorldSpace,
  Weapon,
  on, // TODO: use this.controller.on instead
  once, // TODO: use this.controller.once instead
  storage, // TODO: use this.sp.storage instead
} from 'skyrimPlatform';

import * as messages from '../../messages';

/* eslint-disable @typescript-eslint/no-empty-function */
import { ObjectReferenceEx } from '../../extensions/objectReferenceEx';
import { IdManager } from '../../lib/idManager';
import { nameof } from '../../lib/nameof';
import { setActorValuePercentage } from '../../sync/actorvalues';
import { applyAppearanceToPlayer } from '../../sync/appearance';
import { applyEquipment, isBadMenuShown, syncSpellEquipment, SpellType } from '../../sync/equipment';
import { Inventory, acceptPreloadedInventoryBase, applyInventory, getDiff, getInventory, inventoryEntriesEqual, resetInventoryBase } from '../../sync/inventory';
import { Movement } from '../../sync/movement';
import { learnSpells, removeAllSpells } from '../../sync/spell';
import { ModelApplyUtils } from '../../view/modelApplyUtils';
import { FormModel, WorldModel } from '../../view/model';
import { LoadGameService } from './loadGameService';
import { UpdateMovementMessage } from '../messages/updateMovementMessage';
import { ChangeValuesMessage } from '../messages/changeValuesMessage';
import { UpdateAnimationMessage } from '../messages/updateAnimationMessage';
import { UpdateEquipmentMessage } from '../messages/updateEquipmentMessage';
import { RagdollService } from './ragdollService';
import { UpdateAppearanceMessage } from '../messages/updateAppearanceMessage';
import { TeleportMessage } from '../messages/teleportMessage';
import { DeathStateContainerMessage } from '../messages/deathStateContainerMessage';
import { RespawnNeededError } from '../../lib/errors';
import { OpenContainerMessage } from '../messages/openContainerMessage';
import { ActivateMessage } from '../messages/activateMessage';
import { ClientListener, CombinedController, Sp } from './clientListener';
import { HostStartMessage } from '../messages/hostStartMessage';
import { HostStopMessage } from '../messages/hostStopMessage';
import { ConnectionMessage } from '../events/connectionMessage';
import { SetInventoryMessage } from '../messages/setInventoryMessage';
import { CreateActorMessage, CreateActorMessageAdditionalProps } from '../messages/createActorMessage';
import { DestroyActorMessage } from '../messages/destroyActorMessage';
import { SetRaceMenuOpenMessage } from '../messages/setRaceMenuOpenMessage';
import { UpdatePropertyMessage } from '../messages/updatePropertyMessage';
import { TeleportMessage2 } from '../messages/teleportMessage2';

// TODO: refactor worldViewMisc into service
import {
  getObjectReference,
  getViewFromStorage,
  remoteIdToLocalId,
} from '../../view/worldViewMisc';
import { TimeService } from './timeService';
import { logTrace, logError } from '../../logging';

import { SpellCastMessage } from '../messages/spellCastMessage';
import { UpdateAnimVariablesMessage } from '../messages/updateAnimVariablesMessage';
import { MsgType } from '../../messages';

const carriedOutTeleportSeqKey = 'thornswoodCarriedOutTeleportSeq';

export const getPcInventory = (): Inventory | undefined => {
  const res = storage['pcInv'];
  if (typeof res === 'object' && (res as any)['entries']) {
    return res as Inventory;
  }
  return undefined;
};

const setPcInventory = (inv: Inventory): void => {
  storage['pcInv'] = inv;
};

/*
  THORNSWOOD PATCH: stop re-applying a difference that is never going to close.

  This ran applyInventory every five seconds, for ever, whatever the state of
  the inventory. That is right when the server has sent something new and the
  game has not caught up. It is a disaster when the difference CANNOT be
  resolved, because then it is re-added every five seconds for the rest of the
  session, and Skyrim announces every single add.

  Reported by a tester on 22 September, three separate times and in three
  different words before anybody connected them:
    "I just logged back in, not touching a thing, just notifications of
     looting boots keeps popping up"
    "exiting the bench caused a whole bunch of notifcations for picking up
     boots, but my player inventory doesn't show that many boots"
  and a third clip of the same list scrolling past while standing still. The
  notifications were real and the items were not: every five seconds the same
  unresolvable entry was handed over again.

  So the diff is computed first and three things follow from it. Nothing to do
  means nothing is done, which is the common case and now costs one comparison
  instead of a full apply. Something to do is done, as before. And THE SAME
  THING TO DO, three times running, is a difference that applying does not
  fix: it is reported once and then left alone until it changes.

  Deliberately not a permanent give-up: the signature is compared, so the
  moment the server sends anything different, or the player picks something
  up, it starts working again. What it cannot do any more is spend the rest of
  somebody's evening telling them they found the same boots.
*/
let pcInvLastApply = 0;
let pcInvStuckSig = '';
let pcInvStuckFor = 0;
const PC_INV_GIVE_UP_AFTER = 3;

on('update', () => {
  if (storage['ownerInventorySettling'] === true) { return; }
  if (isBadMenuShown() || Ui.isMenuOpen('RaceSex Menu')) {
    return;
  }
  if (Date.now() - pcInvLastApply <= 5000) {
    return;
  }
  pcInvLastApply = Date.now();

  const pcInv = getPcInventory();
  if (!pcInv) {
    return;
  }
  const pl = Game.getPlayer();
  if (!pl) {
    return;
  }

  // ignoreWorn true, to match the apply below: what is equipped is the
  // equipment sync's business and not this one's.
  const diff = getDiff(pcInv, getInventory(pl), true).entries;
  if (diff.length === 0) {
    pcInvStuckSig = '';
    pcInvStuckFor = 0;
    return;
  }

  const sig = JSON.stringify(diff);
  if (sig === pcInvStuckSig) {
    pcInvStuckFor++;
    if (pcInvStuckFor >= PC_INV_GIVE_UP_AFTER) {
      if (pcInvStuckFor === PC_INV_GIVE_UP_AFTER) {
        printConsole(
          `[thornswood] the inventory difference has not closed after ` +
          `${PC_INV_GIVE_UP_AFTER} tries, so it is left alone until it changes: ${sig}`
        );
      }
      return;
    }
  } else {
    pcInvStuckSig = sig;
    pcInvStuckFor = 0;
  }

  applyInventory(pl, pcInv, false, true);
});

const unequipIronHelmet = () => {
  const ironHelment = Armor.from(Game.getFormEx(0x00012e4d));
  const pl = Game.getPlayer();
  if (pl) {
    pl.unequipItem(ironHelment, false, true);
  }
};

export class RemoteServer extends ClientListener {
  constructor(private sp: Sp, private controller: CombinedController) {
    super();

    this.controller.emitter.on("hostStartMessage", (e) => this.onHostStartMessage(e));
    this.controller.emitter.on("hostStopMessage", (e) => this.onHostStopMessage(e));
    this.controller.emitter.on("setInventoryMessage", (e) => this.onSetInventoryMessage(e));
    this.controller.emitter.on("openContainerMessage", (e) => this.onOpenContainerMessage(e));
    this.controller.emitter.on("updateMovementMessage", (e) => this.onUpdateMovementMessage(e));
    this.controller.emitter.on("updateAnimationMessage", (e) => this.onUpdateAnimationMessage(e));
    this.controller.emitter.on("updateEquipmentMessage", (e) => this.onUpdateEquipmentMessage(e));
    this.controller.emitter.on("changeValuesMessage", (e) => this.onChangeValuesMessage(e));
    this.controller.emitter.on("updateAppearanceMessage", (e) => this.onUpdateAppearanceMessage(e));
    this.controller.emitter.on("teleportMessage", (e) => this.onTeleportMessage(e));
    this.controller.emitter.on("teleportMessage2", (e) => this.onTeleportMessage(e));
    this.controller.emitter.on("createActorMessage", (e) => this.onCreateActorMessage(e));
    this.controller.emitter.on("destroyActorMessage", (e) => this.onDestroyActorMessage(e));
    this.controller.emitter.on("setRaceMenuOpenMessage", (e) => this.onSetRaceMenuOpenMessage(e));
    this.controller.emitter.on("updatePropertyMessage", (e) => this.onUpdatePropertyMessage(e));
    this.controller.emitter.on("deathStateContainerMessage", (e) => this.onDeathStateContainerMessage(e));

    this.controller.emitter.on("connectionAccepted", () => this.handleConnectionAccepted());
    this.controller.emitter.on("connectionDisconnect", () => this.handleConnectionAccepted());
    this.controller.emitter.on("connectionFailed", () => this.handleConnectionAccepted());
    this.controller.emitter.on("connectionDenied", () => this.handleConnectionAccepted());

    this.controller.emitter.on("spellCastMessage", (e) => this.onSpellCastMessage(e));
    this.controller.emitter.on("updateAnimVariablesMessage", (e) => this.onUpdateAnimVariablesMessage(e));

  }

  private onHostStartMessage(event: ConnectionMessage<HostStartMessage>) {
    const msg = event.message;
    const target = msg.target;

    let hosted = storage['hosted'];
    if (typeof hosted !== typeof []) {
      // if you try to switch to Set please checkout .concat usage.
      // concat compiles but doesn't work as expected
      hosted = new Array<number>();
      storage['hosted'] = hosted;
    }

    if (!(hosted as Array<unknown>).includes(target)) {
      (hosted as Array<unknown>).push(target);
    }
  }

  private onHostStopMessage(event: ConnectionMessage<HostStopMessage>) {
    const msg = event.message;
    const target = msg.target;
    logTrace(this, 'hostStop ' + target.toString(16));

    const hosted = storage['hosted'] as Array<number>;
    if (typeof hosted === typeof []) {
      storage['hosted'] = hosted.filter((x) => x !== target);
    }
  }

  private onSetInventoryMessage(event: ConnectionMessage<SetInventoryMessage>): void {
    this.numSetInventory++;

    const msg = event.message;
    this.ownerInventory = msg.inventory;
    this.ownerInventoryIsSnapshot = false;
    const epoch = this.ownerSpawnEpoch;
    once('update', () => {
      if (epoch !== this.ownerSpawnEpoch) { return; }
      setPcInventory(msg.inventory);
      this.recordOwnerOutfit('inventory-received');

      let blocked = false;

      this.controller.emitter.emit('queryBlockSetInventoryEvent', {
        block: () => blocked = true
      });

      if (!blocked) {
        pcInvLastApply = 0;
      }
    });
  }

  private onOpenContainerMessage(event: ConnectionMessage<OpenContainerMessage>): void {
    once('update', async () => {
      await Utility.wait(0.1); // Give a chance to update inventory

      const remoteId = event.message.target;
      const localId = remoteIdToLocalId(remoteId);
      const refr = ObjectReference.from(Game.getFormEx(localId));

      if (refr === null) {
        logError(this, 'onOpenContainerMessage - refr not found', 'remoteId', remoteId.toString(16), 'localId', localId.toString(16));
        return;
      }

      refr.activate(Game.getPlayer(), true);

      const baseObject = refr.getBaseObject();
      const baseType = baseObject?.getType();

      let functionChecker: (() => boolean) | null = null;
      let factName = "";
      let delaySeconds = -1.0;
      if (baseType === FormType.Container) {
        functionChecker = () => Ui.isMenuOpen("ContainerMenu");
        factName = "'ContainerMenu open'";
        delaySeconds = 0.0;
      } else if (baseType === FormType.Furniture) {
        functionChecker = () => !!Game.getPlayer()?.getFurnitureReference();
        factName = "'getFurnitureReference not null'";
        delaySeconds = 1.0;
      }

      if (functionChecker === null) {
        logTrace(this, "onOpenContainerMesage - not a container or furniture", baseType);
        return;
      }

      // In SkyMP containers have 2-nd, closing activation under the hood.
      // This differs from Skyrim's behavior, where it's just one activation.

      (async () => {
        logTrace(this, "onOpenContainerMesage - waiting for", factName, "to be true");
        while (!functionChecker()) await Utility.wait(0.1);

        logTrace(this, "onOpenContainerMesage - waiting for", factName, "to be false");
        while (functionChecker()) await Utility.wait(0.1);

        logTrace(this, "onOpenContainerMesage - menu closed", factName);

        const message: ActivateMessage = {
          t: messages.MsgType.Activate,
          data: {
            caster: 0x14, target: event.message.target, isSecondActivation: true
          }
        };

        logTrace(this, "onOpenContainerMesage - waiting", delaySeconds, "seconds before sending ActivateMessage");

        Utility.waitMenuMode(delaySeconds).then(() => {
          this.controller.emitter.emit("sendMessage", {
            message: message,
            reliability: "reliable"
          });

          logTrace(this, "onOpenContainerMesage - sent ActivateMessage", message);
        });
      })();
    });
  }

  private onTeleportMessage(event: ConnectionMessage<TeleportMessage> | ConnectionMessage<TeleportMessage2>): void {
    const msg = event.message;
    once('update', () => {
      const id = ("idx" in msg && typeof msg.idx === "number") ? this.getIdManager().getId(msg.idx) : this.getMyActorIndex();
      const refr = id === this.getMyActorIndex() ? Game.getPlayer() : getObjectReference(id);
      logTrace(this,
        `Teleporting id`, id, `refrId`, refr?.getFormID().toString(16), `...`,
        msg.pos,
        'cell/world is',
        msg.worldOrCell.toString(16),
      );
      const ragdollService = this.controller.lookupListener(RagdollService);

      const refrId = refr?.getFormID();

      // THORNSWOOD #1932. One move per reference at a time, to the newest
      // destination. Each message used to start its own ragdoll removal (a
      // latent Papyrus call, six game setting writes before it and six
      // after) and, when that returned, its own MoveTo of the reference. On
      // dev on 5 Oct 2026 skyrim-platform.log has 233 teleports of the own
      // character to one spot from 11:51:20.001 to 20.535, so 233 removals
      // and 233 moves were queued, and the game stopped responding at
      // 20.890. A move to an older destination is overwritten by the next
      // one, so a message that arrives while a move of the same reference is
      // on its way only replaces where that move goes.
      const key = refrId || 0;
      const onItsWay = this.teleportsOnTheirWay.has(key);
      this.teleportsOnTheirWay.set(key, { msg, isMyCharacter: id === this.getMyActorIndex() });
      if (onItsWay) {
        return;
      }

      const removeRagdollCallback = () => {
        const newest = this.teleportsOnTheirWay.get(key);
        this.teleportsOnTheirWay.delete(key);
        if (!newest) {
          return; // the connection changed meanwhile, see handleConnectionAccepted
        }
        const m = newest.msg;
        TESModPlatform.moveRefrToPosition(
          ObjectReference.from(Game.getFormEx(refrId || 0)),
          Cell.from(Game.getFormEx(m.worldOrCell)),
          WorldSpace.from(Game.getFormEx(m.worldOrCell)),
          m.pos[0],
          m.pos[1],
          m.pos[2],
          m.rot[0],
          m.rot[1],
          m.rot[2],
        );
        // The server ignores movement until it sees this number echoed
        // (SendInputsService.sendMovement, ActionListener::OnUpdateMovement).
        if (newest.isMyCharacter && typeof m.teleportSeq === "number") {
          storage[carriedOutTeleportSeqKey] = m.teleportSeq;
        }
      };
      const actor = Actor.from(refr);
      if (actor /*&& actor.getFormID() === 0x14*/) {
        ragdollService.safeRemoveRagdollFromWorld(actor, removeRagdollCallback);
      } else {
        removeRagdollCallback();
      }
    });
  }

  private onCreateActorMessage(event: ConnectionMessage<CreateActorMessage>): void {
    const msg = event.message;
    if (this.skipFormViewCreation(msg)) {
      const refrId = msg.refrId!;
      this.onceLoad(refrId, (refr: ObjectReference) => {
        if (refr) {
          ObjectReferenceEx.dealWithRef(refr, refr.getBaseObject() as Form);
          if (msg.props) {
            if (msg.props.inventory) {
              ModelApplyUtils.applyModelInventory(refr, msg.props.inventory);
            }
            ModelApplyUtils.applyModelIsOpen(refr, !!msg.props['isOpen']);
            ModelApplyUtils.applyModelIsHarvested(
              refr,
              !!msg.props['isHarvested'],
            );

            ModelApplyUtils.applyModelNodeScale(refr, msg.props.setNodeScale);

            ModelApplyUtils.applyModelNodeTextureSet(refr, msg.props.setNodeTextureSet);

            // THORNSWOOD PATCH (#495). The server writes this flag as
            // "isDisabled" (CreateActorMessage.h, MpObjectReference.cpp) and
            // this read "disabled", so it was always false: a reference the
            // server holds disabled was never disabled here, and one a client
            // plugin had disabled was enabled again on every stream.
            // "disabled" is still read for a server that ever sends it.
            ModelApplyUtils.applyModelIsDisabled(refr, !!(msg.props['isDisabled'] || msg.props['disabled']));

            // TODO: move to a separate module
            const animation = msg.props.lastAnimation;
            if (typeof animation === "string") {
              const refrid = refr.getFormID();

              (async () => {
                for (let i = 0; i < 5; i++) {
                  // retry. pillars in bleakfalls are not reliable for some reason
                  let res2 = ObjectReference.from(Game.getFormEx(refrid))?.playAnimation(animation);
                  if (res2) {
                    break;
                  }
                  await Utility.wait(2);
                }
              })();
            }


            let displayName = msg.props.displayName;

            // keep in sync with spSnippetService.ts
            if (typeof displayName === "string") {

              const replaceValue = refr.getBaseObject()?.getName();

              if (replaceValue !== undefined) {
                displayName = displayName.replace(/%original_name%/g, replaceValue);
              } else {
                logError(this, "Couldn't get a replaceValue for SetDisplayName, refr.getFormID() was", refr.getFormID().toString(16));
              }

              refr.setDisplayName(displayName, true);
              logTrace(this, `calling setDisplayName`, displayName, `for`, refr.getFormID().toString(16));
            }
          }
        } else {
          logError(this, 'Failed to apply model to', refrId.toString(16));
        }
      });
      return;
    }

    logTrace(this, "Create actor");

    const i = this.getIdManager().allocateIdFor(msg.idx);
    if (this.worldModel.forms.length <= i) {
      this.worldModel.forms.length = i + 1;
    }

    let movement: Movement | undefined = undefined;
    // TODO: better check if it is an npc (not an object reference)
    if (msg.refrId !== undefined && msg.refrId >= 0xff000000) {
      movement = {
        pos: msg.transform.pos,
        rot: msg.transform.rot,
        worldOrCell: msg.transform.worldOrCell,
        runMode: 'Standing',
        direction: 0,
        isInJumpState: false,
        isSneaking: false,
        isBlocking: false,
        isWeapDrawn: false,
        isDead: false,
        healthPercentage: 1.0,
        speed: 0,
      };
    }

    const form: FormModel = {
      idx: msg.idx,
      movement,
      numMovementChanges: 0,
      numAppearanceChanges: 0,
      baseId: msg.baseId,
      refrId: msg.refrId,
      isMyClone: msg.isMe,
    };
    this.worldModel.forms[i] = form;

    if (msg.appearance) {
      form.appearance = msg.appearance;
    }

    if (msg.equipment) {
      form.equipment = msg.equipment;
    }

    if (msg.isDead) {
      form.isDead = msg.isDead;
    }

    if (msg.animation) {
      form.animation = msg.animation;
    }

    if (msg.props) {
      for (const propName in msg.props) {
        (form as Record<string, unknown>)[propName] = msg.props[propName as keyof CreateActorMessageAdditionalProps];
      }
    }

    msg.customPropsJsonDumps.forEach(element => {
      let parsed: unknown;
      try {
        parsed = JSON.parse(element.propValueJsonDump);
      } catch (e) {
        if (e instanceof SyntaxError) {
          logError(this, "createActor", msg.refrId?.toString(16), "failed to parse custom prop", element.propName, element.propValueJsonDump, e.message);
        } else {
          throw e;
        }
      }
      (form as Record<string, unknown>)[element.propName] = parsed;
    });

    if (msg.isMe) {
      const earlyMenuRequest = this.worldModel.playerCharacterFormIdx < 0 && this.raceMenuRequested;
      this.cancelOwnerSpawn();
      this.raceMenuRequested = earlyMenuRequest;
      this.ownerSpawnTarget = msg.transform;
      this.ownerInventory = msg.props?.inventory ?? msg.equipment?.inv;
      this.ownerInventoryIsSnapshot = true;
      // Old owner work may still target the same local reference after a
      // reconnect. Drain it before this snapshot is compared or applied.
      this.ownerInventoryPending = this.sp.getInventoryQueueFence();
      storage['ownerInventorySettling'] = true;
      this.ownerSpawnReady = new Promise<boolean>(resolve => this.finishOwnerSpawn = resolve);
      this.worldModel.playerCharacterFormIdx = i;
      this.worldModel.playerCharacterRefrId = msg.refrId || 0;
      const epoch = this.ownerSpawnEpoch;
      once('update', () => {
        if (epoch === this.ownerSpawnEpoch && this.ownerInventory) {
          setPcInventory(this.ownerInventory);
          this.recordOwnerOutfit('spawn-snapshot');
        }
      });
    }
    const ownerEpoch = this.ownerSpawnEpoch;
    const ownerIsCurrent = () => ownerEpoch === this.ownerSpawnEpoch;

    // TODO: move to a separate module

    if (msg.props && !msg.props.isHostedByOther) {
    }

    // The pack comes from the inventory and what is worn from the equipment
    // record. Since Thornswood #1560 the server writes a new character's
    // record at the moment the character is made, before this packet, so a
    // creation packet carries the clothes in both and the record decides.
    // A new character's packet also says the race menu is open, which is
    // what marks a spawn as creation here.
    const creation = msg.props?.isRaceMenuOpen === true;
    let ownerArrived = false;
    let ownerArrivalCell = 0;
    let ownerBasePrepared = false;
    let initialInventoryLoaded = false;
    const ownerIsLoaded = (): boolean => {
      const pc = Game.getPlayer();
      if (!pc?.is3DLoaded()) { return false; }
      const location = pc.getWorldSpace()?.getFormID() || pc.getParentCell()?.getFormID();
      if (location !== msg.transform.worldOrCell) { return false; }
      if (pc.getWorldSpace()) {
        // Distance alone can accept a nearby source cell while MoveTo is
        // pending. Check Skyrim's actual loaded cell grid before latching.
        const coordinates = this.sp.getExteriorCellCoordinates(pc.getParentCell()?.getFormID() || 0);
        if (!coordinates || coordinates[0] !== Math.floor(msg.transform.pos[0] / 4096)
          || coordinates[1] !== Math.floor(msg.transform.pos[1] / 4096)) { return false; }
      }
      if (ownerArrived) { return pc.getParentCell()?.getFormID() === ownerArrivalCell; }
      const distance = Math.hypot(pc.getPositionX() - msg.transform.pos[0],
        pc.getPositionY() - msg.transform.pos[1], pc.getPositionZ() - msg.transform.pos[2]);
      ownerArrived = distance < 256;
      if (ownerArrived) { ownerArrivalCell = pc.getParentCell()?.getFormID() || 0; }
      return ownerArrived;
    };
    const applyPcInv = (): boolean => {
      const pc = Game.getPlayer();
      let inv = this.ownerInventory;
      if (!pc?.is3DLoaded() || !inv || isBadMenuShown() || Ui.isMenuOpen('RaceSex Menu')) { return false; }
      if (this.ownerInventoryIsSnapshot && msg.equipment) {
        inv = JSON.parse(JSON.stringify(inv)) as Inventory;
        for (const entry of inv.entries) { delete entry.worn; delete entry.wornLeft; }
        // Incoming equipment echoes update the live model while loading.
        // Restore the original saved snapshot, not that transient model.
        for (const equipped of msg.equipment.inv.entries) {
          if (equipped.count <= 0 || (!equipped.worn && !equipped.wornLeft)) { continue; }
          const owned = inv.entries.find(entry => entry.count > 0 && !entry.worn && !entry.wornLeft && inventoryEntriesEqual(entry, equipped, true));
          if (!owned) { return false; }
          if (owned.count > 1) { inv.entries.push({...owned, count: owned.count - 1}); owned.count = 1; }
          owned.worn = equipped.worn; owned.wornLeft = equipped.wornLeft;
        }
      }
      // Base-container reset queues a full removal. Wait for that removal
      // before calculating additions, otherwise retained starter pieces can
      // be omitted from the diff and removed by the older native job.
      if (this.ownerInventoryPending) {
        // Wait for execution, not the desired result. Paused natives can
        // reject a batch, and a newer snapshot can supersede its contents.
        if (!this.ownerInventoryPending()) { return false; }
        this.ownerInventoryPending = undefined;
      }
      if (!ownerBasePrepared && initialInventoryLoaded) {
        acceptPreloadedInventoryBase(pc);
        ownerBasePrepared = true;
      }
      if (!ownerBasePrepared) {
        try { resetInventoryBase(pc); }
        finally {
          ownerBasePrepared = true;
          this.ownerInventoryPending = this.sp.getInventoryQueueFence();
        }
        return false;
      }
      const actual = getInventory(pc);
      setPcInventory(inv);
      if (getDiff(inv, actual, true).entries.length !== 0) {
        // Even a partial apply that throws may have queued earlier entries.
        // Fence those entries before the next diff so they cannot duplicate.
        try { applyInventory(pc, inv, false, true); }
        finally { this.ownerInventoryPending = this.sp.getInventoryQueueFence(); }
        return false;
      }
      let ready = true;
      // Inventory ownership and saved equipment are separate records. Restore
      // deliberate unequips too, without deleting owned items.
      for (const entry of actual.entries) {
        if (entry.count <= 0 || (!entry.worn && !entry.wornLeft)) { continue; }
        const item = Game.getFormEx(entry.baseId);
        if (!item || Ammo.from(item)) { continue; }
        const expected = inv.entries.filter(value => value.count > 0 && value.baseId === entry.baseId);
        if (Weapon.from(item)) {
          if (entry.worn && !expected.some(value => value.worn)) { pc.unequipItemEx(item, 1, false); ready = false; }
          if (entry.wornLeft && !expected.some(value => value.wornLeft)) { pc.unequipItemEx(item, 2, false); ready = false; }
        } else if (!expected.some(value => value.worn || value.wornLeft)) {
          pc.unequipItem(item, false, true); ready = false;
        }
      }
      for (const entry of inv.entries) {
        if (entry.count <= 0 || (!entry.worn && !entry.wornLeft)) { continue; }
        const item = Game.getFormEx(entry.baseId);
        if (!item) { ready = false; continue; }
        if (Ammo.from(item)) { continue; }
        const weapon = Weapon.from(item);
        const equipped = weapon
          ? ((!entry.worn || pc.getEquippedWeapon(false)?.getFormID() === entry.baseId)
            && (!entry.wornLeft || pc.getEquippedWeapon(true)?.getFormID() === entry.baseId))
          : pc.isEquipped(item);
        if (!equipped) {
          if (pc.getItemCount(item) > 0) {
            if (weapon) {
              if (entry.worn) { pc.equipItemEx(item, 1, false, false); }
              if (entry.wornLeft) { pc.equipItemEx(item, 2, false, false); }
            } else { pc.equipItem(item, false, true); }
          }
          ready = false;
        }
      }
      // Base-ID equip checks cannot distinguish improved or enchanted copies.
      // Refuse readiness if the wrong instance is worn; retain all metadata.
      const worn = (inventory: Inventory): Inventory => ({ entries: inventory.entries.filter(entry =>
        entry.count > 0 && (entry.worn || entry.wornLeft) && !Ammo.from(Game.getFormEx(entry.baseId))) });
      return ready && getDiff(worn(inv), worn(getInventory(pc)), false).entries.length === 0;
    };
    if (msg.isMe) { this.ownerOutfitIsReady = () => ownerIsCurrent() && ownerIsLoaded() && applyPcInv(); }
    if (msg.isMe && (msg.props?.isRaceMenuOpen || this.raceMenuRequested)) {
      this.onSetRaceMenuOpenMessage({ message: { t: MsgType.SetRaceMenuOpen, open: true } });
    }

    let appearancePrepared = false;
    const prepareOwnerAppearance = async (): Promise<void> => {
      if (!appearancePrepared && msg.appearance) {
        applyAppearanceToPlayer(msg.appearance);
        await Utility.wait(0.25);
      }
      appearancePrepared = true;
    };
    const settleOwner = async (): Promise<void> => {
      try {
        if (!ownerIsCurrent()) { return; }
        await prepareOwnerAppearance();
        for (let attempt = 1; attempt <= 120 && ownerIsCurrent(); attempt++) {
          try {
            if ([1, 5, 30, 120].includes(attempt)) { this.recordOwnerOutfit('outfit-wait-' + attempt); }
            if (ownerIsLoaded() && applyPcInv()) {
              const pc = Game.getPlayer()!;
              if (msg.equipment) {
                syncSpellEquipment(pc, msg.equipment.leftSpell, SpellType.Left);
                syncSpellEquipment(pc, msg.equipment.rightSpell, SpellType.Right);
                syncSpellEquipment(pc, msg.equipment.voiceSpell, SpellType.Voice);
                syncSpellEquipment(pc, msg.equipment.instantSpell, SpellType.Instant);
              }
              pc.queueNiNodeUpdate();
              logTrace(this, 'Owner inventory and outfit ready at attempt', attempt);
              this.recordOwnerOutfit('outfit-ready');
              this.ownerSpawnSettled = true;
              this.finishOwnerSpawn?.(true);
              if (!this.raceMenuRequested) { delete storage['ownerInventorySettling']; }
              return;
            }
          } catch (e) {
            if (attempt === 1) { logError(this, 'Retrying owner outfit after native failure', e); }
          }
          await Utility.wait(1);
        }
      } catch (e) { logError(this, 'Owner outfit loading failed', e); }
      if (ownerIsCurrent()) {
        this.finishOwnerSpawn?.(false);
        // Keep background inventory writes blocked after failed settlement.
        // Reconnection supplies a fresh authoritative owner snapshot.
        logError(this, 'Owner inventory or outfit did not finish loading; face menu stays closed');
        this.recordOwnerOutfit('outfit-failed');
      }
    };

    if (msg.isMe && msg.props && msg.props.learnedSpells) {
      const learnedSpells = msg.props.learnedSpells;

      once('update', () => {
        if (!ownerIsCurrent()) { return; }
        Utility.wait(1).then(() => {
          if (!ownerIsCurrent()) { return; }
          const player = Game.getPlayer();

          if (player) {
            removeAllSpells(player);
            learnSpells(player, learnedSpells);
            logTrace(this,
              `player learnedSpells:`, JSON.stringify(learnedSpells),
            );
          }
        });
      });
    }

    if (msg.isMe) {
      if (msg.props?.isDead) {
        once("update", () => {
          this.controller.emitter.emit("applyDeathStateEvent", {
            actor: Game.getPlayer()!,
            isDead: true
          });
        });
      }
    }

    if (msg.isMe) {
      const spawnTask = { running: false };
      once('update', () => {
        if (!ownerIsCurrent()) { return; }
        // Use MoveRefrToPosition to spawn if possible (not in main menu)
        // In case of connection lost this is essential
        if (!spawnTask.running) {
          spawnTask.running = true;
          logTrace(this, 'Using moveRefrToPosition to spawn player');
          (async () => {
            try {
              if (creation) {
                // Prepare while still at the source location. Entering the hall
                // must not reveal the old empty inventory for several frames.
                await prepareOwnerAppearance();
                let prepared = false;
                for (let attempt = 0; attempt < 1200 && ownerIsCurrent(); ++attempt) {
                  // Retain a valid arrival if the loaded save already is here.
                  ownerIsLoaded();
                  try { if (applyPcInv()) { prepared = true; break; } }
                  catch (e) { if (attempt === 0) { logError(this, "Retrying inventory before entering creation", e); } }
                  await Utility.wait(0.1);
                }
                if (!prepared || !ownerIsCurrent()) {
                  if (ownerIsCurrent()) { this.finishOwnerSpawn?.(false); }
                  return;
                }
              }
              for (let attempt = 0; attempt < 120 && ownerIsCurrent(); attempt++) {
                TESModPlatform.moveRefrToPosition(
                  Game.getPlayer(),
                  Cell.from(Game.getFormEx(msg.transform.worldOrCell)),
                  WorldSpace.from(Game.getFormEx(msg.transform.worldOrCell)),
                  msg.transform.pos[0], msg.transform.pos[1], msg.transform.pos[2],
                  msg.transform.rot[0], msg.transform.rot[1], msg.transform.rot[2],
                );
                await Utility.wait(1);
                if (!ownerIsCurrent()) { return; }
                // Preparation may have latched a nearby source cell. Refresh
                // after an actual move crosses its exterior boundary.
                if (ownerArrived && Game.getPlayer()?.getParentCell()?.getFormID() !== ownerArrivalCell) {
                  ownerArrived = false;
                  ownerArrivalCell = 0;
                }
                if (ownerIsLoaded()) { await settleOwner(); return; }
              }
            } catch (e) { logError(this, 'Owner movement failed', e); }
            if (ownerIsCurrent()) { this.finishOwnerSpawn?.(false); }
          })();
        }

        if (msg.props) {
          const baseActorValues = new Map<string, unknown>([
            ['healRate', msg.props.healRate],
            ['healRateMult', msg.props.healRateMult],
            ['health', msg.props.health],
            ['magickaRate', msg.props.magickaRate],
            ['magickaRateMult', msg.props.magickaRateMult],
            ['magicka', msg.props.magicka],
            ['staminaRate', msg.props.staminaRate],
            ['staminaRateMult', msg.props.staminaRateMult],
            ['stamina', msg.props.stamina],
            ['healthPercentage', msg.props.healthPercentage],
            ['staminaPercentage', msg.props.staminaPercentage],
            ['magickaPercentage', msg.props.magickaPercentage],
          ]);

          const player = Game.getPlayer();
          if (player) {
            baseActorValues.forEach((value, key) => {
              if (typeof value === 'number') {
                if (key.includes('Percentage')) {
                  const subKey = key.replace('Percentage', '');
                  const subValue = baseActorValues.get(subKey);
                  if (typeof subValue === 'number') {
                    setActorValuePercentage(player, subKey, value);
                  }
                } else {
                  player.setActorValue(key, value);
                }
              }
            });
          }
        }
      });
      once('tick', () => {
        once('tick', () => {
          if (!ownerIsCurrent()) { return; }
          if (!spawnTask.running) {
            /*
              THORNSWOOD PATCH. Let the other spawn path have it.

              There are two ways to spawn here and they race. The one above
              uses moveRefrToPosition and is what the comment on it calls the
              one to use if possible, not in the main menu. This one loads a
              save that SkyrimPlatform writes on the spot, carrying the
              server's load order and appearance, which is how a stock SkyMP
              install builds a character out of nothing at the main menu.

              Both are guarded by the same flag, so whichever fires first
              wins. tick fires during a loading screen and update does not, so
              this one wins on this build every time.

              It should not. po3_StartOnSave has already loaded the real save
              before the server ever answers, so the character is in the world
              with their own face and their own pack, and this asks Skyrim to
              revert all of it and load a synthetic save on top. Papyrus gets
              as far as 'Reverting game' and stops. That is the infinite
              loading screen.

              So when there is already a game to be in, this stands down and
              the update above moves the character instead. At an actual main
              menu, which is the case this was written for, nothing changes.
            */
            /*
              Asked without calling into the game, which is the whole reason
              the first version of this never fired.

              This runs from a tick handler, and tick handlers are dispatched
              without a Papyrus virtual machine, so Ui.isMenuOpen and
              Game.getPlayer do not answer here, they throw. The catch turned
              every throw into 'not in a game', which is the answer that lets
              the save reload through, so the check could never once have said
              yes.

              Both flags below are plain JavaScript, written by code that does
              run with a virtual machine.
            */
            let alreadyInAGame = false;
            try {
              const g = globalThis as any;
              alreadyInAGame = !!g.__thornswoodInWorld ||
                               (Number(g.__thornswoodUncausedLoad) || 0) > 0;
            } catch (e) {
              alreadyInAGame = false;
            }
            if (alreadyInAGame) {
              try {
                (globalThis as any).__thornswoodSpawnedByMove =
                  ((globalThis as any).__thornswoodSpawnedByMove || 0) + 1;
              } catch (e) { }
              return;
            }

            spawnTask.running = true;

            let loadOrder = new Array<string>();
            for (let i = 0; i < this.sp.Game.getModCount(); ++i) {
              loadOrder.push(this.sp.Game.getModName(i));
            }
            // THORNSWOOD PATCH. Light plugins (ESL, ESL-flagged ESP) are not in
            // getModName's list. A face part from one, like a KhisartinBeards
            // beard (0xFE029827), names its plugin by light index, so the save
            // lists them too, in the game's light order.
            let lightLoadOrder = new Array<string>();
            for (let i = 0; i < this.sp.Game.getLightModCount(); ++i) {
              lightLoadOrder.push(this.sp.Game.getLightModName(i));
            }

            logTrace(this, `loading game in world/cell`, msg.transform.worldOrCell.toString(16));
            const loadGameService = this.controller.lookupListener(LoadGameService);
            const initialInventory = creation ? this.ownerInventory : undefined;
            loadGameService.loadGame(
              msg.transform.pos,
              msg.transform.rot,
              msg.transform.worldOrCell,
              msg.appearance
                ? {
                  name: msg.appearance.name,
                  raceId: msg.appearance.raceId,

                  // TODO: In types, isFemale is under face, but in the reality SP expects it here. Fix required.
                  // @ts-expect-error
                  isFemale: msg.appearance.isFemale,

                  face: {
                    hairColor: msg.appearance.hairColor,
                    bodySkinColor: msg.appearance.skinColor,
                    headTextureSetId: msg.appearance.headTextureSetId,
                    headPartIds: msg.appearance.headpartIds,
                    presets: msg.appearance.presets
                  },
                }
                : undefined,
              loadOrder,
              { minutes: 0, seconds: 0, hours: this.controller.lookupListener(TimeService).getTime().newGameHourValue },
              initialInventory,
              lightLoadOrder
            );
            initialInventoryLoaded = !!initialInventory;
            once('update', () => { void settleOwner(); });
          }
        });
      });
    }
  }

  private onDestroyActorMessage(event: ConnectionMessage<DestroyActorMessage>): void {
    const msg = event.message;

    const i = this.getIdManager().getId(msg.idx);
    this.worldModel.forms[i] = undefined;
    getViewFromStorage()?.syncFormArray(this.worldModel);

    // Shrink to fit
    while (1) {
      const length = this.worldModel.forms.length;
      if (!length) {
        break;
      }
      if (this.worldModel.forms[length - 1]) {
        break;
      }
      this.worldModel.forms.length = length - 1;
    }

    if (this.worldModel.playerCharacterFormIdx === i) {
      this.cancelOwnerSpawn();
      this.worldModel.playerCharacterFormIdx = -1;
      this.worldModel.playerCharacterRefrId = 0;

      // TODO: move to a separate module
      once('update', () => Game.quitToMainMenu());
    }

    this.getIdManager().freeIdFor(msg.idx);
  }

  private onUpdateMovementMessage(event: ConnectionMessage<UpdateMovementMessage>): void {
    const msg = event.message;

    const i = this.getIdManager().getId(msg.idx);

    const form = this.worldModel.forms[i];

    if (form === undefined) {
      logError(this, `onUpdateMovementMessage - Form with idx`, msg.idx, `not found`);
      return;
    }

    form.movement = msg.data;
    if (!form.numMovementChanges) {
      form.numMovementChanges = 0;
    }
    form.numMovementChanges++;
  }

  private onUpdateAnimationMessage(event: ConnectionMessage<UpdateAnimationMessage>): void {
    const msg = event.message;

    const i = this.getIdManager().getId(msg.idx);

    const form = this.worldModel.forms[i];

    if (form === undefined) {
      logError(this, `onUpdateAnimationMessage - Form with idx`, msg.idx, `not found`);
      return;
    }

    form.animation = msg.data;
  }

  private onUpdateAppearanceMessage(event: ConnectionMessage<UpdateAppearanceMessage>): void {
    const msg = event.message;

    const i = this.getIdManager().getId(msg.idx);

    const form = this.worldModel.forms[i];

    if (form === undefined) {
      logError(this, `onUpdateAppearanceMessage - Form with idx`, msg.idx, `not found`);
      return;
    }

    form.appearance = msg.data || undefined;
    if (!form.numAppearanceChanges) {
      form.numAppearanceChanges = 0;
    }
    form.numAppearanceChanges++;

    const newAppearance = msg.data;

    if (i === this.getMyActorIndex() && newAppearance) {
      this.controller.once("update", () => {
        applyAppearanceToPlayer(newAppearance);
        logTrace(this, "Applied appearance to the player");
      });
    }
  }

  private onUpdateEquipmentMessage(event: ConnectionMessage<UpdateEquipmentMessage>): void {
    const msg = event.message;

    const i = this.getIdManager().getId(msg.idx);

    const form = this.worldModel.forms[i];

    if (form === undefined) {
      logError(this, `onUpdateEquipmentMessage - Form with idx`, msg.idx, `not found`);
      return;
    }

    form.equipment = msg.data;
  }

  private onUpdatePropertyMessage(event: ConnectionMessage<UpdatePropertyMessage>): void {
    const msg = event.message;
    const msgData = this.extractUpdatePropertyMessageData(msg);

    if (this.skipFormViewCreation(msg)) {
      const refrId = msg.refrId;
      once('update', () => {
        const refr = ObjectReference.from(Game.getFormEx(refrId));
        if (!refr) {
          logError(this, 'UpdateProperty: refr not found');
          return;
        }
        if (msg.propName === 'inventory') {
          ModelApplyUtils.applyModelInventory(refr, msgData as Inventory);
        } else if (msg.propName === 'isOpen') {
          ModelApplyUtils.applyModelIsOpen(refr, !!msgData);
        } else if (msg.propName === 'isHarvested') {
          ModelApplyUtils.applyModelIsHarvested(refr, !!msgData);
        } else if (msg.propName === 'disabled' || msg.propName === 'isDisabled') {
          ModelApplyUtils.applyModelIsDisabled(refr, !!msgData);
        }
      });
      return;
    }
    const i = this.getIdManager().getId(msg.idx);
    const form = this.worldModel.forms[i];
    (form as Record<string, unknown>)[msg.propName] = msgData;
  }

  private onDeathStateContainerMessage(event: ConnectionMessage<DeathStateContainerMessage>): void {
    const msg = event.message;

    logTrace(this, `Received death state:`, JSON.stringify(msg.tIsDead));

    const id = this.getIdManager().getId(msg.tIsDead.idx);
    const form = this.worldModel.forms[id];

    if (form === undefined) {
      logError(this, `onDeathStateContainerMessage - Form with idx`, msg.tIsDead.idx, `not found`);
      return;
    }

    if (msg.tIsDead.propName !== nameof<FormModel>('isDead')) {
      logError(this, `onDeathStateContainerMessage - Invalid propName`, msg.tIsDead.propName);
      return;
    }

    const msgData = this.extractUpdatePropertyMessageData(msg.tIsDead);
    if (typeof msgData !== 'boolean') {
      logError(this, `onDeathStateContainerMessage - Invalid data`, msgData);
      return;
    }

    if (msg.tChangeValues) {
      this.onChangeValuesMessage({ message: msg.tChangeValues });
    }
    once('update', () => this.onUpdatePropertyMessage({ message: msg.tIsDead }));

    if (msg.tTeleport) {
      this.onTeleportMessage({ message: msg.tTeleport });
    }

    once('update', () => {
      const actor =
        id === this.getWorldModel().playerCharacterFormIdx
          ? Game.getPlayer()!
          : Actor.from(Game.getFormEx(remoteIdToLocalId(form.refrId ?? 0)));
      if (actor) {
        try {
          this.controller.emitter.emit("applyDeathStateEvent", {
            actor: actor,
            isDead: msgData
          });
        } catch (e) {
          if (e instanceof RespawnNeededError) {
            actor.disableNoWait(false);
            actor.delete();
          } else {
            throw e;
          }
        }
      }
    });
  }

  private handleConnectionAccepted(): void {
    this.cancelOwnerSpawn();
    // THORNSWOOD. A new connection starts at 0, as the server does when it
    // attaches the character (PartOne::SetUserActor).
    storage[carriedOutTeleportSeqKey] = 0;
    this.teleportsOnTheirWay.clear();
    this.worldModel.forms = [];
    this.worldModel.playerCharacterFormIdx = -1;
    this.worldModel.playerCharacterRefrId = 0;

    logTrace(this, "Handle connection accepted");
  }

  private onChangeValuesMessage(event: ConnectionMessage<ChangeValuesMessage>): void {
    const msg = event.message;

    once('update', () => {
      const id = this.getIdManager().getId(msg.idx);
      const refr = id === this.getMyActorIndex() ? Game.getPlayer() : getObjectReference(id);
      const ac = Actor.from(refr);
      if (!ac) {
        return;
      }

      const { health, stamina, magicka } = msg.data;
      if (typeof health === "number") {
        setActorValuePercentage(ac, 'health', health);
      }
      if (typeof stamina === "number") {
        setActorValuePercentage(ac, 'stamina', stamina);
      }
      if (typeof magicka === "number") {
        setActorValuePercentage(ac, 'magicka', magicka);
      }
    });
  }

  private cancelOwnerSpawn(): void {
    this.ownerSpawnEpoch++;
    this.raceMenuRequest++;
    this.raceMenuRequested = false;
    this.finishOwnerSpawn?.(false);
    this.finishOwnerSpawn = undefined;
    this.ownerSpawnReady = undefined;
    this.ownerInventory = undefined;
    this.ownerInventoryPending = undefined;
    this.ownerInventoryIsSnapshot = false;
    this.ownerSpawnSettled = false;
    this.ownerOutfitIsReady = undefined;
    this.ownerSpawnTarget = undefined;
    delete storage['pcInv'];
    delete storage['ownerInventorySettling'];
  }

  private recordOwnerOutfit(stage: string): void {
    try {
      const pc = Game.getPlayer();
      const summarize = (inv?: Inventory) => (inv?.entries || []).slice(0, 32).map(entry => ({
        baseId: entry.baseId, count: entry.count, worn: !!entry.worn, wornLeft: !!entry.wornLeft,
      }));
      // The front writes these bounded transitions to its existing disk log.
      // Console-only readiness messages could not diagnose connected failures.
      storage['ownerOutfitDiagnostic'] = JSON.stringify({
        version: 'native-outfit-v1', at: Date.now(), stage, epoch: this.ownerSpawnEpoch,
        loaded: !!pc?.is3DLoaded(), race: pc?.getRace()?.getFormID() || 0,
        desired: summarize(this.ownerInventory), actual: pc ? summarize(getInventory(pc)) : [],
        cell: pc?.getParentCell()?.getFormID() || 0,
        world: pc?.getWorldSpace()?.getFormID() || 0,
        pendingQueue: !!this.ownerInventoryPending,
        queueComplete: this.ownerInventoryPending ? this.ownerInventoryPending() : true,
        target: this.ownerSpawnTarget,
        position: pc ? [pc.getPositionX(), pc.getPositionY(), pc.getPositionZ()] : undefined,
      });
      // Native exports (including storage) are recreated for each plugin.
      // This console reaches the disk log even when the front cannot see it.
      logTrace(this, 'Owner outfit', storage['ownerOutfitDiagnostic']);
    } catch (e) { logError(this, 'Could not capture owner outfit', e); }
  }

  private onSetRaceMenuOpenMessage(event: ConnectionMessage<SetRaceMenuOpenMessage>): void {
    this.raceMenuRequested = event.message.open;
    const request = ++this.raceMenuRequest;
    if (!event.message.open) {
      if (this.ownerSpawnSettled && !this.ownerInventoryPending) { delete storage['ownerInventorySettling']; }
      return;
    }
    if (this.worldModel.playerCharacterFormIdx < 0) { return; }
    const epoch = this.ownerSpawnEpoch;
    const ready = this.ownerSpawnReady;
    if (!ready || !this.ownerOutfitIsReady) {
      logError(this, 'Creation has no owner readiness state; waiting for a fresh owner packet');
      return;
    }
    storage['ownerInventorySettling'] = true;
    once('update', async () => {
      if (ready && !await ready) { return; }
      await Utility.wait(0.3);
      for (let attempt = 0; attempt < 120; attempt++) {
        if (epoch !== this.ownerSpawnEpoch || request !== this.raceMenuRequest || !this.raceMenuRequested) { return; }
        try {
          // A newer authoritative inventory may arrive after initial settlement.
          if (this.ownerOutfitIsReady && this.ownerOutfitIsReady()) {
            if (!Ui.isMenuOpen('RaceSex Menu')) {
              unequipIronHelmet();
              this.recordOwnerOutfit('before-face-menu');
              Game.showRaceMenu();
            }
            delete storage['ownerInventorySettling'];
            return;
          }
        } catch (e) {
          if (attempt === 0) { logError(this, 'Retrying menu outfit check after native failure', e); }
        }
        await Utility.wait(1);
      }
      logError(this, 'Menu outfit check did not settle; face menu stays closed');
    });
  }

  /** Packet handlers end **/

  getWorldModel(): WorldModel {
    return this.worldModel;
  }

  getMyActorIndex(): number {
    return this.worldModel.playerCharacterFormIdx;
  }

  // THORNSWOOD #1932. The teleportSeq of the newest teleport of the own
  // character this client has carried out on this connection, 0 before the
  // first. Kept in storage like worldModel so a script reload keeps it.
  getCarriedOutTeleportSeq(): number {
    const value = storage[carriedOutTeleportSeqKey];
    return typeof value === "number" ? value : 0;
  }

  getMyRemoteRefrId(): number {
    return this.worldModel.playerCharacterRefrId;
  }

  getIdManager() {
    return this.idManager_;
  }

  private get worldModel(): WorldModel {
    if (typeof storage["worldModel"] === "function") {
      storage["worldModel"] = { forms: [], playerCharacterFormIdx: -1, playerCharacterRefrId: 0 };
    }
    return storage["worldModel"] as WorldModel;
  }

  private get idManager_(): IdManager {
    if (typeof storage["idManager"] === "function") {
      // Note: full IdManager object preserved across hot-reloads, including methods.
      storage["idManager"] = new IdManager();
    }
    return storage["idManager"] as IdManager;
  }

  private onceLoad(
    refrId: number,
    callback: (refr: ObjectReference) => void,
    maxAttempts: number = 120,
  ) {
    once('update', () => {
      const refr = ObjectReference.from(Game.getFormEx(refrId));
      if (refr) {
        callback(refr);
      } else {
        maxAttempts--;
        if (maxAttempts > 0) {
          once('update', () => this.onceLoad(refrId, callback, maxAttempts));
        } else {
          logError(this, 'Failed to load object reference ' + refrId.toString(16));
        }
      }
    });
  };

  private skipFormViewCreation(
    msg: UpdatePropertyMessage | CreateActorMessage,
  ) {
    // Optimization added in #1186, however it doesn't work for doors for some reason
    return msg.refrId && msg.refrId < 0xff000000 && msg.baseRecordType !== 'DOOR';
  };

  private extractUpdatePropertyMessageData(updatePropertyMessage: UpdatePropertyMessage) {
    let msgData: unknown = updatePropertyMessage.data;

    if (updatePropertyMessage.dataDump !== undefined) {
      try {
        msgData = JSON.parse(updatePropertyMessage.dataDump);
      } catch (e) {
        if (e instanceof SyntaxError) {
          logError(this, 'extractUpdatePropertyMessageData - Failed to parse dataDump', updatePropertyMessage.dataDump);
          return;
        } else {
          throw e;
        }
      }
    }

    return msgData;
  }

  private onSpellCastMessage(event: ConnectionMessage<SpellCastMessage>): void {
    const msg = event.message;

    once('update', () => {
      const ac = Actor.from(Game.getFormEx(remoteIdToLocalId(msg.data.caster)));
      if (!ac) {
        return;
      }

      const actorAnimationVariables: ActorAnimationVariables = {
        booleans: new Uint8Array(msg.data.actorAnimationVariables.booleans),
        floats: new Uint8Array(msg.data.actorAnimationVariables.floats),
        integers: new Uint8Array(msg.data.actorAnimationVariables.integers)
      };

      if (msg.data.interruptCast) {
        interruptCast(ac.getFormID(), msg.data.castingSource, actorAnimationVariables);
        return;
      }

      const spell = ac.getEquippedSpell(msg.data.castingSource);
      if (spell) {
        castSpellImmediate(ac.getFormID(), msg.data.castingSource, spell.getFormID(), remoteIdToLocalId(msg.data.target),
          msg.data.aimAngle, msg.data.aimHeading, actorAnimationVariables);
      }
    });
  }

  private onUpdateAnimVariablesMessage(event: ConnectionMessage<UpdateAnimVariablesMessage>): void {
    const msg = event.message;

    once('update', () => {
      const ac = Actor.from(Game.getFormEx(remoteIdToLocalId(msg.data.actorRemoteId)));
      if (!ac) {
        return;
      }

      const actorAnimationVariables: ActorAnimationVariables = {
        booleans: new Uint8Array(msg.data.actorAnimationVariables.booleans),
        floats: new Uint8Array(msg.data.actorAnimationVariables.floats),
        integers: new Uint8Array(msg.data.actorAnimationVariables.integers)
      };

      const isApplyed = applyAnimationVariablesToActor(ac.getFormID(), actorAnimationVariables);

      if (!isApplyed) {
        logError(this, 'Failed apply AnimationVariables to actor with id: ' + ac.getFormID().toString(16));
      }
    });
  }

  private numSetInventory = 0;
  private teleportsOnTheirWay = new Map<number, { msg: TeleportMessage | TeleportMessage2, isMyCharacter: boolean }>();
  private ownerSpawnEpoch = 0;
  private ownerSpawnReady?: Promise<boolean>;
  private finishOwnerSpawn?: (ready: boolean) => void;
  private ownerInventory?: Inventory;
  private ownerInventoryPending?: () => boolean;
  private ownerInventoryIsSnapshot = false;
  private ownerSpawnSettled = false;
  private ownerOutfitIsReady?: () => boolean;
  private ownerSpawnTarget?: CreateActorMessage['transform'];
  private raceMenuRequested = false;
  private raceMenuRequest = 0;
}
