#include <cmath>
#include "LoadGameApi.h"
#include "LoadGame.h"
#include "NullPointerException.h"
#include "savefile/SFChangeFormNPC.h"
#include <map>

namespace {
uint32_t RgbToAbgr(int32_t rgb)
{
  uint32_t& colorUint = reinterpret_cast<uint32_t&>(rgb);

  colorUint *= 256; // RGB => RGBA

  uint8_t rgba[4];
  for (int i = 0; i < std::size(rgba); ++i) {
    rgba[i] = colorUint / (int)pow(256, std::size(rgba) - i - 1);
    colorUint %= (int)pow(256, std::size(rgba) - i - 1);
  }

  uint32_t resultColor = 0;
  for (int i = 0; i < std::size(rgba); ++i) {
    resultColor +=
      rgba[std::size(rgba) - i - 1] * (int)pow(256, std::size(rgba) - i - 1);
  }
  return resultColor;
}

// formId is this game's id; the save names its plugin by its own lists
// (LoadGame::ListPlugins).
SaveFile_::RefID FormIdToRefId(SaveFile_::SaveFile& save,
                               const SaveFile_::PluginRemap& plugins,
                               uint32_t formId)
{
  return SaveFile_::RefID::CreateRefId(save, plugins.ToSaveFormId(formId));
}

std::unique_ptr<SaveFile_::ChangeFormNPC_> CreateChangeFormNpc(
  std::shared_ptr<SaveFile_::SaveFile> save,
  const SaveFile_::PluginRemap& plugins, Napi::Object npcData)
{
  auto changeFormNpc = std::make_unique<SaveFile_::ChangeFormNPC_>();

  if (auto name = npcData.Get("name"); !name.IsUndefined() && !name.IsNull()) {
    changeFormNpc->playerName =
      NapiHelper::ExtractString(name, "npcData.name");
  }

  if (auto raceId = npcData.Get("raceId");
      !raceId.IsUndefined() && !raceId.IsNull()) {
    auto raceIdExtracted = NapiHelper::ExtractUInt32(raceId, "npcData.raceId");
    changeFormNpc->race = SaveFile_::ChangeFormNPC_::RaceChange();
    changeFormNpc->race->defaultRace = FormIdToRefId(*save, plugins, raceIdExtracted);
    changeFormNpc->race->myRaceNow = FormIdToRefId(*save, plugins, raceIdExtracted);
  }

  // TODO: why mismatch with skyrimPlatform.ts: instead of 'npcData' this is in
  // 'npcData.face'???
  if (auto isFemale = npcData.Get("isFemale");
      !isFemale.IsUndefined() && !isFemale.IsNull()) {
    // TODO: this is a hotfix, fix properly.
    // To test ensure players after relog preserve their gender anims
    if (static_cast<std::string>(isFemale.ToString()) == "true") {
      changeFormNpc->gender = isFemale ? 1 : 0;
    }
  }

  if (auto face = npcData.Get("face"); !face.IsUndefined() && !face.IsNull()) {
    changeFormNpc->face = SaveFile_::ChangeFormNPC_::Face();

    auto faceExtracted = NapiHelper::ExtractObject(face, "npcData.face");

    if (auto bodySkinColor = faceExtracted.Get("bodySkinColor");
        !bodySkinColor.IsUndefined() && !bodySkinColor.IsNull()) {
      auto bodySkinColorExtracted =
        NapiHelper::ExtractInt32(bodySkinColor, "npcData.bodySkinColor");
      changeFormNpc->face->bodySkinColor = RgbToAbgr(bodySkinColorExtracted);
    }

    if (auto headPartIds = faceExtracted.Get("headPartIds");
        !headPartIds.IsUndefined() && !headPartIds.IsNull()) {
      auto headPartIdsExtracted =
        NapiHelper::ExtractArray(headPartIds, "npcData.headPartIds");
      int n = headPartIdsExtracted.Length();

      for (int i = 0; i < n; ++i) {
        auto jHpId = headPartIdsExtracted.Get(i);
        std::string comment = fmt::format("npcData.headPartIds[{}]", i);
        auto hpId = NapiHelper::ExtractUInt32(jHpId, comment.data());
        changeFormNpc->face->headParts.push_back(FormIdToRefId(*save, plugins, hpId));
      }
    }

    if (auto presets = faceExtracted.Get("presets");
        !presets.IsUndefined() && !presets.IsNull()) {
      auto presetsExtracted =
        NapiHelper::ExtractArray(presets, "npcData.presets");
      int n = presetsExtracted.Length();
      for (int i = 0; i < n; ++i) {
        auto jValue = presetsExtracted.Get(i);
        std::string comment = fmt::format("npcData.presets[{}]", i);
        auto value = NapiHelper::ExtractUInt32(jValue, comment.data());
        changeFormNpc->face->presets.push_back(value);
      }
    }

    if (auto headTextureSetId = faceExtracted.Get("headTextureSetId");
        !headTextureSetId.IsUndefined() && !headTextureSetId.IsNull()) {
      auto id = NapiHelper::ExtractUInt32(headTextureSetId,
                                          "npcData.headTextureSetId");
      changeFormNpc->face->headTextureSet = FormIdToRefId(*save, plugins, id);
    }
  }

  return changeFormNpc;
}

std::unique_ptr<LoadGame::Time> CreateTime(
  std::shared_ptr<SaveFile_::SaveFile>, Napi::Object time_)
{
  auto hours = NapiHelper::ExtractInt32(time_.Get("hours"), "time.hours");
  auto minutes =
    NapiHelper::ExtractInt32(time_.Get("minutes"), "time.minutes");
  auto seconds =
    NapiHelper::ExtractInt32(time_.Get("seconds"), "time.seconds");

  auto time = std::make_unique<LoadGame::Time>();
  time->Set(seconds, minutes, hours);
  return time;
}

std::unique_ptr<std::vector<std::string>> CreateLoadOrder(
  std::shared_ptr<SaveFile_::SaveFile>, Napi::Array loadOrder_)
{
  std::unique_ptr<std::vector<std::string>> loadOrder;
  loadOrder.reset(new std::vector<std::string>);
  int n = loadOrder_.Length();
  for (int i = 0; i < n; ++i) {
    auto jValue = loadOrder_.Get(i);
    std::string comment = fmt::format("loadOrder[{}]", i);
    auto value = NapiHelper::ExtractString(jValue, comment.data());
    loadOrder->push_back(value);
  }
  return loadOrder;
}

double InitialInventoryInteger(Napi::Value value, double minimum, double maximum)
{
  if (!value.IsNumber()) throw std::runtime_error("Initial inventory value must be numeric");
  const auto number = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(number) || std::trunc(number) != number || number < minimum || number > maximum)
    throw std::runtime_error("Initial inventory value must be an integer in range");
  return number;
}

