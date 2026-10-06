import { MsgType } from "../../messages";

export interface TeleportMessage {
    t: MsgType.Teleport;
    idx: number;
    pos: number[];
    rot: number[];
    worldOrCell: number;
    // THORNSWOOD. Present when this moves the client's own character; echoed
    // back in UpdateMovementMessage.data.teleportSeq once carried out.
    teleportSeq?: number;
}
