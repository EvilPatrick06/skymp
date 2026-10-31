#include "Inventory.h"
#include "archives/JsonInputArchive.h"
#include "archives/JsonOutputArchive.h"
#include "archives/SimdJsonInputArchive.h"
#include <algorithm>
#include <fmt/format.h>
#include <limits>
#include <spdlog/spdlog.h>
#include <cmath>
#include <tuple>

Inventory::Entry::Entry()
{
}

Inventory::Entry::Entry(uint32_t baseId_, uint32_t count_,
                        const ExtraData& extraData_)
  : baseId(baseId_)
  , count(count_)
  , ExtraData(extraData_)
{
}

Inventory::Entry Inventory::Entry::FromJson(const simdjson::dom::element& e)
{
  std::string minifiedDump = simdjson::minify(e);
  nlohmann::json j = nlohmann::json::parse(minifiedDump);

  Entry res;
  JsonInputArchive ar(j);
  res.Serialize(ar);
  return res;
}

Inventory::Worn Inventory::Entry::GetWorn() const
{
  bool wornValue = worn_.value_or(false);
  bool wornLeftValue = wornLeft.value_or(false);

  if (wornLeftValue) {
    return Worn::Left;
  }
  if (wornValue) {
    return Worn::Right;
  }
  return Worn::None;
}

void Inventory::Entry::SetWorn(Inventory::Worn worn)
{
  if (worn == GetWorn()) {
    return;
  }

  switch (worn) {
    case Worn::None:
      worn_ = false;
      wornLeft = false;
      break;
    case Worn::Right:
      worn_ = true;
      wornLeft = false;
      break;
    case Worn::Left:
      worn_ = false;
      wornLeft = true;
      break;
    default:
      spdlog::warn("Inventory::SetWorn: unknown worn value {}",
                   static_cast<int>(worn));
      worn_ = false;
      wornLeft = false;
      break;
  }
}

bool Inventory::Entry::EqualExceptCount(const Inventory::Entry& other) const
{
  // GetWorn() instead of direct comparison because of possible false vs
  // nullopt mismatch. Logically it should be the same
  return std::make_tuple(baseId, health, enchantmentId, maxCharge,
                         removeEnchantmentOnUnequip, chargePercent, name, soul,
                         poisonId, poisonCount, GetWorn(), made) ==
    std::make_tuple(other.baseId, other.health, other.enchantmentId,
                    other.maxCharge, other.removeEnchantmentOnUnequip,
                    other.chargePercent, other.name, other.soul,
                    other.poisonId, other.poisonCount, other.GetWorn(),
                    other.made);
}

void Inventory::Round(Made& made)
{
  for (auto& effect : made.effects) {
    if (!std::isfinite(effect.magnitude)) {
      effect.magnitude = 0.f;
    }
    effect.magnitude = std::round(effect.magnitude * 100.f) / 100.f;
  }
}

std::string Inventory::Canonical(const Made& made)
{
  // Two decimals, as Round leaves them; fmt prints a float's shortest
  // round-trip text, so "12.3" stays "12.3" on every machine.
  std::string text = made.poison ? "poison|" : "potion|";
  text += made.name;
  for (const auto& effect : made.effects) {
    text += fmt::format("|{:x}:{:.2f}:{}:{}", effect.effectId,
                        effect.magnitude, effect.area, effect.duration);
  }
  return text;
}

uint32_t Inventory::MadeBaseId(const Made& made)
{
  uint32_t hash = 2166136261u;
  for (unsigned char c : Canonical(made)) {
    hash ^= c;
    hash *= 16777619u;
  }
  return kMadeIdFirst | (hash & (~kMadeIdFirst));
}

Inventory& Inventory::AddItem(uint32_t baseId, uint32_t count)
{
  return AddItems({ { baseId, count } });
}

namespace {
// Adds toAdd to entries, each entry joining the stack it matches. Returns
// the entry that would take its stack past what Entry::count holds, with
// entries left part done, or nullptr when everything was added.
const Inventory::Entry* AddEntries(std::vector<Inventory::Entry>& entries,
                                   const std::vector<Inventory::Entry>& toAdd)
{
  for (auto& entryToAdd : toAdd) {
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const Inventory::Entry& entry) {
                             return entry.EqualExceptCount(entryToAdd);
                           });
    if (it == entries.end()) {
      entries.push_back(entryToAdd);
      continue;
    }
    if (entryToAdd.count > std::numeric_limits<uint32_t>::max() - it->count) {
      return &entryToAdd;
    }
    it->count += entryToAdd.count;
  }
  return nullptr;
}
}

