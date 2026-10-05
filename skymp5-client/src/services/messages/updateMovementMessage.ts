import { MsgType } from "../../messages";
import { Movement } from "../../sync/movement";

export interface UpdateMovementMessage {
    t: MsgType.UpdateMovement;
    idx: number;
    // THORNSWOOD. teleportSeq: own character only, see
    // RemoteServer.getCarriedOutTeleportSeq.
    data: Movement & { teleportSeq?: number };
}