// Initial creation accepts its complete basic starter inventory. Complex saved
// item metadata remains on the established returning-character path.
std::unique_ptr<std::vector<InitialInventory::Item>> CreateInitialInventory(
  std::shared_ptr<SaveFile_::SaveFile> save,
  const SaveFile_::PluginRemap& plugins, Napi::Object data)
{
  struct Totals { int64_t delta = 0; bool worn = false; bool left = false; };
  std::map<uint32_t, Totals> totals;
  auto base = RE::TESForm::LookupByID(0x7);
  auto container = base ? base->As<RE::TESContainer>() : nullptr;
  if (!container) throw std::runtime_error("Player base container unavailable before initial load");
  for (uint32_t i = 0; i < container->numContainerObjects; ++i) {
    auto entry = container->containerObjects[i];
    if (entry && entry->obj) {
      if (entry->obj->formID >= 0xff000000 || !entry->obj->IsInventoryObject())
        throw std::runtime_error("Unsupported player base inventory form");
      totals[entry->obj->formID].delta -= entry->count;
    }
  }
  auto entries = NapiHelper::ExtractArray(data.Get("entries"), "initialInventory.entries");
  if (entries.Length() > 4096) throw std::runtime_error("Excessive initial inventory");
  for (uint32_t i = 0; i < entries.Length(); ++i) {
    auto entry = NapiHelper::ExtractObject(entries.Get(i), "initialInventory.entry");
    auto keys = entry.GetPropertyNames();
    for (uint32_t j = 0; j < keys.Length(); ++j) {
      const auto key = keys.Get(j).ToString().Utf8Value();
      if (key != "baseId" && key != "count" && key != "worn" && key != "wornLeft")
        throw std::runtime_error("Unsupported initial inventory metadata: " + key);
    }
    auto id = static_cast<uint32_t>(InitialInventoryInteger(entry.Get("baseId"), 1, UINT32_MAX));
    auto count = static_cast<int32_t>(InitialInventoryInteger(entry.Get("count"), 1, INT32_MAX));
    const auto item = RE::TESForm::LookupByID(id);
    if (id >= 0xff000000 || !item || !item->IsInventoryObject() || count <= 0)
      throw std::runtime_error("Invalid initial inventory item");
    auto worn = entry.Get("worn"); auto left = entry.Get("wornLeft");
    if ((!worn.IsUndefined() && !worn.IsBoolean()) || (!left.IsUndefined() && !left.IsBoolean()))
      throw std::runtime_error("Invalid initial worn flags");
    auto& total = totals[id]; total.delta += count;
    const bool wear = worn.IsBoolean() && worn.ToBoolean().Value();
    const bool wearLeft = left.IsBoolean() && left.ToBoolean().Value();
    if (wear && wearLeft) throw std::runtime_error("One initial stack cannot occupy both hands");
    if ((wear || wearLeft) && count != 1) throw std::runtime_error("Initial worn stack must contain one item");
    if ((wear && total.worn) || (wearLeft && total.left)) throw std::runtime_error("Duplicate initial worn stack");
    total.worn |= wear; total.left |= wearLeft;
  }
  auto result = std::make_unique<std::vector<InitialInventory::Item>>();
  for (auto& [id, total] : totals) {
    if (total.delta < INT32_MIN || total.delta > INT32_MAX) throw std::runtime_error("Initial inventory count overflow");
    if (!total.delta && !total.worn && !total.left) continue;
    const auto ref = FormIdToRefId(*save, plugins, id);
    result->push_back({{ref.byte0, ref.byte1, ref.byte2},
                       static_cast<int32_t>(total.delta), total.worn, total.left});
  }
  return result;
}
}

