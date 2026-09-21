#pragma once
#include "ConditionsEvaluator.h" // ConditionsEvaluatorSettings
#include "FormIndex.h"
#include "Grid.h"
#include "GridElement.h"
#include "MpChangeForms.h"
#include "MpForm.h"
#include "MpObjectReference.h"
#include "NiPoint3.h"
#include "PartOneListener.h"
#include "condition_functions/ConditionFunctionMap.h"
#include "libespm/Loader.h"
#include "papyrus-vm/VirtualMachine.h"
#include "script_objects/MpFormGameObject.h"
#include <MakeID.h>
#include <Timer.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_map>

#ifdef AddForm
#  undef AddForm
#endif

class MpActor;
class FormCallbacks;
class MpChangeForm;
namespace Viet {
template <typename T, typename FormDescType, typename FilterType>
class ISaveStorage;
}
class IScriptStorage;
class GameModeEvent;

class WorldState
{
  friend class MpObjectReference;
  friend class MpActor;

public:
  using FormCallbacksFactory = std::function<FormCallbacks()>;

  struct NpcSettingsEntry
  {
    bool spawnInInterior = false;
    bool spawnInExterior = false;
    bool overriden = false;
  };

public:
  WorldState();
  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;

  void Clear();

  const std::chrono::steady_clock::time_point& GetStartPoint() const;

  void AttachEspm(espm::Loader* espm,
                  const FormCallbacksFactory& formCallbacksFactory);
  void AttachSaveStorage(
    std::shared_ptr<
      Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>
      saveStorage);
  void AttachScriptStorage(std::shared_ptr<IScriptStorage> scriptStorage);

  void AddForm(std::unique_ptr<MpForm> form, uint32_t formId,
               bool skipChecks = false,
               const MpChangeForm* optionalChangeFormToApply = nullptr);

  void LoadChangeForm(const MpChangeForm& changeForm,
                      const FormCallbacks& callbacks);

  void Tick();

  void RequestReloot(MpObjectReference& ref,
                     std::chrono::system_clock::duration time);

  void RequestSave(MpObjectReference& ref);
  bool HasEspmFile(std::string_view filename) const noexcept;

  template <typename T>
  Viet::Promise<Viet::Void> SetTimer(T&& duration,
                                     uint32_t* outTimerId = nullptr)
  {
    return timerRegular.SetTimer(std::forward<T>(duration), outTimerId);
  }

  template <typename T>
  Viet::Promise<Viet::Void> SetEffectTimer(T&& duration,
                                           uint32_t* outTimerId = nullptr)
  {
    return timerEffects.SetTimer(std::forward<T>(duration), outTimerId);
  }

  bool RemoveTimer(uint32_t timerId);
  Viet::Promise<Viet::Void> SetTimer(
    std::reference_wrapper<const std::chrono::system_clock::time_point>
      wrapper);
  Viet::Promise<Viet::Void> SetEffectTimer(
    std::reference_wrapper<const std::chrono::system_clock::time_point>
      wrapper);
  bool RemoveEffectTimer(uint32_t timerId);

  // Loads a requested form, likely resulting in whole chunk
  // loading if not yet loaded
  const std::shared_ptr<MpForm>& LookupFormById(
    uint32_t formId, std::stringstream* optionalOutTrace = nullptr);

  // No loading
  MpForm* LookupFormByIdx(int idx);

  // No loading version of LookupFormById
  const std::shared_ptr<MpForm>& LookupFormByIdNoLoad(uint32_t formId);

  void SendPapyrusEvent(MpForm* form, const char* eventName,
                        const VarValue* arguments, size_t argumentsCount);

  const std::set<MpObjectReference*>& GetNeighborsByPosition(
    uint32_t cellOrWorld, int16_t cellX, int16_t cellY);

  std::shared_ptr<std::vector<uint32_t>> GetAllForms(uint32_t modIndex);

