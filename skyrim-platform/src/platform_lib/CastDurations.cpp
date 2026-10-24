#include "CastDurations.h"

#include <cmath>

// THORNSWOOD PATCH (Thornswood #2848). See CastDurations.h.

namespace {
float Checked(float mult)
{
  return (std::isfinite(mult) && mult > 0.f) ? mult : 1.f;
}
}

CastDurations& CastDurations::GetSingleton()
{
  static CastDurations instance;
  return instance;
}

void CastDurations::SetEvery(uint32_t casterFormId, uint32_t every,
                             float mult)
{
  std::lock_guard l(m);
  if (every == 0) {
    rules.erase(casterFormId);
    for (auto it = latest.begin(); it != latest.end();) {
      it = it->first.first == casterFormId ? latest.erase(it) : std::next(it);
    }
    for (auto it = startedSinceCast.begin(); it != startedSinceCast.end();) {
      it = it->first.first == casterFormId ? startedSinceCast.erase(it)
                                           : std::next(it);
    }
    return;
  }
  auto& rule = rules[casterFormId];
  rule.every = every;
  rule.mult = Checked(mult);
}

bool CastDurations::HasRule(uint32_t casterFormId) const
{
  std::lock_guard l(m);
  return rules.count(casterFormId) > 0;
}

float CastDurations::OnOwnCast(uint32_t casterFormId, uint32_t spellFormId,
                               bool holds, std::vector<Started>* started)
{
  std::lock_guard l(m);
  if (started) {
    started->clear();
  }
  auto ruleIt = rules.find(casterFormId);
  if (ruleIt == rules.end()) {
    return 1.f;
  }
  const Key key{ casterFormId, spellFormId };

  auto startedIt = startedSinceCast.find(key);
  if (startedIt != startedSinceCast.end()) {
    if (started) {
      *started = std::move(startedIt->second);
    }
    startedSinceCast.erase(startedIt);
  }

  float mult = 1.f;
  auto& rule = ruleIt->second;
  if (holds) {
    ++rule.counted;
    if (rule.counted >= rule.every) {
      rule.counted = 0;
      mult = rule.mult;
    }
  }
  latest[key] = mult;
  return mult;
}

void CastDurations::OnPlayedCast(uint32_t casterFormId, uint32_t spellFormId,
                                 float mult)
{
  std::lock_guard l(m);
  latest[Key{ casterFormId, spellFormId }] = Checked(mult);
}

float CastDurations::OnEffectStart(uint32_t casterFormId,
                                   uint32_t spellFormId, uint32_t targetFormId,
                                   uint16_t uniqueId, float gameDuration)
{
  std::lock_guard l(m);
  const Key key{ casterFormId, spellFormId };
  if (rules.count(casterFormId)) {
    startedSinceCast[key].push_back(
      Started{ targetFormId, uniqueId, gameDuration });
  }
  auto it = latest.find(key);
  return it == latest.end() ? 1.f : it->second;
}

void CastDurations::Clear()
{
  std::lock_guard l(m);
  rules.clear();
  latest.clear();
  startedSinceCast.clear();
}
