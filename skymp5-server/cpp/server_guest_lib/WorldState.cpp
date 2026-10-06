#include "WorldState.h"
#include "EvaluateTemplate.h"
#include "FormCallbacks.h"
#include "LeveledListUtils.h"
#include "LocationalDataUtils.h"
#include "MpActor.h"
#include "MerchantTransferController.h"
#include "MpChangeForms.h"
#include "MpObjectReference.h"
#include "libespm/GroupUtils.h"
#include "papyrus-vm/Reader.h"
#include "papyrus-vm/Utils.h"
#include "script_classes/PapyrusClassesFactory.h"
#include "script_compatibility_policies/PapyrusCompatibilityPolicyFactory.h"
#include "script_storages/IScriptStorage.h"
#include <ScopedTask.h>
#include <Timer.h>
#include <algorithm>
#include <antigo/Context.h>
#include <deque>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iterator>
#include <optional>
#include <save_storages/AsyncSaveStorage.h> // UpsertFailedException
#include <save_storages/ISaveStorage.h>
#include <unordered_map>
#include <unordered_set>

namespace {

struct RelootTimeForTypesEntry
{
  std::string recordType;
  std::chrono::system_clock::duration time;

  Viet::Timer timer;
};
}

struct WorldState::Impl
{
  std::shared_ptr<MerchantTransferController> merchantTransfers;
  using ReceiptKey = std::pair<FormDesc, int32_t>;
  struct SavedReceipt
  {
    std::string dump;
    std::weak_ptr<MpForm> form;
  };
  std::map<ReceiptKey, SavedReceipt> savedInventoryReceipts;
  std::vector<std::optional<MpChangeForm>> changesByIdx;
  bool changesByIdxEmpty = true;

  std::shared_ptr<
    Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>
    saveStorage;
  std::shared_ptr<IScriptStorage> scriptStorage;
  bool saveStorageBusy = false;
  std::shared_ptr<VirtualMachine> vm;
  uint32_t nextId = 0xff000000;
  // The server's own forms (ff...) whose saved ChangeForm LoadChangeForm could
  // not read, by form id, with the profile the record belongs to. They are
  // not loaded, and GenerateFormId never hands out one of their ids, so the
  // record on disk is kept as it is instead of being overwritten by the next
  // new character (Thornswood #1269).
  std::map<uint32_t, int32_t> unreadableChangeForms;
  std::shared_ptr<IPapyrusCompatibilityPolicy> policy;
  std::unordered_map<uint32_t, MpChangeForm> changeFormsForDeferredLoad;
  bool chunkLoadingInProgress = false;
  bool formLoadingInProgress = false;
  std::vector<RelootTimeForTypesEntry> relootTimeForTypes;
  // The clock SetTimerClock gave, for reloot timers made after it.
  Viet::Timer::Clock timerClock;
  std::set<std::string> forbiddenRelootTypes;
  std::vector<std::unique_ptr<IPapyrusClassBase>> classes;
  std::array<std::shared_ptr<std::vector<uint32_t>>, 0x100>
    allFormsByModIndexCache;
  std::vector<uint32_t> attachEspmRecordFailures;
  std::optional<std::vector<uint32_t>> placedActorIds;
  std::unordered_set<uint32_t> npcPlacementExclusions;
};

WorldState::~WorldState()
{
  pImpl->merchantTransfers.reset();
  pImpl->savedInventoryReceipts.clear();
}

WorldState::WorldState()
  : worldStartTime(std::chrono::steady_clock::now())
{
  logger.reset(new spdlog::logger("empty logger"));

  pImpl.reset(new Impl);
  pImpl->policy = PapyrusCompatibilityPolicyFactory::Create(this);
  pImpl->merchantTransfers = std::make_shared<MerchantTransferController>(*this);
}

void WorldState::Clear()
{
  pImpl->savedInventoryReceipts.clear();
  if (pImpl->merchantTransfers) {
    pImpl->merchantTransfers = std::make_shared<MerchantTransferController>(*this);
  }
  forms.clear();
  grids.clear();
  formIdxManager.reset();
}

const std::chrono::steady_clock::time_point& WorldState::GetStartPoint() const
{
  return worldStartTime;
}

void WorldState::AttachEspm(espm::Loader* espm_,
                            const FormCallbacksFactory& formCallbacksFactory_)
{
  espm = espm_;
  formCallbacksFactory = formCallbacksFactory_;
  espmCache.reset(new espm::CompressedFieldsCache);
  espmFiles = espm->GetFileNames();
  pImpl->placedActorIds.reset();
}

void WorldState::AttachSaveStorage(
  std::shared_ptr<
    Viet::ISaveStorage<MpChangeForm, FormDesc, std::vector<FormDesc>>>
    saveStorage)
{
  spdlog::info("AttachSaveStorage - db fixes installed");

  pImpl->saveStorage = saveStorage;
}

void WorldState::AttachScriptStorage(
  std::shared_ptr<IScriptStorage> scriptStorage)
{
  pImpl->scriptStorage = scriptStorage;
}

void WorldState::AddForm(std::unique_ptr<MpForm> form, uint32_t formId,
                         bool skipChecks,
                         const MpChangeForm* optionalChangeFormToApply)
{
  if (!skipChecks && forms.find(formId) != forms.end()) {
    throw std::runtime_error(
      fmt::format("Form with id {:x} already exists", formId));
  }

  // Assign formIndex before Init
  if (auto refr = form->AsObjectReference()) {
    if (!formIdxManager) {
      formIdxManager.reset(new MakeID(FormIndex::g_invalidIdx - 1));
    }

    if (!formIdxManager->CreateID(refr->idx)) {
      throw std::runtime_error("CreateID failed");
    }

    if (refrByIdxUnreliable.size() <= refr->GetIdx()) {
      refrByIdxUnreliable.resize(refr->GetIdx() + 1, nullptr);
    }
    refrByIdxUnreliable[refr->GetIdx()] = refr;
  }

  // MpObjectReference::Init requests save for newly created forms. That's why
  // we want formIndex to be assigned before init.
  form->Init(this, formId, optionalChangeFormToApply != nullptr);

  auto it = forms.insert({ formId, std::move(form) }).first;

  if (optionalChangeFormToApply) {
    auto refr = it->second->AsObjectReference();
    if (!refr) {
      forms.erase(it); // Rollback changes due to exception
      throw std::runtime_error(
        "Unable to apply ChangeForm, cast to ObjectReference failed");
    }
    refr->ApplyChangeForm(*optionalChangeFormToApply);
  }
}

