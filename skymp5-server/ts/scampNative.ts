const scampNativeNode = require(process.cwd() + "/scam_native.node");

export declare interface Bot {
  destroy(): void;
  send(msg: Record<string, unknown>): void;
}

export type SendChatMessageFn = (
  formId: number,
  message: Record<string, unknown>
) => void;

export interface InventorySnapshot {
  entries: {
    baseId: number; count: number;
    health?: number; enchantmentId?: number; maxCharge?: number;
    removeEnchantmentOnUnequip?: boolean; chargePercent?: number;
    name?: string; soul?: number; poisonId?: number; poisonCount?: number;
    worn?: boolean; wornLeft?: boolean;
  }[];
}

export interface ScampServer {
  on(event: "connect", handler: (userId: number) => void): void;
  on(event: "disconnect", handler: (userId: number) => void): void;
  on(
    event: "customPacket",
    handler: (userId: number, content: string) => void
  ): void;
  attachSaveStorage(): void;
  tick(): void;
  compareAndSetInventory(formId: number, expected: InventorySnapshot,
    expectedReceipt: string, replacement: InventorySnapshot, sequence: number,
    expectedProfileId: number): boolean;
  /** Commits one hidden custom property, inventory and receipt in one save.
   * Each property value is limited to 16 KiB of serialized JSON. A publication
   * error may occur after commit; inspect the receipt before deciding outcome.
   */
  compareAndSetInventoryAndProperty(formId: number, expected: InventorySnapshot,
    expectedReceipt: string, replacement: InventorySnapshot, sequence: number,
    expectedProfileId: number,
    property: {name: string; expected: unknown; replacement: unknown}): boolean;
  getInventoryReceipt(formId: number): string;
  getSavedInventoryReceipt(formId: number): string;
  prepareNpcLoad(): number;
  loadNpcBatch(cursor: number, limit: number): {
    nextCursor: number;
    total: number;
    actorIds: number[];
  };
  setNpcServerControlled(formId: number, controlled: boolean): void;
  setNpcDifficultyTier(formId: number, tier: number): void;
  updateNpcMovement(formId: number, pos: number[], angle: number[], speed: number): void;
  stopNpcMovement(formId: number): void;
  getNavmeshRecords(cellOrWorldId: number, pos?: number[]): number[];
  getNpcAIState(formId: number): {
    pos: number[]; rot: number[]; cellOrWorld: number;
    isDead: boolean; isDisabled: boolean; isHuman: boolean; isConnected: boolean;
    profileId: number;
    isServerControlled: boolean; lifeGeneration: number; difficultyTier: number;
    meleeReach: number; meleeAllowance: number;
    canSwim: boolean; canFly: boolean; immobile: boolean;
    aggression: number; confidence: number; combatTarget: number;
    factions: { id: number; rank: number }[];
  };
  getFactionReactions(factionIds: number[]): {
    source: number; target: number; reaction: number;
  }[];
  serverNpcAttack(aggressorId: number, targetId: number): boolean;
  getLoadedFormCount(): number;
  setNpcPlacementExclusions(formIds: number[]): void;
  getLoadedNpcIds(): number[];

  createActor(
    formId: number,
    pos: number[],
    angleZ: number,
    cellOrWorld: number,
    userProfileId?: number
  ): number;

  destroyActor(formId: number): void;
  setUserActor(userId: number, actorFormId: number): void;
  getUserActor(userId: number): number;
  getUserGuid(userId: number): string;
  isConnected(userId: number): boolean;
  getActorName(actorId: number): string;
  getActorPos(actorId: number): number[];
  getActorCellOrWorld(actorId: number): number;
  setRaceMenuOpen(formId: number, open: boolean): void;
  sendCustomPacket(userId: number, jsonContent: string): void;
  setEnabled(actorId: number, enabled: boolean): void;
  getActorsByProfileId(profileId: number): number[];
  createBot(): Bot;
  getUserByActor(formId: number): number;
  getUserIp(userId: number): string;
  kick(userId: number): void;

  executeJavaScriptOnChakra(src: string): void;
  clear(): void;
  writeLogs(logLevel: string, message: string): void;
  getPrometheusMetrics(): string;
}

export const createScampServer = (serverSettings: Record<string, unknown>) => {
  const res = new scampNativeNode.ScampServer(JSON.stringify(serverSettings));
  res._setSelf(res);
  return res;
}

export const getScampNative = () => {
  return scampNativeNode;
}
