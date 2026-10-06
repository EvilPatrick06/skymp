import { MsgType } from "../../messages";

export interface TeleportMessage2 {
    t: MsgType.Teleport2;
    pos: number[];
    rot: number[];
    worldOrCell: number;
    // THORNSWOOD. Present when this moves the client's own character; echoed
    // back in UpdateMovementMessage.data.teleportSeq once carried out.
    teleportSeq?: number;
}
