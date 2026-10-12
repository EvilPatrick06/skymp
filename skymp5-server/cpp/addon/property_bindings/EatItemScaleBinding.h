#pragma once
#include "PropertyBinding.h"

// Thornswood #1382. Readable and writable only inside an onEatItem handler
// for the same actor: the scale (0 to 1) the item being eaten or drunk is
// applied at. Anywhere else it reads 1 and a write is refused.
class EatItemScaleBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "eatItemScale"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;
};