void WorldState::Tick()
{
  const auto now = std::chrono::system_clock::now();
  TickSaveStorage(now);
  TickTimers(now);
}

void WorldState::LoadChangeForm(const MpChangeForm& changeForm,
                                const FormCallbacks& callbacks)
{
  Viet::ScopedTask<bool> task([](bool& st) { st = false; },
                              pImpl->formLoadingInProgress);
  pImpl->formLoadingInProgress = true;

  std::unique_ptr<MpObjectReference> form;

  const auto baseId = changeForm.baseDesc.ToFormId(espmFiles);
  const auto formId = changeForm.formDesc.ToFormId(espmFiles);

  std::string baseType = "STAT";
  if (espm) {
    const auto rec = espm->GetBrowser().LookupById(baseId).rec;

    if (!rec) {
      spdlog::warn("Skipping ChangeForm {:x}: unable to find base record {:x}",
                   formId, baseId);
      KeepUnreadableChangeForm(formId, changeForm.profileId);
      return;
    }
    baseType = rec->GetType().ToString();
  }

  if (espm) {
    if (!changeForm.appearanceDump.empty()) {
      simdjson::dom::parser p;
      auto res = p.parse(changeForm.appearanceDump);
      if (res.error()) {
        spdlog::warn(
          "Skipping ChangeForm {:x}: unable to parse appearanceDump", formId);
        KeepUnreadableChangeForm(formId, changeForm.profileId);
        return;
      }

      Appearance appearance;
      try {
        appearance = Appearance::FromJson(res.value());
      } catch (std::exception& e) {
        spdlog::warn("Skipping ChangeForm {:x}: Appearance::FromJson failed "
                     "on appearanceDump: {}",
                     formId, e.what());
        KeepUnreadableChangeForm(formId, changeForm.profileId);
        return;
      }

      const auto rec = espm->GetBrowser().LookupById(appearance.raceId).rec;

      if (!rec || rec->GetType().ToString() != "RACE") {
        spdlog::warn("Skipping ChangeForm {:x}: bad raceId {:x} in "
                     "appearanceDump",
                     formId, appearance.raceId);
        KeepUnreadableChangeForm(formId, changeForm.profileId);
        return;
      }
    }
  }

  if (formId < 0xff000000) {
    // An asynchronous save can arrive after population policy is installed.
    // It must not restore enabled state or client ownership on a cut identity.
    if (pImpl->npcPlacementExclusions.count(formId)) return;
    auto it = forms.find(formId);
    if (it != forms.end()) {
      auto refr = std::dynamic_pointer_cast<MpObjectReference>(it->second);
      if (refr) {
        refr->ApplyChangeForm(changeForm);
      }
    } else {
      pImpl->changeFormsForDeferredLoad[formId] = changeForm;
    }
    return;
  }

  switch (changeForm.recType) {
    case MpChangeForm::ACHR:
      form.reset(new MpActor(LocationalData(), callbacks, baseId));
      break;
    case MpChangeForm::REFR:
      form.reset(new MpObjectReference(LocationalData(), callbacks, baseId,
                                       baseType.data()));
      break;
    default:
      throw std::runtime_error("Unknown ChangeForm type: " +
                               std::to_string(changeForm.recType));
  }

  auto formRawPtr = reinterpret_cast<MpObjectReference*>(form.get());
  AddForm(std::move(form), formId, false, &changeForm);
  auto idx = formRawPtr->GetIdx();

  // EnsureBaseContainerAdded forces saving here.
  // We do not want characters to save when they are load partially
  // This behaviour results in
  // https://github.com/skyrim-multiplayer/issue-tracker/issues/64

  // So we expect that RequestSave does nothing in this case:

  if (pImpl->changesByIdx.size() > idx &&
      pImpl->changesByIdx[idx] != std::nullopt) {
    assert(false);

    // For Release configuration we just manually remove formId from changes
    pImpl->changesByIdx[idx] = std::nullopt;
  }
}

void WorldState::RequestReloot(MpObjectReference& ref,
                               std::chrono::system_clock::duration time)
{
  auto refPtr = &ref;

  std::optional<Viet::Promise<Viet::Void>> timer;

  constexpr auto kEpsilon = std::chrono::milliseconds(1);

  for (auto& entry : pImpl->relootTimeForTypes) {
    auto diff = entry.time - time;
    if (diff < kEpsilon && diff > -kEpsilon) {
      timer = entry.timer.SetTimer(time, nullptr);
      break;
    }
  }

  if (timer == std::nullopt) {
    timer = SetTimer(time);
  }

  timer->Then([this, refPtr, formId = ref.GetFormId()](Viet::Void) {
    auto& form = LookupFormById(formId);
    auto reference = form->AsObjectReference();
    // Check if the reference is still the same, not re-created with the same
    // id for example
    if (reference && reference == refPtr) {
      reference->DoReloot();
    }
  });
}

void WorldState::RequestSave(MpObjectReference& ref)
{
  if (pImpl->formLoadingInProgress) {
    return;
  }

  auto idx = ref.GetIdx();

  [[unlikely]] if (idx == FormIndex::g_invalidIdx) {
    return spdlog::error("RequestSave {:x} - Invalid index", ref.GetFormId());
  }

  if (pImpl->changesByIdx.size() <= idx) {
    pImpl->changesByIdx.resize(idx + 1);
  }

  pImpl->changesByIdx[idx] = ref.GetChangeForm();
  pImpl->changesByIdxEmpty = false;
}

const std::shared_ptr<MpForm>& WorldState::LookupFormById(
  uint32_t formId, std::stringstream* optionalOutTrace)
{
  static const std::shared_ptr<MpForm> kNullForm;

  if (optionalOutTrace) {
    *optionalOutTrace << "searching for " << std::hex << formId << std::endl;
  }

  auto it = forms.find(formId);
  if (it == forms.end()) {
    if (formId < 0xff000000) {
      if (LoadForm(formId, optionalOutTrace)) {
        it = forms.find(formId);
        if (it != forms.end()) {
          if (optionalOutTrace) {
            *optionalOutTrace << "found after successful LoadForm" << std::hex
                              << formId << std::endl;
          }
          return it->second;
        }
        if (optionalOutTrace) {
          *optionalOutTrace << "not found after successful LoadForm"
                            << std::hex << formId << std::endl;
        }
        return kNullForm;
      } else {
        if (optionalOutTrace) {
          *optionalOutTrace << "LoadForm returned false " << std::hex << formId
                            << std::endl;
        }
      }
    }
    if (optionalOutTrace) {
      *optionalOutTrace << "not found " << std::hex << formId << std::endl;
    }
    return kNullForm;
  }

  if (optionalOutTrace) {
    *optionalOutTrace << "found " << std::hex << formId << std::endl;
  }
  return it->second;
}

