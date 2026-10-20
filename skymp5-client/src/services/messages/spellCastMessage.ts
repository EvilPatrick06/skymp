import { MsgType } from "../../messages";

export interface SpellCastMsgData {
    caster: number
    target: number
    spell: number
    isDualCasting: boolean
    interruptCast: boolean
    castingSource: number
    aimAngle: number,
    aimHeading: number,
    actorAnimationVariables: {
        booleans: number[]
        floats: number[]
        integers: number[]
    }
    // THORNSWOOD PATCH (Thornswood #2848): how many times their usual time
    // this cast's effects hold. From the caster, what its own machine gave
    // them; from the server, what everybody else's gives them. Absent is the
    // game's own.
    durationMult?: number
}

export interface SpellCastMessage {
    t: MsgType.SpellCast,
    data: SpellCastMsgData
}
