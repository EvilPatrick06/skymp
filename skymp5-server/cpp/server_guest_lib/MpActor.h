#pragma once
#include "AnimationData.h"
#include "Appearance.h"
#include "Equipment.h"
#include "GetBaseActorValues.h"
#include "MpObjectReference.h"
#include "libespm/espm.h"
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "DeathStateContainerMessage.h"

class WorldState;
struct ActorValues;
class RespawnEvent;
class ActiveMagicEffectsMap;
struct UpdateMovementMessage;

class MpActor : public MpObjectReference
{
public:
  friend class RespawnEvent;

  static const char* Type() { return "Actor"; }
  const char* GetFormType() const override { return "Actor"; }

  MpActor(const LocationalData& locationalData_,
          const FormCallbacks& calbacks_, uint32_t optBaseId = 0);

  const bool& IsRaceMenuOpen() const;
  const bool& IsDead() const;
  const bool& IsRespawning() const;
  // Process-local lifetime/ownership fence. Never written into a change form.
  uint64_t GetRuntimeIdentity() const noexcept;
  uint64_t GetRuntimeLifeIdentity() const noexcept;
  void InvalidateRuntimeIdentity(bool preserveLife = false) noexcept;
  // Revoke NPC authority derived from this human's ending session.
  void ReleaseHostedActors() noexcept;
  void AcceptClientHost();
  bool HasRuntimeIdentity(uint64_t expected) const;
  bool CompareAndSetActorProperty(int32_t expectedProfileId,
    const InventoryPropertyChange& property);

  bool IsServerControlled() const
  {
    return GetDynamicFields().GetValueDump("_skympServerControlled") == "true";
  }
  void SetServerControlled(bool controlled);
  void SetNpcDifficultyTier(int tier);
  int GetNpcDifficultyTier() const;
  float GetNpcHealthMultiplier() const;
  float GetNpcDamageMultiplier() const;
  void UpdateServerMovement(const NiPoint3& pos, const NiPoint3& angle,
                            float speed);
  void StopServerMovement();
  UpdateMovementMessage GetServerMovementMessage() const;
  uint32_t GetServerCombatTarget() const;
  void RecordServerCombatTarget(uint32_t targetId);
  void PublishServerAttackAnimation(const AnimationData& animation);

  bool IsSpellLearned(uint32_t spellId) const; // including from base
  bool IsSpellLearnedFromBase(uint32_t spellId) const;
  std::vector<uint32_t> GetSpellList() const;

  std::unique_ptr<const Appearance> GetAppearance() const;
  const std::string& GetAppearanceAsJson();
  bool HasStoredAppearance() const;
  bool ShouldPublishToOtherClients() const;
  bool ShouldPublishToOtherClients(bool hasAppearance) const;
  std::string GetLastAnimEventAsJson() const;
  const Equipment& GetEquipment() const;
  std::array<std::optional<Inventory::Entry>, 2> GetEquippedWeapon() const;
  std::array<std::optional<Inventory::Entry>, 2> GetEquippedScroll() const;
  std::array<std::optional<Inventory::Entry>, 2> GetEquippedLight() const;
  std::array<std::optional<Inventory::Entry>, 2> GetEquippedShield() const;
  uint32_t GetRaceId() const;
  bool IsWeaponDrawn() const;
  espm::ObjectBounds GetBounds() const;
  const std::vector<FormDesc>& GetTemplateChain() const;
  bool IsCreatedAsPlayer() const;
  const ActorValues& GetActorValues() const;
  const ActiveMagicEffectsMap& GetActiveMagicEffects() const;
  int32_t GetProfileId() const;

  float GetHealthRespawnPercentage() const;
  float GetMagickaRespawnPercentage() const;
  float GetStaminaRespawnPercentage() const;

  bool ShouldSkipRestoration() const noexcept;
  void UpdateNextRestorationTime(std::chrono::seconds duration) noexcept;

