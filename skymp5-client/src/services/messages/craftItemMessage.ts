import { Inventory } from "skyrimPlatform";
import { MsgType } from "../../messages";
import { Made } from "../../sync/inventory";

export interface CraftItemMessage {
    t: MsgType.CraftItem,
    data: CraftItemMessageData
}

interface CraftItemMessageData {
    workbench: number;
    craftInputObjects: Inventory;
    resultObjectId: number;
    // Thornswood #1649: what is in a bottle the game brewed this session.
    resultPotion?: Made;
}
