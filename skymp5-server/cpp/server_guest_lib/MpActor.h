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
  void InvalidateRuntimeIdentity() noexcept;
  bool HasRuntimeIdentity(uint64_t expected) const;

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

  void AddSpell(uint32_t spellId);
  void RemoveSpell(uint32_t spellId);

  void SetLastAnimEvent(const std::optional<AnimationData>& animationData);
  std::optional<AnimationData> GetLastAnimEvent() const;

private:
  struct Impl;
  std::shared_ptr<Impl> pImpl;
  bool factionsLoaded = false;

  void SendAndSetDeathState(bool isDead, bool shouldTeleport);
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
