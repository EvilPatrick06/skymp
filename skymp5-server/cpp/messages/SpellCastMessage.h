#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <optional>
#include <type_traits>

struct ActorAnimationVariables
{
  std::vector<uint8_t> booleans;
  std::vector<uint8_t> floats;
  std::vector<uint8_t> integers;

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("booleans", booleans)
      .Serialize("floats", floats)
      .Serialize("integers", integers);
  }
};

struct SpellCastMsgData
{
  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("caster", caster)
      .Serialize("target", target)
      .Serialize("spell", spell)
      .Serialize("isDualCasting", isDualCasting)
      .Serialize("interruptCast", interruptCast)
      .Serialize("castingSource", castingSource)
      .Serialize("aimAngle", aimAngle)
      .Serialize("aimHeading", aimHeading)
      .Serialize("actorAnimationVariables", actorAnimationVariables)
      .Serialize("durationMult", durationMult);
  }

  uint32_t caster = 0;
  uint32_t target = 0;
  uint32_t spell = 0;
  bool isDualCasting = false;
  bool interruptCast = false;
  int32_t castingSource = 0;
  float aimAngle = 0.f;
  float aimHeading = 0.f;

  ActorAnimationVariables actorAnimationVariables;

  // THORNSWOOD PATCH (Thornswood #2848). How many times its usual time each
  // effect of this cast holds, on whichever machine runs that effect. 1 is
  // the game's own. The caster's machine sends what it gave its own
  // effects; neighbours get the server's answer (SpellCastEvent.h), never
  // the caster's word. Absent is the game's own.
  std::optional<float> durationMult;
};

struct SpellCastMessage : public MessageBase<SpellCastMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::SpellCast)>{};

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType).Serialize("data", data);
  }

  SpellCastMsgData data;
};
