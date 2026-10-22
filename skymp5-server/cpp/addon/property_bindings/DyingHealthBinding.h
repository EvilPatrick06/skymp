#pragma once
#include "PropertyBinding.h"

class DyingEvent;

// Thornswood #1626. Readable and writable only inside an onDying handler for
// the same actor: the health share (above 0, at most 1) a refused death keeps
// the actor alive at. It reads the health before the blow until a handler
// writes another. Anywhere else it reads 0 and a write is refused.
class DyingHealthBinding : public PropertyBinding
{
public:
  std::string GetPropertyName() const override { return "dyingHealth"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer,
                  uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId,
           Napi::Value newValue) override;

  // The innermost onDying being asked about this actor, or nullptr.
  static DyingEvent* FindDyingEvent(ScampServer& scampServer, uint32_t formId);
};
