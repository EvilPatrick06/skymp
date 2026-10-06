#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

struct TeleportMessage2 : public MessageBase<TeleportMessage2>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::Teleport2)>{};

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType)
      .Serialize("pos", pos)
      .Serialize("rot", rot)
      .Serialize("worldOrCell", worldOrCell)
      .Serialize("teleportSeq", teleportSeq);
  }

  std::array<float, 3> pos, rot;
  uint32_t worldOrCell = 0;

  // THORNSWOOD. Set when this tells a client to move its own character: the
  // number the client echoes back in UpdateMovementMessage::Data::teleportSeq
  // once it has carried the move out. See MpActor::NumberTeleportForOwnClient.
  std::optional<uint32_t> teleportSeq;
};