  // See LookupFormById comment
  template <class F>
  F& GetFormAt(uint32_t formId)
  {
    const std::shared_ptr<MpForm>& form = LookupFormById(formId);
    if (!form) {
      throw std::runtime_error(
        fmt::format("Form with id {:#x} doesn't exist", formId));
    }

    F* typedForm = nullptr;

    if constexpr (std::is_same_v<F, MpActor>) {
      typedForm = form->AsActor();
    } else if constexpr (std::is_same_v<F, MpObjectReference>) {
      typedForm = form->AsObjectReference();
    } else {
      typedForm = dynamic_cast<F*>(form.get());
    }

    if (!typedForm) {
      if constexpr (std::is_same_v<F, MpActor>) {
        if (auto ref = std::dynamic_pointer_cast<MpObjectReference>(form)) {
          auto pos = ref->GetPos();
          spdlog::warn(
            "Specified Form is ObjectReference, but we tried to treat it as "
            "Actor, likely because of a client bug. formId={:#x}, "
            "baseId={:#x}, pos is {:.2f} {:.2f} {:.2f} at {}",
            formId, ref->GetBaseId(), pos.x, pos.y, pos.z,
            ref->GetCellOrWorld().ToString());
        }
      }
      throw std::runtime_error(
        fmt::format("Form with id {:#x} is not {} (actually it is {})", formId,
                    F::Type(), MpForm::GetFormType(&*form)));
    }

    return *typedForm;
  };

  template <class FormType = MpForm>
  void DestroyForm(uint32_t formId,
                   std::shared_ptr<FormType>* outDestroyedForm = nullptr)
  {
    auto it = forms.find(formId);
    if (it == forms.end()) {
      throw std::runtime_error(
        static_cast<const std::stringstream&>(std::stringstream()
                                              << "Form with id " << std::hex
                                              << formId << " doesn't exist")
          .str());
    }

    auto& form = it->second;
    if (!dynamic_cast<FormType*>(form.get())) {
      std::stringstream s;
      s << "Expected form " << std::hex << formId << " to be "
        << MpForm::GetFormType<FormType>() << ", but got "
        << MpForm::GetFormType(form.get());
      throw std::runtime_error(s.str());
    }

    if (outDestroyedForm)
      *outDestroyedForm = std::dynamic_pointer_cast<FormType>(it->second);

    it->second->BeforeDestroy();

    if (auto formIndex = dynamic_cast<FormIndex*>(form.get())) {
      if (formIdxManager && !formIdxManager->DestroyID(formIndex->idx))
        throw std::runtime_error("DestroyID failed");
    }

    forms.erase(it);
  };

  espm::Loader& GetEspm() const;
  bool HasEspm() const;
  espm::CompressedFieldsCache& GetEspmCache();
  IScriptStorage* GetScriptStorage() const;
  VirtualMachine& GetPapyrusVm();
  const std::set<uint32_t>& GetActorsByProfileId(int32_t profileId) const;
  const std::set<uint32_t>& GetActorsByPrivateIndexedProperty(
    const std::string& privateIndexedPropertyMapKey) const;
  std::string MakePrivateIndexedPropertyMapKey(
    const std::string& propertyName,
    const std::string& propertyValueStringified);
  uint32_t GenerateFormId();
  void SetRelootTime(const std::string& recordType,
                     std::chrono::system_clock::duration time);
  std::optional<std::chrono::system_clock::duration> GetRelootTime(
    const std::string& recordType) const;

  // Utility function to check if the provided baseId has the certain keyword
  bool HasKeyword(uint32_t baseId, const char* keyword);

  // Only for tests
  auto& GetGrids() { return grids; }

  void SetNpcSettings(
    std::unordered_map<std::string, NpcSettingsEntry>&& settings);
  void SetForbiddenRelootTypes(const std::set<std::string>& types);
  void SetEnableConsoleCommandsForAllSetting(bool enable);

public:
  std::vector<std::string> espmFiles;
  std::unordered_map<int32_t, std::set<uint32_t>> actorIdByProfileId;
  std::unordered_map<std::string, std::set<uint32_t>>
    actorIdByPrivateIndexedProperty;
  std::shared_ptr<spdlog::logger> logger;
  std::vector<std::shared_ptr<PartOneListener>> listeners;
  std::unordered_map<uint32_t, uint32_t> hosters;
  std::unordered_map<uint32_t, std::map<uint32_t, float>>
    activationChildsByActivationParent;
  std::vector<std::optional<std::chrono::system_clock::time_point>>
    lastMovUpdateByIdx;

  bool isPapyrusHotReloadEnabled = false;

  bool npcEnabled = false;
  std::unordered_map<std::string, NpcSettingsEntry> npcSettings;
  NpcSettingsEntry defaultSetting;
  bool enableConsoleCommandsForAll = false;

  bool disableVanillaScriptsInExterior = true;

