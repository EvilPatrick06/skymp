#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
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

  /*
    THORNSWOOD PATCH (Thornswood #1649). A made thing, kept as what it is.

    A potion brewed at a lab is a form the brewing game made while it ran
    (above 0xff000000). The id means nothing to the next session or to any
    other client, so the server keeps the bottle by its contents instead:
    each effect's magic effect record (MGEF, a global form id), magnitude,
    area and duration, the name the game gave it and whether it is a
    poison. The client builds a form from this on arrival (createMadePotion)
    and the server applies these effects when it is drunk.

    An entry carrying `made` has the base id MadeBaseId(made): the same
    contents always get the same id, so two bottles of the same brew stack
    wherever they are, whoever brewed them, in every session.
  */
  struct MadeEffect
  {
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("effectId", effectId)
        .Serialize("magnitude", magnitude)
        .Serialize("area", area)
        .Serialize("duration", duration);
    }

    uint32_t effectId = 0;
    float magnitude = 0.f;
    uint32_t area = 0;
    uint32_t duration = 0;

    friend bool operator==(const MadeEffect& lhs, const MadeEffect& rhs)
    {
      return std::tie(lhs.effectId, lhs.magnitude, lhs.area, lhs.duration) ==
        std::tie(rhs.effectId, rhs.magnitude, rhs.area, rhs.duration);
    }
  };

  struct Made
  {
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("name", name)
        .Serialize("poison", poison)
        .Serialize("effects", effects);
    }

    std::string name;
    bool poison = false;
    std::vector<MadeEffect> effects;

    friend bool operator==(const Made& lhs, const Made& rhs)
    {
      return std::tie(lhs.name, lhs.poison, lhs.effects) ==
        std::tie(rhs.name, rhs.poison, rhs.effects);
    }
    friend bool operator!=(const Made& lhs, const Made& rhs)
    {
      return !(lhs == rhs);
    }
  };

  // The base id an entry carrying `made` has. In 0xff800000..0xffffffff,
  // the top half of the runtime range: a game session hands out its own
  // runtime ids from the bottom of that range upwards, one at a time, and
  // never gets near the top, so the client can give the form it builds this
  // very id. FNV-1a over the canonical text of `made` (Made::Canonical).
  static constexpr uint32_t kMadeIdFirst = 0xff800000;
  static uint32_t MadeBaseId(const Made& made);

  // Magnitudes are kept to two decimals so the same brew made twice is the
  // same text, and the text survives a JSON round trip. Made::Round applies
  // that, Canonical writes the text the id is hashed from.
  static void Round(Made& made);
  static std::string Canonical(const Made& made);

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
        .Serialize("wornLeft", wornLeft)
        .Serialize("made", made);
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
    std::optional<Made> made; // Thornswood #1649, see Made above
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
