#include "CraftStockBinding.h"
#include "MpActor.h"
#include "NapiHelper.h"
#include <fmt/format.h>
#include <stdexcept>
#include <string>

namespace {
// More than any building holds. A list longer than this is a mistake in the
// gamemode, not a house.
constexpr uint32_t kMaxContainers = 256;

// The reference with this id, refused unless its base is of this type.
MpObjectReference& RefOfType(ScampServer& scampServer, uint32_t refId,
                             const char* type, const char* what)
{
  auto& worldState = scampServer.GetPartOne()->worldState;
  auto& form = worldState.LookupFormById(refId);
  auto refr = form ? form->AsObjectReference() : nullptr;
  if (!refr || refr->AsActor()) {
    throw std::runtime_error(
      fmt::format("craftStock.{} {:#x} is not a reference", what, refId));
  }
  if (refr->GetBaseType() != type) {
    throw std::runtime_error(
      fmt::format("craftStock.{} {:#x} is a {}, not a {}", what, refId,
                  refr->GetBaseType(), type));
  }
  return *refr;
}
}

Napi::Value CraftStockBinding::Get(Napi::Env env, ScampServer& scampServer,
                                   uint32_t formId)
{
  auto& actor =
    scampServer.GetPartOne()->worldState.GetFormAt<MpActor>(formId);
  auto& stock = actor.GetBenchStock();
  if (!stock) {
    return env.Null();
  }
  auto res = Napi::Object::New(env);
  res.Set("bench", Napi::Number::New(env, stock->workbenchId));
  auto from = Napi::Array::New(env, stock->containerIds.size());
  for (uint32_t i = 0; i < stock->containerIds.size(); ++i) {
    from.Set(i, Napi::Number::New(env, stock->containerIds[i]));
  }
  res.Set("from", from);
  res.Set("open", Napi::Boolean::New(env, stock->open));
  return res;
}

void CraftStockBinding::Set(Napi::Env env, ScampServer& scampServer,
                            uint32_t formId, Napi::Value newValue)
{
  auto& actor =
    scampServer.GetPartOne()->worldState.GetFormAt<MpActor>(formId);

  if (newValue.IsNull() || newValue.IsUndefined()) {
    actor.SetBenchStock(std::nullopt);
    return;
  }

  auto value = NapiHelper::ExtractObject(newValue, "craftStock");
  MpActor::BenchStock stock;
  stock.workbenchId =
    NapiHelper::ExtractUInt32(value.Get("bench"), "craftStock.bench");
  RefOfType(scampServer, stock.workbenchId, "FURN", "bench");

  auto from = NapiHelper::ExtractArray(value.Get("from"), "craftStock.from");
  if (from.Length() > kMaxContainers) {
    throw std::runtime_error(
      fmt::format("craftStock.from names {} containers, more than {}",
                  from.Length(), kMaxContainers));
  }
  for (uint32_t i = 0; i < from.Length(); ++i) {
    auto containerId =
      NapiHelper::ExtractUInt32(from.Get(i), "craftStock.from[i]");
    RefOfType(scampServer, containerId, "CONT", "from[i]");
    stock.containerIds.push_back(containerId);
  }

  actor.SetBenchStock(std::move(stock));
}
