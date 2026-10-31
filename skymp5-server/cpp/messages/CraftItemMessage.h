#pragma once
#include "../server_guest_lib/Inventory.h"
#include "MessageBase.h"
#include "MsgType.h"
#include <type_traits>

struct CraftItemMessage : public MessageBase<CraftItemMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::CraftItem)>{};

  struct Data
  {
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("t", kMsgType)
        .Serialize("workbench", workbench)
        .Serialize("craftInputObjects", craftInputObjects)
        .Serialize("resultObjectId", resultObjectId)
        .Serialize("resultPotion", resultPotion);
    }

    uint32_t workbench = 0;
    Inventory craftInputObjects;
    uint32_t resultObjectId = 0;
    // THORNSWOOD PATCH (Thornswood #1649). When what arrived is a bottle the
    // game brewed (a runtime potion), what is in it, read off the form by
    // the client. The bottle's own id is only good for that session.
    std::optional<Inventory::Made> resultPotion;
  };

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType).Serialize("data", data);
  }

  Data data;
};