  void SetRaceMenuOpen(bool isOpen);
  void SetAppearance(const Appearance* newAppearance);
  void SetEquipment(const Equipment& newEquipment);

  void SetHealthRespawnPercentage(float percentage);
  void SetMagickaRespawnPercentage(float percentage);
  void SetStaminaRespawnPercentage(float percentage);

  void AddToFaction(Faction faction, bool lazyLoad = true);
  bool IsInFaction(FormDesc factionForm, bool lazyLoad = true);
  std::vector<Faction> GetFactions(int minFactionID, int maxFactionID,
                                   bool lazyLoad = true);
  void RemoveFromFaction(FormDesc factionForm, bool lazyLoad = true);

  void VisitProperties(CreateActorMessage& message,
                       VisitPropertiesMode mode) override;
  void Disable() override;

  void SendToUser(const IMessageBase& message, bool reliable);
  void SendToUserDeferred(const IMessageBase& message, bool reliable,
                          int deferredChannelId,
                          bool overwritePreviousChannelMessages);

  Networking::UserId GetUserId() const;

  // Returns the actor to send messages to, considering hosters.
  // If this actor is offline and has a hoster, returns the hoster.
  // Otherwise returns this actor.
  MpActor& GetActorToSendTo();

  [[nodiscard]] bool OnEquip(uint32_t baseId);

  // TODO: consider removing the entire DestroyEventSink feature because it's
  // only used in unit tests
  class DestroyEventSink
  {
  public:
    virtual ~DestroyEventSink() = default;
    virtual void BeforeDestroy(MpActor& actor) = 0;
  };

  void AddEventSink(std::shared_ptr<DestroyEventSink> sink);
  void RemoveEventSink(std::shared_ptr<DestroyEventSink> sink);

  class DisableEventSink
  {
  public:
    virtual ~DisableEventSink() = default;
    virtual void BeforeDisable(MpActor& actor) = 0;
  };

  void AddEventSink(std::shared_ptr<DisableEventSink> sink);
  void RemoveEventSink(std::shared_ptr<DisableEventSink> sink);

  MpChangeForm GetChangeForm() const override;
  void ApplyChangeForm(const MpChangeForm& changeForm) override;

  uint32_t NextSnippetIndex(
    std::optional<Viet::Promise<VarValue>> promise = std::nullopt);

  void ResolveSnippet(uint32_t snippetIdx, VarValue v);
  // SetPercentages kills on a health of nothing without asking the gamemode;
  // NetSetPercentages asks first (Thornswood #1626). SetPercentage asks.
  void SetPercentages(const ActorValues& actorValues,
                      MpActor* aggressor = nullptr);
  void SetPercentage(espm::ActorValue av, float percentage);
  void NetSendChangeValues(
    const ActorValues& actorValues,
    const std::optional<std::vector<espm::ActorValue>>& avFilter);
  void NetSetPercentages(
    const ActorValues& actorValues, MpActor* aggressor,
    const std::optional<std::vector<espm::ActorValue>>& avFilter);

  std::chrono::steady_clock::time_point GetLastAttributesPercentagesUpdate();
  std::chrono::steady_clock::time_point GetLastHitTime(
    std::optional<uint32_t> targetId) const;
  size_t CountRecentHits(std::chrono::duration<float> timeWindow) const;

  void SetLastAttributesPercentagesUpdate(
    std::chrono::steady_clock::time_point timePoint =
      std::chrono::steady_clock::now());
  void SetLastHitTime(uint32_t targetId,
                      std::chrono::steady_clock::time_point timePoint);

  std::chrono::duration<float> GetDurationOfAttributesPercentagesUpdate(
    std::chrono::steady_clock::time_point now);

  void Kill(MpActor* killer = nullptr, bool shouldTeleport = false);
  void Respawn(bool shouldTeleport = true);
  void RespawnWithDelay(bool shouldTeleport = true);
  void Teleport(const LocationalData& position);

