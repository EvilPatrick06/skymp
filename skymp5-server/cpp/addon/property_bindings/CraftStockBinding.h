#pragma once
#include "PropertyBinding.h"

// Thornswood #3159. The stock a crafting bench draws materials from for an
// actor (MpActor::BenchStock): { bench, from }, the bench's reference and the
// containers' references, in the order they are drawn from. The gamemode
// writes it when the bench is pressed, because whose chests count is its
// rule; the server opens it when the actor takes that bench and closes it
// when the actor leaves. A write of null, or of nothing, names none. A read
// adds open, whether the actor is at that bench now, and is null when none is
// named.
class CraftStockBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "craftStock"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
