import { ClientListener, Sp, CombinedController } from "./clientListener";

export class DisableDifficultySelectionService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        this.controller.on("update", () => this.onUpdate());
    }

    private onUpdate() {
        this.counter++;
        if (this.counter >= 60) {
            this.counter = 0;
            this.sp.Utility.setINIInt("iDifficulty:GamePlay", this.difficulty);
        }
    }

    /*
      THORNSWOOD PATCH. Expert, not Legendary.

      Upstream pins iDifficulty every sixty updates so nobody turns it down
      mid game. The pin stays; the number is Expert.
      0 Novice, 1 Apprentice, 2 Adept, 3 Expert, 4 Master, 5 Legendary.
    */
    private readonly difficulty = 3;

    private counter = 0;
}
