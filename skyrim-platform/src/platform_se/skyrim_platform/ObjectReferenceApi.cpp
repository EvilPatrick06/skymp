#include "ObjectReferenceApi.h"

#include "NullPointerException.h"

namespace {
RE::TESObjectREFR* GetArgObjectReference(const Napi::Value& arg)
{
  auto formId = NapiHelper::ExtractUInt32(arg, "refrFormId");
  auto refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);

  if (!refr) {
    throw NullPointerException("refr");
  }

  return refr;
}
}

Napi::Value ObjectReferenceApi::SetCollision(const Napi::CallbackInfo& info)
{
  auto refr = GetArgObjectReference(info[0]);
  refr->SetCollision(NapiHelper::ExtractBoolean(info[1], "collision"));
  return info.Env().Undefined();
}

// THORNSWOOD PATCH (Thornswood #3080). The bench type in a furniture
// record's WBDT: 0 for furniture that is no bench (a chair, a bed, a chopping
// block, an ore vein), otherwise the crafting menu it opens (1 create object,
// 2 sharpening wheel, 3 enchanter, 5 alchemy, 7 armour table, ...). 0 for a
// base that is not furniture. Papyrus has no way to read it, and the client
// needs it to send a bench through the server before Skyrim opens its menu.
Napi::Value ObjectReferenceApi::GetWorkbenchType(
  const Napi::CallbackInfo& info)
{
  auto formId = NapiHelper::ExtractUInt32(info[0], "baseFormId");
  auto furniture = RE::TESForm::LookupByID<RE::TESFurniture>(formId);
  int benchType =
    furniture ? static_cast<int>(furniture->workBenchData.benchType.get()) : 0;
  return Napi::Number::New(info.Env(), benchType);
}