  // THORNSWOOD #1932. Each teleport the server tells this actor's own client
  // to carry out gets a number, and the client echoes the newest number it
  // has carried out in every movement packet
  // (UpdateMovementMessage::Data::teleportSeq). Without it the server cannot
  // tell a packet sent before the client moved from one sent after. Measured
  // on dev on 5 Oct 2026: the server stalled for 48 s, a held door activation
  // then moved the server's copy outside the Bannered Mare, and each of the
  // 232 movement packets the client had sent from inside during the stall
  // was answered with its own TeleportMessage2. Returns std::nullopt when no
  // user is attached, because the message then goes to a hoster, which does
  // not echo it.
  std::optional<uint32_t> NumberTeleportForOwnClient();

  // True for a movement packet the client sent before carrying out the
  // newest numbered teleport. It describes the place the client was told to
  // leave, so it is neither applied, passed on, nor answered with another
  // teleport. A packet without the echo is never judged here.
  bool IsSentBeforeNewestTeleport(std::optional<uint32_t> carriedOut) const;

  // A newly attached client has carried out none of the numbers given so
  // far, so nothing is outstanding for it.
  void ForgetTeleportsForOwnClient();

  void SetSpawnPoint(const LocationalData& position);
  LocationalData GetSpawnPoint() const;
  LocationalData GetEditorLocationalData() const;
  const float GetRespawnTime() const;
  void SetRespawnTime(float time);

  void SetIsDead(bool isDead);

  void RestoreActorValue(espm::ActorValue av, float value);
  void DamageActorValue(espm::ActorValue av, float value);
  void SetActorValue(espm::ActorValue actorValue, float value);

  // TODO: only used in legacy MGEF implementation, remove when MGEF is
  // rewritten
  void SetActorValues(const ActorValues& actorValues);

  BaseActorValues GetBaseValues();
  BaseActorValues GetMaximumValues();

  void DropItem(const uint32_t baseId, const Inventory::Entry& entry);
  void SetIsBlockActive(bool isBlockActive);
  bool IsBlockActive() const;
  NiPoint3 GetViewDirection() const;
  void IncreaseBlockCount() noexcept;
  void ResetBlockCount() noexcept;
  uint32_t GetBlockCount() const noexcept;
  void ApplyMagicEffect(espm::Effects::Effect& effect,
                        bool hasSweetpie = false,
                        bool durationOverriden = false);
  void ApplyMagicEffects(std::vector<espm::Effects::Effect>& effects,
                         bool hasSweetpie = false,
                         bool durationOverriden = false);
  void RemoveMagicEffect(const espm::ActorValue actorValue);
  void RemoveAllMagicEffects();
  void ReapplyMagicEffects();

  bool GetConsoleCommandsAllowedFlag() const;
  void SetConsoleCommandsAllowedFlag(bool newValue);

  void EquipBestWeapon();

  // THORNSWOOD #1560. Replaces what this actor wears and tells everybody who
  // can see it. The change is counted here. See EquipmentBinding::Set.
  void ReplaceEquipment(Equipment newEquipment);

  // THORNSWOOD (Thornswood #1651). Gives the entries of this actor's
  // equipment record the names in `renamed`, when `renamed` is the record
  // with nothing but names changed: the same entries in the same order, the
  // same counts, extras, worn flags and spells. Returns false and changes
  // nothing otherwise. Nobody is told, because a name changes nothing anybody
  // sees on the actor. This is how the record follows the gamemode renaming
  // a piece in the pack; see EquipmentBinding::Set.
  bool RenameEquipment(const Equipment& renamed);

  void AddSpell(uint32_t spellId);
  void RemoveSpell(uint32_t spellId);

  void SetLastAnimEvent(const std::optional<AnimationData>& animationData);
  std::optional<AnimationData> GetLastAnimEvent() const;

