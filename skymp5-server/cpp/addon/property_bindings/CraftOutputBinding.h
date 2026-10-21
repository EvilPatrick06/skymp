#pragma once
#include "PropertyBinding.h"

// Thornswood #2906. Readable and writable only inside an onCraft handler for
// the same actor: what the craft gives, { baseId, count }, which is the
// recipe's output until a handler names another. A write takes the same
// shape, and either half may be left out to keep what it is. Anywhere else it
// reads undefined and a write is refused.
class CraftOutputBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "craftOutput"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