const std::shared_ptr<MpForm>& WorldState::LookupFormByIdNoLoad(
  uint32_t formId)
{
  static const std::shared_ptr<MpForm> kNullForm;

  auto it = forms.find(formId);
  if (it == forms.end()) {
    return kNullForm;
  }

  return it->second;
}

bool WorldState::AttachEspmRecord(const espm::CombineBrowser& br,
                                  const espm::RecordHeader* record,
                                  const espm::IdMapping& mapping,
                                  std::stringstream* optionalOutTrace)
{
  auto& cache = GetEspmCache();
  // this place is a hotpath.
  // we want to use reinterpret_cast<> instead of espm::Convert<>
  // in order to reduce amount of generated assembly code
  auto* refr = reinterpret_cast<const espm::REFR*>(record);
  auto data = refr->GetData(cache);

  uint32_t baseId = espm::utils::GetMappedId(data.baseId, mapping);
  espm::LookupResult base = br.LookupById(baseId);
  if (!base.rec) {
    logger->info("baseId {} {}", baseId, static_cast<const void*>(base.rec));
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - base record not found {:x} \n", baseId);
    }
    return false;
  }

  espm::Type t = base.rec->GetType();
  bool isNpc = t == "NPC_";
  bool isFurniture = t == "FURN";
  bool isActivator = t == "ACTI";
  bool isDoor = t == "DOOR";
  bool isContainer = t == "CONT";
  bool isFlor = t == "FLOR" &&
    reinterpret_cast<const espm::FLOR*>(base.rec)->GetData(cache).resultItem;
  bool isTree = t == "TREE" &&
    reinterpret_cast<const espm::TREE*>(base.rec)->GetData(cache).resultItem;

  if (!isNpc && !isFurniture && !isActivator && !espm::utils::IsItem(t) &&
      !isDoor && !isContainer && !isFlor && !isTree) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips base type {} \n", t.ToString());
    }
    return false;
  }

  bool startsDead = false;
  if (isNpc) {
    auto* achr = reinterpret_cast<const espm::ACHR*>(record);
    startsDead = achr->StartsDead();
    if (startsDead) {
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format(
          "AttachEspmRecord - the server skips dead actors\n");
      }
      return false; // TODO: Load dead references
    }
  }

  // TODO: Load disabled references
  enum
  {
    InitiallyDisabled = 0x800,
    DeletedRecord = 0x20
  };

  if (refr->GetFlags() & InitiallyDisabled) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips initially disabled references\n");
    }
    return false;
  }

  if (refr->GetFlags() & DeletedRecord) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips deleted references\n");
    }
    return false;
  }

  if (!npcEnabled && isNpc) {
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format(
        "AttachEspmRecord - the server skips npcs: npcEnabled = false\n");
    }
    return false;
  }

  uint32_t formId = espm::utils::GetMappedId(record->GetId(), mapping);

  if (isNpc) {
    if (NpcSourceFilesOverriden() && !IsNpcAllowed(formId)) {
      spdlog::trace("Skip NPC loading, it is not allowed. refrId {:#x}",
                    formId);
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format("Skip NPC loading, it is not allowed. refrId {:#x}",
                         formId)
          << std::endl;
      }
      return false;
    }
    auto npcData =
      reinterpret_cast<const espm::NPC_*>(base.rec)->GetData(cache);

    // THORNSWOOD. Was unconditional. Now a setting, npcAllowEssential.
    if (!npcAllowEssential &&
        (npcData.isEssential || npcData.isProtected || npcData.isUnique)) {
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format("Skip NPC due to its flags")
                          << std::endl;
      }
      return false;
    }

    // THORNSWOOD. Was unconditional. Now a setting, npcAllowCrimeFaction.
    // Every guard and most townsfolk are in a crime faction, so this gate on
    // its own empties the holds.
    if (!npcAllowCrimeFaction) {
      enum class ListType : uint32_t
      {
        CrimeFactionsList = 0x26953
      };

      auto factionBaseId = static_cast<std::underlying_type_t<ListType>>(
        ListType::CrimeFactionsList);
      espm::LookupResult res = br.LookupById(factionBaseId);
      auto* formList = reinterpret_cast<const espm::FLST*>(res.rec);
      std::vector<uint32_t> factionFormIds = formList->GetData(cache).formIds;
      for (auto& factionFormId : factionFormIds) {
        factionFormId = res.ToGlobalId(factionFormId);
      }

      for (auto fact : npcData.factions) {
        auto it = std::find(factionFormIds.begin(), factionFormIds.end(),
                            base.ToGlobalId(fact.formId));
        if (it != factionFormIds.end()) {
          logger->info("Skipping actor {:#x} because it's in faction {:#x}",
                       record->GetId(), *it);
          if (optionalOutTrace) {
            *optionalOutTrace << fmt::format("Skip NPC due to faction")
                              << std::endl;
          }
          return false;
        }
      }
    }

    const int kPcLevel = 0; // Shouldn't mean much to races

    // May be not exact the same template chain as it would be in MpActor
    // but it's ok for this check
    std::vector<uint32_t> evaluateChainResult =
      LeveledListUtils::EvaluateTemplateChain(br, base, kPcLevel);
    std::vector<FormDesc> templateChain(evaluateChainResult.size());
    std::transform(evaluateChainResult.begin(), evaluateChainResult.end(),
                   templateChain.begin(), [&](uint32_t formId) {
                     return FormDesc::FromFormId(formId, this->espmFiles);
                   });

    uint32_t race = EvaluateTemplate<espm::NPC_::UseTraits>(
      this, baseId, templateChain,
      [&](const auto& npcLookupResult, const auto& npcData) {
        return npcLookupResult.ToGlobalId(npcData.race);
      });

    bool isBanned = std::find(bannedEspmCharacterRaceIds.begin(),
                              bannedEspmCharacterRaceIds.end(),
                              race) != bannedEspmCharacterRaceIds.end();
    if (isBanned) {
      logger->info("Skipping actor {:#x} because it has banned race {:#x}",
                   record->GetId(), race);
      if (optionalOutTrace) {
        *optionalOutTrace << fmt::format("Skip NPC due to banned race")
                          << std::endl;
      }
      return false;
    }
  }

  auto locationalData = data.loc;

  uint32_t worldOrCell =
    espm::utils::GetMappedId(GetWorldOrCell(br, record), mapping);
  if (!worldOrCell) {
    logger->error("Anomaly: refr without world/cell");
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format("Anomaly: refr without world/cell")
                        << std::endl;
    }
    return false;
  }

  if (isNpc && NpcSourceFilesOverriden()) {
    bool isInterior = false, isExterior = false,
         spawnInInterior = defaultSetting.spawnInInterior,
         spawnInExterior = defaultSetting.spawnInExterior;
    espm::LookupResult res = br.LookupById(worldOrCell);
    auto* cellRecord = espm::Convert<espm::CELL>(res.rec);
    if (cellRecord) {
      isInterior = true;
    }
    auto* worldRecord = espm::Convert<espm::WRLD>(res.rec);
    if (worldRecord) {
      isExterior = true;
    }
    uint32_t npcFileIdx = GetFileIdx(formId);
    if (npcFileIdx >= espmFiles.size()) {
      spdlog::error("NPC's idx is greater than espmFiles.size(). NPC's"
                    "formId "
                    "{:#x}, espmFiles size: {}",
                    formId, espmFiles.size());
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format("NPC's idx is greater than espmFiles.size(). NPC's"
                         "formId "
                         "{:#x}, espmFiles size: {}",
                         formId, espmFiles.size())
          << std::endl;
      }
      return false;
    }
    auto it = npcSettings.find(espmFiles[npcFileIdx]);
    if (it != npcSettings.end()) {
      spawnInInterior = it->second.spawnInInterior;
      spawnInExterior = it->second.spawnInExterior;
      spdlog::trace(
        "found npc setting spawnInInterior: {}, spawnInExterior: {}",
        spawnInInterior, spawnInExterior);
    } else {
      spdlog::trace("npc setting has not been found. Use default "
                    "spawnInInterior: {} and spawnInExterior: {}",
                    spawnInInterior, spawnInExterior);
    }

    if (spawnInInterior && isInterior || spawnInExterior && isExterior) {
    }

    if ((!spawnInInterior || !isInterior) &&
        (!spawnInExterior || !isExterior)) {
      spdlog::trace(
        "Unable to spawn npc because of "
        "rules applied in server settings: spawnInInterior={}, "
        "spawnInExterior={}, NPC location: exterior={}, interior={}",
        spawnInInterior, spawnInExterior, isExterior, isInterior);
      if (optionalOutTrace) {
        *optionalOutTrace
          << fmt::format(
               "Unable to spawn npc because of "
               "rules applied in server settings: spawnInInterior={}, "
               "spawnInExterior={}, NPC location: exterior={}, interior={}",
               spawnInInterior, spawnInExterior, isExterior, isInterior)
          << std::endl;
      }
      return false;
    }
  }

  if (!locationalData) {
    logger->error("Anomaly: refr without locationalData");
    if (optionalOutTrace) {
      *optionalOutTrace << fmt::format("Anomaly: refr without locationalData")
                        << std::endl;
    }
    return false;
  }

  std::optional<NiPoint3> primitiveBoundsDiv2;
  if (data.boundsDiv2) {
    primitiveBoundsDiv2 =
      NiPoint3(data.boundsDiv2[0], data.boundsDiv2[1], data.boundsDiv2[2]);
  }

  auto typeStr = t.ToString();
  std::unique_ptr<MpForm> form;
  LocationalData formLocationalData = {
    LocationalDataUtils::GetPos(locationalData),
    LocationalDataUtils::GetRot(locationalData),
    FormDesc::FromFormId(worldOrCell, espmFiles)
  };

  MpChangeFormREFR* changeForm = nullptr;
  if (!isNpc) {
    form.reset(new MpObjectReference(formLocationalData,
                                     formCallbacksFactory(), baseId,
                                     typeStr.data(), primitiveBoundsDiv2));
  } else {
    form.reset(
      new MpActor(formLocationalData, formCallbacksFactory(), baseId));
  }
  AddForm(std::move(form), formId, true, changeForm);

  // Do not TriggerFormInitEvent here, doing it later after changeForm apply

  if (optionalOutTrace) {
    *optionalOutTrace << fmt::format("AttachEspmRecord returned true")
                      << std::endl;
  }
  return true;
}