Napi::Value LoadGameApi::LoadGame(const Napi::CallbackInfo& info)
{
  NiPoint3 niPos = NapiHelper::ExtractNiPoint3(info[0], "pos");
  NiPoint3 niAngle = NapiHelper::ExtractNiPoint3(info[1], "angle");
  std::array<float, 3> pos = { niPos[0], niPos[1], niPos[2] };
  std::array<float, 3> angle = { niAngle[0], niAngle[1], niAngle[2] };

  uint32_t cellOrWorld = NapiHelper::ExtractUInt32(info[2], "cellOrWorld");

  constexpr auto kPathInAssetsMale = "assets/template.ess";

  const char* pathInAsset = kPathInAssetsMale;

  auto save = LoadGame::PrepareSaveFile(pathInAsset);
  if (!save) {
    throw NullPointerException("save");
  }

  std::unique_ptr<std::vector<std::string>> saveLoadOrder =
    (info[4].IsUndefined() || info[4].IsNull())
    ? nullptr
    : CreateLoadOrder(save, NapiHelper::ExtractArray(info[4], "loadOrder"));

  std::unique_ptr<std::vector<std::string>> saveLightLoadOrder =
    (info[7].IsUndefined() || info[7].IsNull())
    ? nullptr
    : CreateLoadOrder(save,
                      NapiHelper::ExtractArray(info[7], "lightLoadOrder"));

  // Thornswood #1715. The plugins go into the save before any form does:
  // every id below is this game's, and the save names it through its own
  // plugin lists.
  const auto plugins = LoadGame::ListPlugins(*save, saveLoadOrder.get(),
                                             saveLightLoadOrder.get());

  std::unique_ptr<SaveFile_::ChangeFormNPC_> changeFormNpc =
    (info[3].IsUndefined() || info[3].IsNull())
    ? nullptr
    : CreateChangeFormNpc(save, plugins,
                          NapiHelper::ExtractObject(info[3], "npcData"));

  std::unique_ptr<LoadGame::Time> saveFileTime =
    (info[5].IsUndefined() || info[5].IsNull())
    ? nullptr
    : CreateTime(save, NapiHelper::ExtractObject(info[5], "time"));

  auto inventory = (info[6].IsUndefined() || info[6].IsNull())
    ? nullptr
    : CreateInitialInventory(
        save, plugins, NapiHelper::ExtractObject(info[6], "initialInventory"));

  LoadGame::Run(save, pos, angle, cellOrWorld, saveFileTime.get(), nullptr,
                changeFormNpc.get(), plugins, inventory.get());

  return info.Env().Undefined();
}

Napi::Value LoadGameApi::GetExteriorCellCoordinates(const Napi::CallbackInfo& info)
{
  const auto id = NapiHelper::ExtractUInt32(info[0], "cellId");
  const auto cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(id);
  if (!cell || !cell->IsExteriorCell()) return info.Env().Undefined();
  const auto coordinates = cell->GetCoordinates();
  if (!coordinates) return info.Env().Undefined();
  auto result = Napi::Array::New(info.Env(), 2);
  result.Set(uint32_t(0), coordinates->cellX);
  result.Set(uint32_t(1), coordinates->cellY);
  return result;
}
