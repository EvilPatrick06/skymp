#pragma once
#include "PropertyBinding.h"

// Thornswood #2906. Readable and writable only inside an onCraft handler for
// the same actor: what the craft gives, { baseId, count }, which is the
// recipe's output until a handler names another. A write takes the same
// shape, and either half may be left out to keep what it is. Anywhere else it
// reads undefined and a write is refused.
//
// Thornswood #3026: a third key, name, is the name the entry is added with (a
// master weaver's garment). It reads only when named; a write of a string
// names it, null names none, and left out it stays what it is.
class CraftOutputBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "craftOutput"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
