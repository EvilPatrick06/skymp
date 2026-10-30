#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <simdjson.h>
#include <string>
#include <tuple>
#include <vector>

class Inventory
{
public:
  enum class Worn
  {
    None = 0,
    Right,
    Left
  };

  // Doesn't parse extra data currently
  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("entries", entries);
  }

  // TODO: get rid of this in favor of Serialize
  nlohmann::json ToJson() const;
  static Inventory FromJson(const simdjson::dom::element& element);
  static Inventory FromJson(const nlohmann::json& j);

  class ExtraData
  {
  public:
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("health", health)
        .Serialize("enchantmentId", enchantmentId)
        .Serialize("maxCharge", maxCharge)
        .Serialize("removeEnchantmentOnUnequip", removeEnchantmentOnUnequip)
        .Serialize("chargePercent", chargePercent)
        .Serialize("name", name)
        .Serialize("soul", soul)
        .Serialize("poisonId", poisonId)
        .Serialize("poisonCount", poisonCount)
        .Serialize("worn", worn_)
        .Serialize("wornLeft", wornLeft);
    }

    std::optional<float> health;
    std::optional<uint32_t> enchantmentId;
    std::optional<float> maxCharge;
    std::optional<bool> removeEnchantmentOnUnequip;
    std::optional<float> chargePercent;
    std::optional<std::string> name;
    std::optional<uint8_t> soul;
    std::optional<uint32_t> poisonId;
    std::optional<uint32_t> poisonCount;
    std::optional<bool> worn_;
    std::optional<bool> wornLeft;
  };

  class Entry : public ExtraData
  {
  public:
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("baseId", baseId).Serialize("count", count);

      ExtraData::Serialize(archive);
    }

    Entry();
    Entry(uint32_t baseId_, uint32_t count_,
          const ExtraData& extraData_ = ExtraData());

    uint32_t baseId = 0;
    uint32_t count = 0;

    // TODO: get rid of this in favor of Serialize
    static Entry FromJson(const simdjson::dom::element& e);

    Worn GetWorn() const;
    void SetWorn(Worn worn);
    bool EqualExceptCount(const Entry& other) const;

    friend bool operator==(const Entry& lhs, const Entry& rhs)
    {
      return lhs.EqualExceptCount(rhs) && lhs.count == rhs.count;
    }

    friend bool operator!=(const Entry& lhs, const Entry& rhs)
    {
      return !(lhs == rhs);
    }
  };

  // AddItems refuses, with std::runtime_error and nothing changed, an add
  // that would take a stack past what Entry::count holds. CanAddItems says
  // beforehand whether it would, for callers that take something first.
  Inventory& AddItem(uint32_t baseId, uint32_t count);
  Inventory& AddItems(const std::vector<Entry>& entries);
  bool CanAddItems(const std::vector<Entry>& entries) const;
  Inventory& RemoveItems(const std::vector<Entry>& entries);

  /*
    THORNSWOOD PATCH (Thornswood #3163). The stacks a removal that names
    only what an item is would take, for the removals the client asks for by
    a base id alone: eating or drinking one, and a craft's inputs.

    Each wanted entry takes from the stacks that match it exactly first,
    then from the stacks that differ from it only in their name, in the
    order they sit. So a named entry (food carrying its days left, "Venison
    (6 days left)") can be eaten and cooked, while a stack that differs in
    anything else (worn, tempered, enchanted, poisoned, a soul) is still
    never taken for it. The answer is the stacks to take, each with its own
    extra and the count to take from it, to hand to RemoveItems; nothing
    here changes the inventory. Throws std::runtime_error, with
    RemoveItems's words, when the stacks it may take hold too few.
  */
  std::vector<Entry> ResolveRemoval(const std::vector<Entry>& wanted) const;
  bool HasItem(uint32_t baseId) const;
  uint32_t GetItemCount(uint32_t baseId) const;
  uint32_t GetTotalItemCount() const;
  bool IsEmpty() const;

  std::vector<Entry> entries;

  friend bool operator==(const Inventory& lhs, const Inventory& rhs)
  {
    return lhs.entries == rhs.entries;
  }

  friend bool operator!=(const Inventory& lhs, const Inventory& rhs)
  {
    return !(lhs == rhs);
  }
};