Inventory& Inventory::AddItems(const std::vector<Entry>& toAdd)
{
  auto copy = entries;
  if (auto refused = AddEntries(copy, toAdd)) {
    throw std::runtime_error(fmt::format(
      "Inventory can't take {} more of {:#x}: one stack holds at most {}",
      refused->count, refused->baseId, std::numeric_limits<uint32_t>::max()));
  }
  entries = std::move(copy);
  return *this;
}

bool Inventory::CanAddItems(const std::vector<Entry>& toAdd) const
{
  auto copy = entries;
  return AddEntries(copy, toAdd) == nullptr;
}

Inventory& Inventory::RemoveItems(const std::vector<Entry>& entries)
{
  auto copy = *this;

  for (auto& e : entries) {
    if (!e.count)
      continue;

    uint32_t remaining = e.count;
    uint32_t totalRemoved = 0;
    for (auto& entry : copy.entries) {
      if (entry.EqualExceptCount(e)) {
        if (entry.count > remaining) {
          entry.count -= remaining;
          totalRemoved += remaining;
          remaining = 0;
          break;
        } else {
          totalRemoved += entry.count;
          remaining -= entry.count;
          entry.count = 0;
        }
      }
    }

    if (totalRemoved != e.count) {
      throw std::runtime_error(
        fmt::format("Source inventory doesn't have enough {:#x} ({} is "
                    "required while {} present)",
                    e.baseId, e.count, totalRemoved));
    }

    // remove empty entries
    copy.entries.erase(
      std::remove_if(copy.entries.begin(), copy.entries.end(),
                     [](const Entry& e) { return e.count == 0; }),
      copy.entries.end());
  }

  *this = copy;
  return *this;
}

std::vector<Inventory::Entry> Inventory::ResolveRemoval(
  const std::vector<Entry>& wanted) const
{
  auto left = entries;
  std::vector<Entry> taken;

  auto take = [&](const Entry& e, uint32_t& remaining, bool byName) {
    for (auto& entry : left) {
      if (!remaining) {
        return;
      }
      if (!entry.count) {
        continue;
      }
      // The first pass has emptied every exact match by the time the
      // second runs, so the second takes only the stacks with another name.
      Entry asWanted = entry;
      if (byName) {
        asWanted.name = e.name;
      }
      if (!asWanted.EqualExceptCount(e)) {
        continue;
      }
      const uint32_t part = std::min(entry.count, remaining);
      Entry piece = entry;
      piece.count = part;
      auto it = std::find_if(taken.begin(), taken.end(), [&](const Entry& t) {
        return t.EqualExceptCount(piece);
      });
      if (it == taken.end()) {
        taken.push_back(piece);
      } else {
        it->count += part;
      }
      entry.count -= part;
      remaining -= part;
    }
  };

  for (auto& e : wanted) {
    if (!e.count) {
      continue;
    }
    uint32_t remaining = e.count;
    take(e, remaining, false);
    take(e, remaining, true);
    if (remaining) {
      throw std::runtime_error(
        fmt::format("Source inventory doesn't have enough {:#x} ({} is "
                    "required while {} present)",
                    e.baseId, e.count, e.count - remaining));
    }
  }
  return taken;
}

bool Inventory::HasItem(uint32_t baseId) const
{
  for (auto& entry : entries) {
    if (entry.baseId == baseId) {
      return true;
    }
  }
  return false;
}

uint32_t Inventory::GetItemCount(uint32_t baseId) const
{
  uint32_t sum = 0;
  for (auto& entry : entries) {
    if (entry.baseId == baseId) {
      sum += entry.count;
    }
  }
  return sum;
}

uint32_t Inventory::GetTotalItemCount() const
{
  uint32_t sum = 0;
  for (auto& entry : entries) {
    sum += entry.count;
  }
  return sum;
}

bool Inventory::IsEmpty() const
{
  return entries.empty();
}

nlohmann::json Inventory::ToJson() const
{
  JsonOutputArchive ar;
  const_cast<Inventory*>(this)->Serialize(ar);
  return std::move(ar.j);
}

Inventory Inventory::FromJson(const simdjson::dom::element& element)
{
  SimdJsonInputArchive ar(element);
  Inventory res;
  res.Serialize(ar);
  return res;
}

Inventory Inventory::FromJson(const nlohmann::json& j)
{
  JsonInputArchive ar(j);
  Inventory res;
  res.Serialize(ar);
  return res;
}