  // THORNSWOOD. The three gates below decide which of the world's NPCs the
  // server is willing to take ownership of, and all three used to be settled
  // in C++ with no way to reach them. That meant a rebuild to answer a
  // question, which is the wrong price for a question you have to ask several
  // times before you get the answer right. They are read out of
  // server-settings.json now: see ScampServer.cpp.
  //
  // npcAllowEssential   essential, protected and unique actors. Off by
  //                     default, because these are the quest critical ones and
  //                     the server respawns them at their editor location with
  //                     a fresh inventory rather than letting them bleed out
  //                     the way the game does.
  // npcAllowCrimeFaction  members of any crime faction, which is every guard
  //                     and most townsfolk. Off by default: guards want a
  //                     crime system to react to and the server has none.
  bool npcAllowEssential = false;
  bool npcAllowCrimeFaction = false;

  // THORNSWOOD. Equipment validation, see ActionListener::OnUpdateEquipment.
  // Both default to upstream behaviour; a modded load order will want them
  // off, because both reject on grounds mod content trips by design.
  bool equipmentSlotCheckEnabled = true;
  bool equipmentSpellCheckEnabled = true;

  // THORNSWOOD. The third one, and the one that actually fires here. Upstream
  // refuses the WHOLE equipment update when any single worn item is one the
  // server has no record of, so one unknown item takes everything else off
  // with it. Measured on the live server on 20 September: 24 refusals over two
  // days, every one of them the same item, 0x13790 IronWarAxe, on every
  // character within seconds of spawning. Skyrim Unbound is in the load order
  // and hands out starting gear inside the client's own game, which the server
  // never learns about, so the very first equipment update a new character
  // sends is refused and every one after it is too.
  //
  // With this off, an item the server does not know about is dropped from the
  // update and the rest of it is accepted, which is the difference between
  // wearing everything but the axe and wearing nothing at all.
  bool equipmentInventoryCheckEnabled = true;

  // Races the server refuses to take over. The playable ten are deliberately
  // NOT in this default any more. They were, and since every human, elf, orc,
  // khajiit and argonian in the game is one of them, that single line was the
  // whole reason a world with npcEnabled turned on still had nobody in it.
  // The only upstream test for this feature is a cow, which is the tell.
  std::vector<uint32_t> bannedEspmCharacterRaceIds = {
    0x000e7713, 0x00012e82, 0x001052a3, 0x00088884, 0x0008883a, 0x00088846,
    0x00108272, 0x000a82b9, 0x0008883c, 0x00088794, 0x00088845, 0x0008883d,
    0x00088844, 0x00088840, 0x000a82ba,

    /* Mannequin */
    0x0010760a
  };

  ConditionsEvaluatorSettings conditionsEvaluatorSettings;
  ConditionFunctionMap conditionFunctionMap;
  std::vector<GameModeEvent*> currentGameModeEventsStack;

private:
  bool AttachEspmRecord(const espm::CombineBrowser& br,
                        const espm::RecordHeader* record,
                        const espm::IdMapping& mapping,
                        std::stringstream* optionalOutTrace = nullptr);

  bool LoadForm(uint32_t formId,
                std::stringstream* optionalOutTrace = nullptr);
  void TickSaveStorage(const std::chrono::system_clock::time_point& now);
  void TickTimers(const std::chrono::system_clock::time_point& now);
  [[nodiscard]] bool NpcSourceFilesOverriden() const noexcept;
  [[nodiscard]] bool IsNpcAllowed(uint32_t refrId) const noexcept;
  [[nodiscard]] uint32_t GetFileIdx(uint32_t formId) const noexcept;
  [[nodiscard]] bool IsRelootForbidden(std::string type) const noexcept;

private:
  struct GridInfo
  {
    std::shared_ptr<GridImpl<MpObjectReference*>> grid =
      std::make_shared<GridImpl<MpObjectReference*>>();
    std::map<int16_t, std::map<int16_t, bool>> loadedChunks;
  };

  std::unordered_map<uint32_t, std::shared_ptr<MpForm>> forms;
  std::unordered_map<std::string, size_t> loadOrderMap;
  std::unordered_map<uint32_t, GridInfo> grids;
  std::unique_ptr<MakeID> formIdxManager;
  std::vector<MpObjectReference*> refrByIdxUnreliable;
  espm::Loader* espm = nullptr;
  FormCallbacksFactory formCallbacksFactory;
  std::unique_ptr<espm::CompressedFieldsCache> espmCache;

  struct Impl;
  std::shared_ptr<Impl> pImpl;
  Viet::Timer timerEffects, timerRegular;
  std::chrono::steady_clock::time_point worldStartTime;
};
