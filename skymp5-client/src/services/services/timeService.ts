import { ClientListener, CombinedController, Sp } from "./clientListener";

// Thornswood #3156: the world is 4E 252 in 2026 (Thornswood's lore/README.md),
// fifty years after the civil war, and a real year is a year of the Fourth Era,
// as a real day is a day of the calendar. Upstream counts 2020 as 4E 199.
const FOURTH_ERA_YEAR_IN_2026 = 252;

export function gameYearOf(date: Date) {
    return date.getUTCFullYear() - 2026 + FOURTH_ERA_YEAR_IN_2026;
}

export class TimeService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        controller.on("update", () => this.onUpdate());
    }

    public getTime() {
        const hoursOffsetSetting = this.sp.settings["skymp5-client"]["hoursOffset"];
        const hoursOffset = typeof hoursOffsetSetting === "number" ? hoursOffsetSetting : 0;
        const hoursOffsetMs = hoursOffset * 60 * 60 * 1000;

        const d = new Date(Date.now() + hoursOffsetMs);

        let newGameHourValue = 0;
        newGameHourValue += d.getUTCHours();
        newGameHourValue += d.getUTCMinutes() / 60;
        newGameHourValue += d.getUTCSeconds() / 60 / 60;
        newGameHourValue += d.getUTCMilliseconds() / 60 / 60 / 1000;
        return { newGameHourValue, date: d };
    }

    private every2seconds() {
        const gameHourId = 0x38;
        const gameMonthId = 0x36;
        const gameDayId = 0x37;
        const gameYearId = 0x35;
        const timeScaleId = 0x3a;

        const gameHour = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(gameHourId));
        const gameDay = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(gameDayId));
        const gameMonth = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(gameMonthId));
        const gameYear = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(gameYearId));
        const timeScale = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(timeScaleId));

        if (!gameHour || !gameDay || !gameMonth || !gameYear || !timeScale) {
            return;
        }

        const { newGameHourValue, date } = this.getTime();

        const diff = Math.abs(gameHour.getValue() - newGameHourValue);

        if (diff >= 1 / 60) {
            gameHour.setValue(newGameHourValue);
            gameDay.setValue(date.getUTCDate());
            gameMonth.setValue(date.getUTCMonth());
            gameYear.setValue(gameYearOf(date));
        }

        timeScale.setValue(gameHour.getValue() > newGameHourValue ? 0.6 : 1.2);
    }

    private onUpdate() {
        if (Date.now() - this.lastTimeUpd <= 2000) {
          return;
        }
        this.lastTimeUpd = Date.now();
        this.every2seconds();
    }

    private lastTimeUpd = 0;
}
