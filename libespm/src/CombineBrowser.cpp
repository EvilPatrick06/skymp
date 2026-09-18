#include "libespm/CombineBrowser.h"
#include "libespm/Browser.h"
#include "libespm/RecordHeader.h"
#include "libespm/Utils.h"
#include <array>
#include <cctype>
#include <fmt/format.h>
#include <memory>
#include <string>
#include <unordered_set>

namespace espm {

namespace {
// Plugin file names are case-insensitive. Windows treats them that way, Skyrim
// treats them that way, and authors are inconsistent: CFTO.esp lists its master
// as 'Hearthfires.esm' while the file on disk is 'HearthFires.esm'. Comparing
// with == rejected those outright and took the whole load order down with them.
bool NamesEqual(const std::string& a, const char* b) noexcept
{
  size_t i = 0;
  for (; i < a.size(); ++i) {
    if (b[i] == '\0') {
      return false;
    }
    const auto x = static_cast<unsigned char>(a[i]);
    const auto y = static_cast<unsigned char>(b[i]);
    if (std::tolower(x) != std::tolower(y)) {
      return false;
    }
  }
  return b[i] == '\0';
}
}

int32_t CombineBrowser::Impl::GetFileIndex(const char* fileName) const noexcept
{
  // returns index of sources array or -1 if not found
  if (fileName[0] != '\0') {
    for (size_t i = 0; i < sources.size(); ++i) {
      if (NamesEqual(sources[i].fileName, fileName)) {
        return i;
      }
    }
  }
  return -1;
}

LookupResult CombineBrowser::LookupById(uint32_t combFormId) const noexcept
{
  // Otherwise, we'll find a TES4 record in Skyrim.esm which is not relevant
  // for CombineBrowser use cases
  if (combFormId == 0) {
    return LookupResult();
  }

  const RecordHeader* resRec = nullptr;
  uint8_t resFileIdx = 0;
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    auto& src = pImpl->sources[i];
    const uint32_t rawFormId = utils::GetMappedId(combFormId, *src.toRaw);
    if (rawFormId >= 0xff000000)
      continue;
    auto rec = src.br->LookupById(rawFormId);
    if (rec) {
      resRec = rec;
      resFileIdx = (uint8_t)i;
    }
  }
  return resRec ? LookupResult(this, resRec, resFileIdx) : LookupResult();
}

std::vector<LookupResult> CombineBrowser::LookupByIdAll(
  uint32_t combFormId) const noexcept
{
  std::vector<LookupResult> res;
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    auto& src = pImpl->sources[i];
    const uint32_t rawFormId = utils::GetMappedId(combFormId, *src.toRaw);
    if (rawFormId >= 0xff000000)
      continue;
    const RecordHeader* rec = src.br->LookupById(rawFormId);
    if (rec) {
      res.push_back({ this, rec, static_cast<uint8_t>(i) });
    }
  }
  return res;
}

std::pair<const RecordHeader**, size_t> CombineBrowser::FindNavMeshes(
  uint32_t worldSpaceId, CellOrGridPos cellOrGridPos) const noexcept
{
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    auto& src = pImpl->sources[i];
    const uint32_t rawFormId = utils::GetMappedId(worldSpaceId, *src.toRaw);
    if (rawFormId >= 0xff000000)
      continue;
    auto p = src.br->FindNavMeshes(worldSpaceId, cellOrGridPos);
    auto front = p.first;
    auto size = p.second;
    if (front && *front) {
      return { front, size };
    }
  }
  return { nullptr, 0 };
}

std::vector<const std::vector<const RecordHeader*>*>
CombineBrowser::GetRecordsByType(const char* type) const
{
  std::vector<const std::vector<const RecordHeader*>*> res;
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    res.push_back(&pImpl->sources[i].br->GetRecordsByType(type));
  }
  return res;
}

std::vector<LookupResult> CombineBrowser::GetDistinctRecordsByType(
  const char* type) const
{
  if (pImpl->numSources == 0) {
    return {};
  }

  std::unordered_set<formId> formSet;
  std::vector<LookupResult> result;
  for (size_t i = pImpl->numSources - 1; i != static_cast<size_t>(-1); --i) {
    const auto& records = pImpl->sources[i].br->GetRecordsByType(type);
    formSet.reserve(records.size());
    result.reserve(records.size());

    for (auto record : records) {
      auto mappedId =
        utils::GetMappedId(record->GetId(), *pImpl->sources[i].toComb);
      if (formSet.insert(mappedId).second) {
        result.push_back(LookupResult(this, record, static_cast<uint8_t>(i)));
      }
    }
  }

  return result;
}

std::vector<const std::vector<const RecordHeader*>*>
CombineBrowser::GetRecordsAtPos(uint32_t cellOrWorld, int16_t cellX,
                                int16_t cellY) const
{
  std::vector<const std::vector<const RecordHeader*>*> res;
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    res.push_back(
      &pImpl->sources[i].br->GetRecordsAtPos(cellOrWorld, cellX, cellY));
  }
  return res;
}

const IdMapping* CombineBrowser::GetCombMapping(
  size_t fileIndex) const noexcept
{
  if (fileIndex >= pImpl->numSources) {
    return nullptr;
  }
  return pImpl->sources[fileIndex].toComb.get();
}

const IdMapping* CombineBrowser::GetRawMapping(size_t fileIndex) const noexcept
{
  if (fileIndex >= pImpl->numSources) {
    return nullptr;
  }
  return pImpl->sources[fileIndex].toRaw.get();
}

CompressedFieldsCache& CombineBrowser::GetCache() const noexcept
{
  return pImpl->cache;
}

const GroupStack& CombineBrowser::GetParentGroupsEnsured(
  const RecordHeader* rec) const
{
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    const auto result = pImpl->sources[i].br->GetParentGroupsOptional(rec);
    if (result) {
      return *result;
    }
  }
  throw std::runtime_error(fmt::format(
    "espm::CombineBrowser: no browsers know record id={:#x}", rec->GetId()));
}

const std::vector<const void*>& CombineBrowser::GetSubsEnsured(
  const GroupHeader* group) const
{
  for (size_t i = 0; i < pImpl->numSources; ++i) {
    const auto result = pImpl->sources[i].br->GetSubsOptional(group);
    if (result) {
      return *result;
    }
  }
  throw std::runtime_error(
    "espm::CombineBrowser: no browsers know requested group");
}

}
