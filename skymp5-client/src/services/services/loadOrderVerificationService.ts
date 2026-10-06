import { Game, Utility, printConsole, createText, setTextSize } from "skyrimPlatform";
import { getScreenResolution } from "../../view/formView";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { Mod } from "../messages_http/serverManifest";
import { logTrace } from "../../logging";
import { SettingsService } from "./settingsService";
import {
  ClientMods,
  evaluateLoadOrder,
  screenNoticeForEval,
} from "./loadOrderCheck";

const STATE_KEY = 'loadOrderCheckState';

interface State {
  statusTextId?: number;
};

export class LoadOrderVerificationService extends ClientListener {
  constructor(private sp: Sp, private controller: CombinedController) {
    super();
    this.controller.once("update", () => this.onceUpdate());
  }

  private onceUpdate() {
    this.verifyLoadOrder();
  }

  private verifyLoadOrder() {
    const settingsService = this.controller.lookupListener(SettingsService);

    this.resetText();
    const clientMods = this.getClientMods();
    this.printModOrder('Client load order:', clientMods.mods);
    this.printModOrder('Client light plugins:', clientMods.light);
    return settingsService.getServerMods()
      .then((serverMods) => {
        const result = evaluateLoadOrder(clientMods, serverMods);

        if (result.kind === 'unreachable' || serverMods === null) {
          printConsole('Server mod list could not be fetched after retries.');
          const notice = screenNoticeForEval({ kind: 'unreachable' }, false)!;
          this.updateText(notice.text, notice.color, notice.clearDelay);
          return;
        }

        this.printModOrder('Server load order:', serverMods.mods);
        if (serverMods.light !== undefined) {
          this.printModOrder('Server light plugins:', serverMods.light);
        }

        if (result.kind === 'tooFewClientMods') {
          throw new Error(
            `Missing some server mods. Server has ${result.serverCount} ${result.list} plugins, we have ${result.clientCount}`,
          );
        }
        if (result.kind === 'tooManyClientMods') {
          const notice = screenNoticeForEval(result, false)!;
          this.updateText(notice.text, notice.color, notice.clearDelay);
          return;
        }
        if (result.kind === 'mismatch') {
          const server = result.list === 'light' ? serverMods.light! : serverMods.mods;
          const client = result.list === 'light' ? clientMods.light : clientMods.mods;
          for (const i of result.indices) {
            printConsole(`${i}-th ${result.list} plugin (numbered from 0) does not match.`);
            printConsole(`Server has ${JSON.stringify(server[i])}`);
            printConsole(`We have ${JSON.stringify(client[i])}`);
          }
          throw new Error(`Load order check failed! ${result.list} plugin indices: ` + JSON.stringify(result.indices));
        }
      })
      .catch((err) => {
        printConsole(err);
        const ignore = !!this.sp.settings['skymp5-client']['ignoreLoadOrderMismatch'];
        const notice = screenNoticeForEval({ kind: 'mismatch', list: 'full', indices: [] }, ignore)!;
        this.updateText(notice.text, notice.color, notice.clearDelay);
      });
  };

  private getState(): State {
    if (typeof this.sp.storage[STATE_KEY] !== 'object') {
      return {};
    }
    return this.sp.storage[STATE_KEY] as State;
  };

  private setState(replacement: State) {
    const oldState = this.sp.storage[STATE_KEY] = this.getState();
    for (const [k, v] of Object.entries(replacement)) {
      (oldState as Record<string, any>)[k] = v;
    }
  };

  private resetText() {
    let { statusTextId } = this.getState();
    if (statusTextId) {
      this.sp.destroyText(statusTextId);
      statusTextId = undefined;
      this.setState({ statusTextId });
    }
  };

  private updateText(text: string, color: [number, number, number, number], clearDelay?: number) {
    const { width, height } = getScreenResolution();
    this.resetText();
    const statusTextId = createText(width / 2, height / 2, text, color);
    setTextSize(statusTextId, 0.5);
    this.setState({ statusTextId });
    if (clearDelay) {
      Utility.wait(clearDelay).then(() => this.resetText());
    }
  }

  private enumerateClientMods(getCount: (() => number), getAt: ((idx: number) => string)) {
    const result = [];
    for (let i = 0; i < getCount(); ++i) {
      const filename = getAt(i);
      const { crc32, size } = this.getFileInfoSafe(filename);
      result.push({ filename, crc32, size });
    }
    return result;
  }

  // Both of the game's plugin lists: full plugins, then light plugins in
  // light index order.
  private getClientMods(): ClientMods {
    return {
      mods: this.enumerateClientMods(Game.getModCount, Game.getModName),
      light: this.enumerateClientMods(Game.getLightModCount, Game.getLightModName),
    };
  };

  private printModOrder(header: string, order: Mod[]) {
    printConsole(header);
    for (const [i, mod] of Object.entries(order)) {
      printConsole(`#${i} ${JSON.stringify(mod)}`);
    }
  };

  private getFileInfoSafe(filename: string) {
    try {
      return this.sp.getFileInfo(filename);
    } catch (e) {
      const message = (e as Record<string, unknown>).message;

      if (typeof message === "string" && message.includes('is not a valid argument')) {
        logTrace(this, `Failed to get file info for`, filename);
        return { crc32: 0, size: 0 };
      } else {
        throw e;
      }
    }
  }
}
