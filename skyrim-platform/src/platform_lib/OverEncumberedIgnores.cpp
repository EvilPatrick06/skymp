#include "OverEncumberedIgnores.h"

// THORNSWOOD PATCH (Thornswood #1385). See OverEncumberedIgnores.h.

OverEncumberedIgnores& OverEncumberedIgnores::GetSingleton()
{
  static OverEncumberedIgnores instance;
  return instance;
}

void OverEncumberedIgnores::Set(uint32_t formId, bool ignored)
{
  std::lock_guard l(m);
  if (ignored) {
    formIds.insert(formId);
  } else {
    formIds.erase(formId);
  }
  count = formIds.size();
}

bool OverEncumberedIgnores::Has(uint32_t formId) const
{
  if (count == 0) {
    return false;
  }
  std::lock_guard l(m);
  return formIds.count(formId) > 0;
}

size_t OverEncumberedIgnores::Size() const
{
  std::lock_guard l(m);
  return formIds.size();
}

void OverEncumberedIgnores::Clear()
{
  std::lock_guard l(m);
  formIds.clear();
  count = 0;
}