bool WorldState::LoadForm(uint32_t formId, std::stringstream* optionalOutTrace)
{
  if (pImpl->npcPlacementExclusions.count(formId)) return false;
  // Chunk discovery can revisit preloaded references and plugin overrides.
  // Reattaching one allocates an index for a duplicate that AddForm discards,
  // leaving an abandoned index and a dangling entry in the lookup cache.
  if (forms.find(formId) != forms.end()) {
    return true;
  }

  ANTIGO_CONTEXT_INIT(ctx);
  ctx.AddUnsigned(formId);

  auto& br = GetEspm().GetBrowser();

  auto lookupRes = br.LookupById(formId);

  if (!lookupRes.rec) {
    return false;
  }

  auto mapping = br.GetCombMapping(lookupRes.fileIdx);

  bool attached =
    AttachEspmRecord(br, lookupRes.rec, *mapping, optionalOutTrace);
  if (optionalOutTrace) {
    *optionalOutTrace << "AttachEspmRecord " << (attached ? "true" : "false")
                      << std::endl;
  }

  if (!attached) {
    pImpl->attachEspmRecordFailures.push_back(formId);
  }

  if (attached) {
    auto& refr = GetFormAt<MpObjectReference>(formId);
    auto it = pImpl->changeFormsForDeferredLoad.find(formId);
    if (it != pImpl->changeFormsForDeferredLoad.end()) {
      auto copy = it->second; // crashes without copying
      refr.ApplyChangeForm(copy);
    }

    refr.ForceSubscriptionsUpdate();
  }

  return attached;
}

void WorldState::RememberInventoryReceipt(MpObjectReference& ref)
{
  auto actor = ref.AsActor();
  if (!actor || actor->GetProfileId() <= 0) {
    throw std::runtime_error("Inventory receipts require a human profile");
  }
  auto key = Impl::ReceiptKey{ref.GetChangeForm().formDesc, actor->GetProfileId()};
  auto existing = pImpl->savedInventoryReceipts.find(key);
  if (existing != pImpl->savedInventoryReceipts.end() &&
      existing->second.form.lock().get() == &ref) {
    return;
  }
  pImpl->savedInventoryReceipts[key] = {
    ref.GetInventoryReceiptDump(), forms.at(ref.GetFormId())
  };
}

