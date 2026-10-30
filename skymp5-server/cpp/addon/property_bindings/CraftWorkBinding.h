#pragma once
#include "PropertyBinding.h"

// Thornswood #3161. Readable and writable only inside an onCraftWork handler
// for the same actor: how long the craft takes at the bench, { ms }, which is
// 0 (made at once) until a handler names a time. A write takes the same shape;
// ms is a whole number from 0 to CraftWorkEvent::kMaxWorkTime. Anywhere else
// it reads undefined and a write is refused. See CraftWorkEvent.h.
class CraftWorkBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "craftWork"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
