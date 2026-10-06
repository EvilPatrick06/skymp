#pragma once
#include "CellOrGridPos.h"
#include "GroupStack.h"
#include "IdMapping.h"
#include "LoadOrder.h"
#include "LookupResult.h"

namespace espm {

class RecordHeader;
class Browser;

class CombineBrowser
{
  friend class Combiner;

public:
  // Returns default constructed LookupResult on failure
  // Gets record from the last file in the load order
  LookupResult LookupById(uint32_t formId) const noexcept;

  // Returns a record for each file adding/editing record with such id
  std::vector<LookupResult> LookupByIdAll(uint32_t formId) const noexcept;

  std::pair<const RecordHeader**, size_t> FindNavMeshes(
    uint32_t worldSpaceId, CellOrGridPos cellOrGridPos) const noexcept;

  std::vector<const std::vector<const RecordHeader*>*> GetRecordsByType(
    const char* type) const;

  std::vector<LookupResult> GetDistinctRecordsByType(const char* type) const;

  std::vector<const std::vector<const RecordHeader*>*> GetRecordsAtPos(
    uint32_t cellOrWorld, int16_t cellX, int16_t cellY) const;

  // Returns nullptr on failure
  const IdMapping* GetCombMapping(size_t fileIndex) const noexcept;
  const RawIdMapping* GetRawMapping(size_t fileIndex) const noexcept;

  // Every source in the order it was added, with the slot the game gives it
  const LoadOrder& GetLoadOrder() const noexcept;

  // CompressedFieldsCache is not logically related to Combiner, this method is
  // added for usability
  espm::CompressedFieldsCache& GetCache() const noexcept;

  const GroupStack& GetParentGroupsEnsured(const RecordHeader* rec) const;
  const std::vector<const void*>& GetSubsEnsured(
    const GroupHeader* group) const;

private:
  struct Source
  {
    Browser* br = nullptr;
    std::string fileName;
    std::unique_ptr<espm::IdMapping> toComb;
    std::unique_ptr<espm::RawIdMapping> toRaw;
  };

  struct Impl
  {
    CompressedFieldsCache cache;

    // Up to 254 full and 4096 light plugins, the game's own limits, which
    // Combiner::Combine enforces. A fixed 256 used to cap the two together.
    std::vector<Source> sources;
    LoadOrder loadOrder;
    int32_t GetFileIndex(const char* fileName) const noexcept;
  };
  std::shared_ptr<Impl> pImpl;

  CombineBrowser() = default;
  CombineBrowser(const CombineBrowser&) = delete;
  void operator=(const CombineBrowser&) = delete;
};

}