void WorldState::ForgetInventoryReceipt(MpObjectReference& ref)
{
  for (auto it = pImpl->savedInventoryReceipts.begin();
       it != pImpl->savedInventoryReceipts.end();) {
    auto form = it->second.form.lock();
    if (!form || form.get() == &ref) {
      it = pImpl->savedInventoryReceipts.erase(it);
    } else {
      ++it;
    }
  }
}


MerchantTransferController& WorldState::GetMerchantTransfers()
{
  if (!pImpl->merchantTransfers) {
    pImpl->merchantTransfers = std::make_shared<MerchantTransferController>(*this);
  }
  pImpl->merchantTransfers->UseParentSavedReceipt(*this);
  return *pImpl->merchantTransfers;
}

std::string WorldState::GetSavedInventoryReceipt(MpObjectReference& ref)
{
  if (!pImpl->saveStorage) {
    throw std::runtime_error("Inventory save acknowledgement is unavailable");
  }
  RememberInventoryReceipt(ref);
  auto actor = ref.AsActor();
  auto key = Impl::ReceiptKey{ref.GetChangeForm().formDesc, actor->GetProfileId()};
  return pImpl->savedInventoryReceipts.at(key).dump;
}

void WorldState::TickSaveStorage(const std::chrono::system_clock::time_point&)
{
  if (!pImpl->saveStorage) {
    return;
  }

  try {
    pImpl->saveStorage->Tick();
  } catch (
    Viet::AsyncSaveStorage<MpChangeForm, FormDesc,
                           std::vector<FormDesc>>::UpsertFailedException& e) {
    spdlog::error(
      "TickSaveStorage - received UpsertFailedException {}, re-saving",
      e.what());

    // No SetTimer here because timers may also break in theory. we faced such
    // problems earlier
    pImpl->saveStorageBusy = false;

    auto& forms = e.GetAffectedForms();
    size_t numRequested = 0;

    for (size_t i = 0; i < forms.size(); ++i) {
      auto& changeForm = forms[i];
      if (changeForm == std::nullopt) {
        continue;
      }

      MpForm* form = LookupFormByIdx(static_cast<int>(i));
      MpObjectReference* refr = form ? form->AsObjectReference() : nullptr;
      if (!refr) {
        continue;
      }

      auto formId = changeForm->formDesc.ToFormId(espmFiles);

      if (refr->GetFormId() != formId) {
        spdlog::error("TickSaveStorage - formIds not matching {:x} <=> {:x}",
                      refr->GetFormId(), formId);
        continue;
      }

      RequestSave(*refr);
      numRequested++;
    }

    spdlog::info("TickSaveStorage - requested re-save for {} forms in buffer "
                 "with size {}",
                 numRequested, forms.size());

  } catch (std::exception& e) {
    spdlog::error(
      "TickSaveStorage - received std::exception {}, can't request re-save",
      e.what());
    pImpl->saveStorageBusy = false;
  }

  if (pImpl->saveStorageBusy) {
    return;
  }

  if (pImpl->changesByIdxEmpty) {
    return;
  }

  pImpl->saveStorageBusy = true;

  auto pImpl_ = pImpl;

  auto previousSize = pImpl->changesByIdx.size();
  std::vector<std::weak_ptr<MpForm>> submittedForms;

  try {
    // Metadata belongs to this submitted snapshot, not the actor's later state.
    std::vector<std::pair<Impl::ReceiptKey, Impl::SavedReceipt>> receipts;
    for (const auto& changeForm : pImpl->changesByIdx) {
      if (!changeForm) {
        continue;
      }
      auto live = forms.find(changeForm->formDesc.ToFormId(espmFiles));
      if (live != forms.end()) submittedForms.push_back(live->second);
      if (changeForm->profileId <= 0) continue;
      auto key = Impl::ReceiptKey{changeForm->formDesc, changeForm->profileId};
      auto cached = pImpl->savedInventoryReceipts.find(key);
      if (cached != pImpl->savedInventoryReceipts.end()) {
        auto dump = changeForm->dynamicFields.GetValueDump(
          MpObjectReference::kInventoryReceiptProperty);
        receipts.push_back({key, {dump, cached->second.form}});
      }
    }
    std::weak_ptr<MerchantTransferController> controller = pImpl->merchantTransfers;
    auto transfers = pImpl->merchantTransfers->CaptureSubmittedBatch(pImpl->changesByIdx);
    pImpl->saveStorage->Upsert(std::move(pImpl->changesByIdx),
      [pImpl_, receipts = std::move(receipts), controller, transfers = std::move(transfers)] {
        pImpl_->saveStorageBusy = false;
        for (const auto& saved : receipts) {
          auto current = pImpl_->savedInventoryReceipts.find(saved.first);
          auto form = saved.second.form.lock();
          auto actor = form ? form->AsActor() : nullptr;
          if (current != pImpl_->savedInventoryReceipts.end() && actor &&
              actor->GetProfileId() == saved.first.second &&
              current->second.form.lock() == form) {
            current->second.dump = saved.second.dump;
          }
        }
        if (auto current = controller.lock()) current->AcknowledgeSubmittedBatch(transfers);
      });
    pImpl->changesByIdxEmpty = true;
  } catch (std::exception& e) {
    pImpl->saveStorageBusy = false;
    spdlog::error("TickSaveStorage - Upsert failed with {}", e.what());
    // Upsert may consume the rvalue buffer before throwing. Reconstruct
    // current snapshots for the submitted identities; retain any buffer
    // contents if failure happened before the move. Do not clear retries.
    for (auto& submitted : submittedForms) {
      auto form = submitted.lock();
      if (!form) continue;
      auto current = forms.find(form->GetFormId());
      auto ref = form->AsObjectReference();
      if (ref && current != forms.end() && current->second == form) RequestSave(*ref);
    }
    return;
  }

  pImpl->changesByIdx.clear();

  bool recycleSuccess =
    pImpl->saveStorage->GetRecycledChangeFormsBuffer(pImpl->changesByIdx);

  if (!recycleSuccess) {
    // Just resize to previous size, at least not to resize every RequestSave
    pImpl->changesByIdx.resize(previousSize);
  }
}

void WorldState::TickTimers(const std::chrono::system_clock::time_point&)
{
  timerEffects.TickTimers();
  timerRegular.TickTimers();

  for (auto& entry : pImpl->relootTimeForTypes) {
    entry.timer.TickTimers();
  }
}