  // THORNSWOOD PATCH (Thornswood #3159). A bench's stock: the containers a
  // crafting bench may take materials from for this actor. Patrick, 22 Oct
  // 2026: a bench draws materials from chests you own in the same building.
  // Which chests those are is the gamemode's to say, and it says it when the
  // bench is pressed (CraftStockBinding, the craftStock property). The stock
  // opens when this actor takes that bench (MpObjectReference, FURN) and
  // closes when it leaves it, and it is never saved.
  //
  // While it is open, this actor's own client is sent its pack with the
  // stock's materials in it (GetInventoryWithBenchStock, MpObjectReference::
  // SendInventoryUpdate), because the game's crafting menu only lists what it
  // can make from the pack, and sent again when one of the containers
  // changes (MpObjectReference::SetBenchStockReader). Nothing moves until a
  // craft: CraftService takes what the pack lacks from the stock's
  // containers, in the same step that takes the rest from the pack
  // (CraftEvent::OnFireSuccess). The materials the client was shown and did
  // not use go when the bench closes, because the server's own inventory is
  // sent then.
  struct BenchStock
  {
    uint32_t workbenchId = 0;
    std::vector<uint32_t> containerIds;
    bool open = false;
  };

  // Names the stock for the next time this actor takes that bench, or none.
  // A stock that was open closes.
  void SetBenchStock(std::optional<BenchStock> stock);
  const std::optional<BenchStock>& GetBenchStock() const;

  // The actor took this bench: the stock named for it opens, and the client
  // is sent the pack with it at once, ahead of whatever is sent next, so the
  // crafting menu that opens next lists it. A stock named for another bench
  // is dropped. Returns true when a stock opened.
  bool OpenBenchStock(uint32_t workbenchId);

  // The actor left this bench: its stock closes, and the client is sent the
  // server's inventory. Another bench's stock is left alone.
  void CloseBenchStock(uint32_t workbenchId);

  // The containers of the stock open at this bench that still exist, in the
  // order the gamemode named them, or none.
  std::vector<MpObjectReference*> GetOpenBenchStock(uint32_t workbenchId);

  // What this actor's own client is shown as its pack: the inventory, and
  // while a stock is open, each of its containers' entries that carry
  // nothing but a base and a count, the only kind a craft's inputs name
  // (Inventory::RemoveItems matches them exactly). An entry that would take
  // a stack past what a count holds is left out rather than thrown on.
  Inventory GetInventoryWithBenchStock();

private:
  struct Impl;
  std::shared_ptr<Impl> pImpl;
  bool factionsLoaded = false;

  bool SendAndSetDeathState(bool isDead, bool shouldTeleport);

  // Thornswood #1626. Fires onDying for a death a health write is about to
  // make. std::nullopt lets the death go ahead; a value is the health share
  // a listener's refusal keeps the actor alive at. The caller checks the
  // actor (and the killer) are still the same lives afterwards.
  std::optional<float> AskBeforeDeath(MpActor* killer);
  void SendServerStateToObservers(const IMessageBase& message,
                                  bool reliable) const;

  DeathStateContainerMessage GetDeathStateMsg(const LocationalData& position,
                                              bool isDead,
                                              bool shouldTeleport);

  void EatItem(uint32_t baseId, espm::Type t);

  bool ReadBook(uint32_t baseId);

  void ModifyActorValuePercentage(espm::ActorValue av, float percentageDelta);

  void EnsureTemplateChainEvaluated(
    espm::Loader& loader,
    ChangeFormGuard::Mode mode = ChangeFormGuard::Mode::RequestSave);

  std::map<uint32_t, uint32_t> EvaluateDeathItem();
  void AddDeathItem(uint64_t expectedRuntimeIdentity);
  void LoadFactions();

protected:
  void BeforeDestroy() override;
  void Init(WorldState* parent, uint32_t formId, bool hasChangeForm) override;
};