void WorldState::SendPapyrusEvent(MpForm* form, const char* eventName,
                                  const VarValue* arguments,
                                  size_t argumentsCount)
{
  std::vector<VarValue> args = { arguments, arguments + argumentsCount };

  if (spdlog::should_log(spdlog::level::trace)) {
    std::vector<std::string> argsStrings(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
      argsStrings[i] = args[i].ToString();
    }

    if (!strcmp(eventName, "OnTrigger")) {
      static std::once_flag g_once;
      std::call_once(g_once, [&] {
        spdlog::trace("WorldState::SendPapyrusEvent {:x} - {} [{}]",
                      form->GetFormId(), eventName,
                      fmt::join(argsStrings, ", "));
        spdlog::trace("WorldState::SendPapyrusEvent {:x} - Muting {} globally "
                      "to keep logs clear",
                      form->GetFormId(), eventName);
      });
    } else {
      spdlog::trace("WorldState::SendPapyrusEvent {:x} - {} [{}]",
                    form->GetFormId(), eventName,
                    fmt::join(argsStrings, ", "));
    }
  }

  VirtualMachine::OnEnter onEnter = [&](const StackData& stackData) {
    pImpl->policy->BeforeSendPapyrusEvent(
      form, eventName, arguments, argumentsCount,
      stackData.stackIdHolder.GetStackId());
  };
  auto& vm = GetPapyrusVm();
  return vm.SendEvent(form->ToGameObject(), eventName, args, onEnter);
}

const std::set<MpObjectReference*>& WorldState::GetNeighborsByPosition(
  uint32_t cellOrWorld, int16_t cellX, int16_t cellY)
{
  if (espm && !pImpl->chunkLoadingInProgress) {
    Viet::ScopedTask<bool> task([](bool& st) { st = false; },
                                pImpl->chunkLoadingInProgress);
    pImpl->chunkLoadingInProgress = true;

    auto& br = espm->GetBrowser();
    for (int16_t x = cellX - 1; x <= cellX + 1; ++x) {
      for (int16_t y = cellY - 1; y <= cellY + 1; ++y) {
        const bool loaded = grids[cellOrWorld].loadedChunks[x][y];
        if (!loaded) {
          for (size_t i = 0; i < espmFiles.size(); ++i) {
            auto combMapping = br.GetCombMapping(i);
            auto rawMapping = br.GetRawMapping(i);
            uint32_t mappedCellOrWorld =
              espm::utils::GetMappedId(cellOrWorld, *rawMapping);
            auto records = br.GetRecordsAtPos(mappedCellOrWorld, x, y);
            for (auto rec : *records[i]) {
              auto mappedId =
                espm::utils::GetMappedId(rec->GetId(), *combMapping);
              assert(mappedId < 0xff000000);
              LoadForm(mappedId);
            }
          }
          // Do not keep "loaded" reference here since LoadForm would
          // invalidate this reference
          grids[cellOrWorld].loadedChunks[x][y] = true;
        }
      }
    }
  }

  auto& neighbours =
    grids[cellOrWorld].grid->GetNeighboursByPosition(cellX, cellY);
  return neighbours;
}

const std::set<MpObjectReference*>& WorldState::GetLoadedNeighborsByPosition(
  uint32_t cellOrWorld, int16_t cellX, int16_t cellY)
{
  auto state = std::make_pair(&pImpl->chunkLoadingInProgress, pImpl->chunkLoadingInProgress);
  Viet::ScopedTask<decltype(state)> restore(
    [](decltype(state)& value) { *value.first = value.second; }, state);
  pImpl->chunkLoadingInProgress = true;
  return GetNeighborsByPosition(cellOrWorld, cellX, cellY);
}

size_t WorldState::PrepareNpcLoad()
{
  if (!pImpl->placedActorIds) {
    std::vector<uint32_t> ids;
    for (const auto& lookup :
         GetEspm().GetBrowser().GetDistinctRecordsByType("REFR")) {
      if (lookup.rec->GetType() == "ACHR") {
        ids.push_back(lookup.ToGlobalId(lookup.rec->GetId()));
      }
    }
    std::sort(ids.begin(), ids.end());
    pImpl->placedActorIds = std::move(ids);
  }
  return pImpl->placedActorIds->size();
}

void WorldState::SetNpcPlacementExclusions(const std::vector<uint32_t>& ids)
{
  if (ids.size() > 20000) throw std::invalid_argument("Too many NPC exclusions");
  std::unordered_set<uint32_t> next;
  for (const auto id : ids) {
    if (id >= 0xff000000 ||
        FormDesc::FromFormId(id, espmFiles) == FormDesc{0x14, "Skyrim.esm"}) {
      throw std::invalid_argument("Exclusions require existing placed NPC identities");
    }
    const auto lookup = GetEspm().GetBrowser().LookupById(id);
    if (!lookup.rec || lookup.rec->GetType() != "ACHR") {
      throw std::invalid_argument("An NPC exclusion must name an ACHR record");
    }
    const auto loaded = LookupFormByIdNoLoad(id);
    const auto deferred = pImpl->changeFormsForDeferredLoad.find(id);
    if (deferred != pImpl->changeFormsForDeferredLoad.end() &&
        deferred->second.profileId >= 0) {
      throw std::invalid_argument("An NPC exclusion cannot affect a saved human identity");
    }
    if (loaded) {
      const auto actor = loaded->AsActor();
      if (!actor || actor->IsCreatedAsPlayer() || actor->GetProfileId() >= 0 ||
          actor->GetUserId() != Networking::InvalidUserId) {
        throw std::invalid_argument("An NPC exclusion cannot affect a human identity");
      }
    }
    next.insert(id);
  }
  // Validate the whole policy before changing any reference. Saved identities
  // already in the world are hidden and stopped; future loads never create them.
  pImpl->npcPlacementExclusions = std::move(next);
  for (const auto id : pImpl->npcPlacementExclusions) {
    const auto loaded = LookupFormByIdNoLoad(id);
    if (loaded && loaded->AsActor()) {
      auto& actor = *loaded->AsActor();
      actor.SetServerControlled(true);
      actor.StopServerMovement();
      actor.Disable();
    }
  }
}

std::vector<uint32_t> WorldState::GetLoadedNpcIds() const
{
  std::vector<uint32_t> ids;
  for (const auto& [id, form] : forms) {
    const auto actor = form->AsActor();
    if (actor && !actor->IsCreatedAsPlayer() && actor->GetProfileId() < 0 &&
        actor->GetUserId() == Networking::InvalidUserId) ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

WorldState::NpcLoadBatch WorldState::LoadNpcBatch(size_t cursor, size_t limit)
{
  if (limit == 0 || limit > 128) {
    throw std::invalid_argument("NPC batch limit must be from 1 to 128");
  }
  PrepareNpcLoad();

  const auto& ids = *pImpl->placedActorIds;
  if (cursor > ids.size()) {
    throw std::out_of_range("NPC batch cursor exceeds the placed cast");
  }
  const auto end = cursor + std::min(limit, ids.size() - cursor);
  NpcLoadBatch result{ end, ids.size(), {} };

  // ForceSubscriptionsUpdate normally discovers nine surrounding chunks.
  // Keep subscriptions to already loaded references, but defer discovery so
  // one preload request cannot turn into an unbounded world load.
  auto chunkState = std::make_pair(&pImpl->chunkLoadingInProgress,
                                   pImpl->chunkLoadingInProgress);
  Viet::ScopedTask<decltype(chunkState)> restore(
    [](decltype(chunkState)& state) { *state.first = state.second; },
    chunkState);
  pImpl->chunkLoadingInProgress = true;
  for (size_t i = cursor; i < end; ++i) {
    if (pImpl->npcPlacementExclusions.count(ids[i])) continue;
    // The vanilla human reference is an ACHR record too. It is not an NPC
    // and must not be instantiated as a second character by the preloader.
    const auto desc = FormDesc::FromFormId(ids[i], espmFiles);
    if (desc == FormDesc{ 0x14, "Skyrim.esm" }) {
      continue;
    }
    const auto& form = LookupFormById(ids[i]);
    if (form && form->AsActor()) {
      result.actorIds.push_back(ids[i]);
    }
  }
  return result;
}

std::shared_ptr<std::vector<uint32_t>> WorldState::GetAllForms(
  uint32_t modIndex)
{
  if (modIndex >= std::size(pImpl->allFormsByModIndexCache)) {
    spdlog::error("WorldState::GetAllForms - Invalid mod index {}", modIndex);
    return nullptr;
  }

  auto& resCache = pImpl->allFormsByModIndexCache[modIndex];

  if (!resCache) {
    // Deduplicate form IDs
    // TODO: Consider cleaning changeFormsForDeferredLoad after adding a form
    // so we don't need de-duplicate in runtime
    std::unordered_set<uint32_t> formIds;
    for (const auto& p : forms) {
      if ((p.first >> 24) == modIndex) {
        formIds.insert(p.first);
      }
    }
    for (const auto& p : pImpl->changeFormsForDeferredLoad) {
      if ((p.first >> 24) == modIndex) {
        formIds.insert(p.first);
      }
    }

    for (auto failedToAttachFormId : pImpl->attachEspmRecordFailures) {
      formIds.erase(failedToAttachFormId);
    }

    resCache =
      std::make_shared<std::vector<uint32_t>>(formIds.begin(), formIds.end());
  }

  return resCache;
}

MpForm* WorldState::LookupFormByIdx(int idx)
{
  if (formIdxManager) {
    if (idx >= 0 && idx < refrByIdxUnreliable.size()) {
      auto refr = refrByIdxUnreliable[idx];
      if (refr && refr->GetIdx() == idx) {
        return refr;
      }
    }
  }
  return nullptr;
}

espm::Loader& WorldState::GetEspm() const
{
  if (!espm) {
    throw std::runtime_error("No espm attached");
  }
  return *espm;
}

bool WorldState::HasEspm() const
{
  return !!espm;
}

espm::CompressedFieldsCache& WorldState::GetEspmCache()
{
  if (!espmCache) {
    throw std::runtime_error("No espm cache found");
  }
  return *espmCache;
}

IScriptStorage* WorldState::GetScriptStorage() const
{
  return pImpl->scriptStorage.get();
}

struct LazyState
{
  std::shared_ptr<PexScript> pex;
  std::vector<uint8_t> pexBin;

  // With Papyrus hotreload enabled, this variable hold references to
  // previous versions of pex files. This prevents the invalidation of
  // string/identifier types of VarValue
  std::vector<std::shared_ptr<PexScript>> oldPexHolder;
};

PexScript::Lazy CreatePexScriptLazy(
  const CIString& required, std::shared_ptr<IScriptStorage> scriptStorage,
  std::shared_ptr<spdlog::logger> logger, bool enableHotReload)
{
  auto lazyState = std::make_shared<LazyState>();

  PexScript::Lazy lazy;
  lazy.source = required.data();
  lazy.fn = [lazyState, scriptStorage, required, logger, enableHotReload]() {
    if (enableHotReload) {
      auto requiredPex = scriptStorage->GetScriptPex(required.data());
      if (requiredPex != lazyState->pexBin) {
        lazyState->oldPexHolder.push_back(lazyState->pex);
        lazyState->pex.reset();
        lazyState->pexBin = requiredPex;
        logger->info("Papyrus script {} has been reloaded", required);
      }
    }

    if (!lazyState->pex) {
      auto requiredPex = scriptStorage->GetScriptPex(required.data());
      if (requiredPex.empty()) {
        throw std::runtime_error(
          "'" + std::string({ required.begin(), required.end() }) +
          "' is listed but failed to "
          "load from the storage");
      }
      auto pexStructure = Reader({ requiredPex }).GetSourceStructures();
      lazyState->pex = pexStructure[0];
    }
    return lazyState->pex;
  };

  return lazy;
}

VirtualMachine& WorldState::GetPapyrusVm()
{
  if (!pImpl->vm) {
    std::vector<PexScript::Lazy> pexStructures;
    std::vector<std::string> scriptNames;

    auto scriptStorage = pImpl->scriptStorage;
    if (!scriptStorage) {
      logger->error("Required scriptStorage to be non-null");
      pImpl->vm.reset(
        new VirtualMachine(std::vector<std::shared_ptr<PexScript>>()));
      return *pImpl->vm;
    }

    auto& scripts = scriptStorage->ListScripts(false);
    for (auto& required : scripts) {
      auto lazy = CreatePexScriptLazy(required, scriptStorage, this->logger,
                                      this->isPapyrusHotReloadEnabled);
      pexStructures.push_back(lazy);
    }

    if (!pexStructures.empty()) {
      pImpl->vm.reset(new VirtualMachine(pexStructures));

      pImpl->vm->SetMissingScriptHandler(
        [scriptStorage, this](std::string className) {
          std::optional<PexScript::Lazy> result;

          CIString classNameCi = { className.begin(), className.end() };
          if (scriptStorage->ListScripts(true).count(classNameCi)) {
            result =
              CreatePexScriptLazy(classNameCi, scriptStorage, this->logger,
                                  this->isPapyrusHotReloadEnabled);
          }
          return result;
        });

      pImpl->classes =
        PapyrusClassesFactory::CreateAndRegister(*pImpl->vm, pImpl->policy);
    }
  }

  return *pImpl->vm;
}

const std::set<uint32_t>& WorldState::GetActorsByProfileId(
  int32_t profileId) const
{
  static const std::set<uint32_t> kEmptySet;

  auto it = actorIdByProfileId.find(profileId);
  if (it == actorIdByProfileId.end()) {
    return kEmptySet;
  }
  return it->second;
}

const std::set<uint32_t>& WorldState::GetActorsByPrivateIndexedProperty(
  const std::string& privateIndexedPropertyMapKey) const
{
  static const std::set<uint32_t> kEmptySet;

  auto it = actorIdByPrivateIndexedProperty.find(privateIndexedPropertyMapKey);
  if (it == actorIdByPrivateIndexedProperty.end()) {
    return kEmptySet;
  }
  return it->second;
}

std::string WorldState::MakePrivateIndexedPropertyMapKey(
  const std::string& propertyName, const std::string& propertyValueStringified)
{
  return propertyName + '=' + propertyValueStringified;
}

uint32_t WorldState::GenerateFormId()
{
  // An id whose saved record could not be read is still that record's: a new
  // form under it would be saved over the file (Thornswood #1269).
  while (LookupFormById(pImpl->nextId) ||
         pImpl->unreadableChangeForms.count(pImpl->nextId)) {
    ++pImpl->nextId;
  }
  return pImpl->nextId++;
}

void WorldState::KeepUnreadableChangeForm(uint32_t formId, int32_t profileId)
{
  // Only the server's own forms are handed out by GenerateFormId. A placed
  // reference from a plugin keeps its id whatever happens to its save.
  if (formId < 0xff000000) {
    return;
  }
  pImpl->unreadableChangeForms[formId] = profileId;
  spdlog::warn("ChangeForm {:x} (profile {}) is kept as it is on disk, and "
               "its id is not given to any new form",
               formId, profileId);
}

const std::map<uint32_t, int32_t>& WorldState::GetUnreadableChangeForms()
  const
{
  return pImpl->unreadableChangeForms;
}

void WorldState::SetRelootTime(const std::string& recordType,
                               std::chrono::system_clock::duration time)
{
  auto it = std::find_if(pImpl->relootTimeForTypes.begin(),
                         pImpl->relootTimeForTypes.end(),
                         [&recordType](const RelootTimeForTypesEntry& entry) {
                           return entry.recordType == recordType;
                         });

  if (it != pImpl->relootTimeForTypes.end()) {
    it->time = time;
  } else {
    RelootTimeForTypesEntry entry{ recordType, time };
    entry.timer.SetClock(pImpl->timerClock);
    pImpl->relootTimeForTypes.push_back(std::move(entry));
  }
}

void WorldState::SetTimerClock(Viet::Timer::Clock clock)
{
  pImpl->timerClock = clock;
  timerEffects.SetClock(clock);
  timerRegular.SetClock(clock);
  for (auto& entry : pImpl->relootTimeForTypes) {
    entry.timer.SetClock(clock);
  }
}

std::optional<std::chrono::system_clock::duration> WorldState::GetRelootTime(
  const std::string& recordType) const
{
  auto it = std::find_if(pImpl->relootTimeForTypes.begin(),
                         pImpl->relootTimeForTypes.end(),
                         [&recordType](const RelootTimeForTypesEntry& entry) {
                           return entry.recordType == recordType;
                         });

  if (it == pImpl->relootTimeForTypes.end()) {
    return std::nullopt;
  }
  return it->time;
}

bool WorldState::HasKeyword(uint32_t baseId, const char* keyword)
{
  auto lookupRes = GetEspm().GetBrowser().LookupById(baseId);

  if (!lookupRes.rec) {
    return false;
  }

  const auto keywordIds = lookupRes.rec->GetKeywordIds(GetEspmCache());

  for (auto keywordId : keywordIds) {
    auto keywordIdGlobal = lookupRes.ToGlobalId(keywordId);
    auto rec = GetEspm().GetBrowser().LookupById(keywordIdGlobal).rec;
    if (rec && !Utils::stricmp(rec->GetEditorId(GetEspmCache()), keyword)) {
      return true;
    }
  }

  return false;
}

bool WorldState::NpcSourceFilesOverriden() const noexcept
{
  return !npcSettings.empty() || defaultSetting.overriden;
}

bool WorldState::IsNpcAllowed(uint32_t refrId) const noexcept
{
  if (npcSettings.empty() && defaultSetting.overriden) {
    return true;
  }
  uint32_t npcFileIdx = GetFileIdx(refrId);
  for (const auto& [fileName, _] : npcSettings) {
    auto it = std::find(espmFiles.begin(), espmFiles.end(), fileName);
    if (it == espmFiles.end()) {
      return false;
    }
    auto idx = std::distance(espmFiles.begin(), it);
    if (npcFileIdx == idx) {
      return true;
    }
  }
  return false;
}

uint32_t WorldState::GetFileIdx(uint32_t formId) const noexcept
{
  return formId >> 24;
}

void WorldState::SetNpcSettings(
  std::unordered_map<std::string, NpcSettingsEntry>&& settings)
{
  npcSettings = settings;
}

bool WorldState::RemoveTimer(uint32_t timerId)
{
  return timerRegular.RemoveTimer(timerId);
}

bool WorldState::RemoveEffectTimer(uint32_t timerId)
{
  return timerEffects.RemoveTimer(timerId);
}

void WorldState::SetForbiddenRelootTypes(const std::set<std::string>& types)
{
  pImpl->forbiddenRelootTypes = types;
}

void WorldState::SetEnableConsoleCommandsForAllSetting(bool enable)
{
  enableConsoleCommandsForAll = enable;
}

bool WorldState::IsRelootForbidden(std::string type) const noexcept
{
  return pImpl->forbiddenRelootTypes.find(type) !=
    pImpl->forbiddenRelootTypes.end();
}

bool WorldState::HasEspmFile(std::string_view filename) const noexcept
{
  return std::find(espmFiles.begin(), espmFiles.end(), filename) !=
    espmFiles.end();
}
