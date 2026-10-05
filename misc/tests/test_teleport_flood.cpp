// Thornswood #1932. Replays the dev teleport flood of 5 Oct 2026 through the
// server code that produced it (MovementValidation::Validate,
// ActionListener::OnUpdateMovement, MpActor::Teleport and the door branch of
// MpObjectReference::ProcessActivateNormal, copied out of the sources by
// test_teleport_flood.js) and counts the teleports the client is sent.
//
// Measured on dev 1.1.105.10: the server loop stopped for 48 s from
// 11:50:27 ("worst 48050ms"); a door activation held in it moved the
// server's copy of the character out of the Bannered Mare (1605e) to
// [25669.7, -7632.9, -3237.4]
// in WhiterunWorld (1a26f). skyrim-platform.log then has 233 "Teleporting
// id 2 refrId 14" to that spot: the door's TeleportMessage and one
// TeleportMessage2 for each of the 232 movement packets the client had sent
// from inside since pressing the door.
//
// Exit code 0 when the counts are the ones the fix promises, 1 otherwise.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "FormDesc.h"
#include "LocationalData.h"
#include "NiPoint3.h"
#include "TeleportNumbering.h"

namespace Networking {
using UserId = unsigned short;
constexpr UserId InvalidUserId = 65535;
}

struct TeleportMessage
{
  uint32_t idx = 0;
  std::array<float, 3> pos{}, rot{};
  uint32_t worldOrCell = 0;
  std::optional<uint32_t> teleportSeq;
};

struct TeleportMessage2
{
  std::array<float, 3> pos{}, rot{};
  uint32_t worldOrCell = 0;
  std::optional<uint32_t> teleportSeq;
};

struct UpdateMovementMessage
{
  struct Data
  {
    uint32_t worldOrCell = 0;
    std::array<float, 3> pos{ 0, 0, 0 };
    std::array<float, 3> rot{ 0, 0, 0 };
    std::string runMode = "Standing";
    bool isInJumpState = false;
    bool isSneaking = false;
    bool isBlocking = false;
    bool isWeapDrawn = false;
    bool isDead = false;
    std::optional<uint32_t> teleportSeq;
  };
  uint32_t idx = 0;
  Data data;
};

struct RawMessageData
{
  Networking::UserId userId = 0;
};

// What the client was told: every teleport of its own character.
struct Sent
{
  bool correction; // TeleportMessage2 from movement validation
  std::optional<uint32_t> teleportSeq;
  uint32_t worldOrCell;
  std::array<float, 3> pos;
};
static std::vector<Sent> g_sent;

enum class SetPosMode
{
  CalledByUpdateMovement,
  Other
};
using SetAngleMode = SetPosMode;
enum class AnimationVariableBool
{
  kInvalidVariable,
  kVariable_bInJumpState,
  kVariable__skymp_isWeapDrawn,
  kVariable_IsBlocking,
  kVariable_IsSneaking,
};

struct WorldState
{
  std::vector<std::string> espmFiles{ "Skyrim.esm" };
  std::vector<std::optional<std::chrono::system_clock::time_point>>
    lastMovUpdateByIdx;
  uint32_t lastTeleportSeq = 0;
};

class MpActor
{
public:
  struct Impl
  {
    uint32_t newestTeleportSeq = 0;
  };

  MpActor(WorldState* parent_, Networking::UserId userId_)
    : parent(parent_)
    , userId(userId_)
    , pImpl(new Impl)
  {
  }
  ~MpActor() { delete pImpl; }

  WorldState* GetParent() const { return parent; }
  Networking::UserId GetUserId() const { return userId; }
  MpActor& GetActorToSendTo() { return *this; }
  MpActor* AsActor() { return this; }
  uint32_t GetIdx() const { return 2; }

  void SendToUser(const TeleportMessage& msg, bool)
  {
    g_sent.push_back({ false, msg.teleportSeq, msg.worldOrCell, msg.pos });
  }

  const NiPoint3& GetPos() const { return pos; }
  const NiPoint3& GetAngle() const { return angle; }
  const FormDesc& GetCellOrWorld() const { return cellOrWorld; }
  void SetPos(const NiPoint3& p, SetPosMode = SetPosMode::Other) { pos = p; }
  void SetAngle(const NiPoint3& a, SetAngleMode = SetAngleMode::Other)
  {
    angle = a;
  }
  void SetCellOrWorldObsolete(const FormDesc& f) { cellOrWorld = f; }

  bool GetTeleportFlag() const { return false; }
  void SetTeleportFlag(bool) {}
  void IncreaseBlockCount() {}
  void ResetBlockCount() {}
  uint32_t GetBlockCount() const { return 0; }
  void SetIsBlockActive(bool) {}
  void SetAnimationVariableBool(AnimationVariableBool, bool) {}
  void SetLastAnimEvent(std::nullopt_t) {}

  void Teleport(const LocationalData& position);
  std::optional<uint32_t> NumberTeleportForOwnClient();
  bool IsSentBeforeNewestTeleport(std::optional<uint32_t> carriedOut) const;
  void ForgetTeleportsForOwnClient();

private:
  WorldState* parent;
  Networking::UserId userId;
  NiPoint3 pos, angle;
  FormDesc cellOrWorld;
  Impl* pImpl;
};
using MpObjectReference = MpActor;

struct ServerState
{
  MpActor* actor = nullptr;
  MpActor* ActorByUser(Networking::UserId) { return actor; }
};

struct PartOneSendTargetWrapper
{
  void Send(Networking::UserId, const TeleportMessage2& msg, bool)
  {
    g_sent.push_back({ true, msg.teleportSeq, msg.worldOrCell, msg.pos });
  }
};

struct PartOne
{
  ServerState serverState;
  WorldState worldState;
  PartOneSendTargetWrapper sendTarget;
  PartOneSendTargetWrapper& GetSendTarget() { return sendTarget; }
};

#include "MovementValidationUnderTest.inc"

class ActionListener
{
public:
  explicit ActionListener(PartOne& partOne_)
    : partOne(partOne_)
  {
  }
  MpActor* SendToNeighbours(uint32_t, const RawMessageData& raw)
  {
    return partOne.serverState.ActorByUser(raw.userId);
  }
  void OnUpdateMovement(const RawMessageData& rawMsgData,
                        const UpdateMovementMessage& msg);

private:
  PartOne& partOne;
};

#include "MpActorTeleportUnderTest.inc"
#include "OnUpdateMovementUnderTest.inc"

static void DoorTeleport(MpObjectReference& activationSource,
                         WorldState* worldState, const float (&pos)[3],
                         const float (&rot)[3], uint32_t teleportWorldOrCell)
{
  MpActor* actorActivator = activationSource.AsActor();
#include "DoorTeleportUnderTest.inc"
}

namespace {
constexpr uint32_t kBanneredMare = 0x1605e;
constexpr uint32_t kWhiterunWorld = 0x1a26f;
const float kInside[3] = { -113.6f, -810.f, 69.3f };
const float kOutside[3] = { 25669.7f, -7632.93f, -3237.44f };
const float kNoRot[3] = { 0, 0, 0 };

// The client's side: where it says it is and the newest teleport number it
// has carried out. A client without the fix sends no number.
struct Client
{
  uint32_t cell;
  NiPoint3 pos;
  std::optional<uint32_t> carriedOut;

  UpdateMovementMessage Packet(int jitter) const
  {
    UpdateMovementMessage m;
    m.idx = 2;
    m.data.worldOrCell = cell;
    m.data.pos = { pos.x + jitter % 7, pos.y, pos.z };
    m.data.teleportSeq = carriedOut;
    return m;
  }
  void CarryOut(const Sent& s)
  {
    if (TELEPORT_NUMBERING_IN_SOURCE) {
      carriedOut = s.teleportSeq;
    }
  }
};

size_t Since(size_t mark, bool correction)
{
  return std::count_if(
    g_sent.begin() + mark, g_sent.end(),
    [&](const Sent& s) { return s.correction == correction; });
}

int failures = 0;
void Expect(const char* what, size_t got, size_t want)
{
  std::printf("%-74s %4zu (want %zu)%s\n", what, got, want,
              got == want ? "" : "  FAIL");
  if (got != want) {
    failures++;
  }
}
}

int main()
{
  PartOne partOne;
  MpActor actor(&partOne.worldState, 0);
  partOne.serverState.actor = &actor;
  ActionListener listener(partOne);
  RawMessageData raw;

  actor.SetCellOrWorldObsolete(
    FormDesc::FromFormId(kBanneredMare, partOne.worldState.espmFiles));
  actor.SetPos({ kInside[0], kInside[1], kInside[2] });

  Client client{ kBanneredMare,
                 { kInside[0], kInside[1], kInside[2] },
                 std::nullopt };
  if (TELEPORT_NUMBERING_IN_SOURCE) {
    client.carriedOut = 0; // what a client sends before its first teleport
  }

  std::printf("teleport numbering in the source: %s\n",
              TELEPORT_NUMBERING_IN_SOURCE ? "yes" : "no");

  // 1. Before the door: the client inside, the server agrees.
  size_t mark = g_sent.size();
  listener.OnUpdateMovement(raw, client.Packet(0));
  Expect("teleports for a packet from inside before the door",
         g_sent.size() - mark, 0);

  // 2. The held door activation is processed: one door teleport.
  mark = g_sent.size();
  DoorTeleport(actor, &partOne.worldState, kOutside, kNoRot, kWhiterunWorld);
  Expect("door teleports sent", Since(mark, false), 1);
  const Sent door = g_sent.back();

  // 3. The 232 packets the client sent from inside during the stall.
  mark = g_sent.size();
  for (int i = 0; i < 232; ++i) {
    listener.OnUpdateMovement(raw, client.Packet(i));
  }
  Expect("corrections for the 232 packets sent from inside before the door",
         Since(mark, true), 0);
  Expect("teleports the client is sent for the door and the 232 packets",
         Since(0, false) + Since(0, true), 1);
  Expect("server copy still outside after them (1 = yes)",
         actor.GetCellOrWorld().ToFormId(partOne.worldState.espmFiles) ==
           kWhiterunWorld,
         1);

  // 4. The client carries out the door teleport and reports from outside.
  client.CarryOut(door);
  client.cell = kWhiterunWorld;
  client.pos = { kOutside[0], kOutside[1], kOutside[2] };
  mark = g_sent.size();
  for (int i = 0; i < 10; ++i) {
    listener.OnUpdateMovement(raw, client.Packet(i));
  }
  Expect("teleports once the client reports from outside",
         g_sent.size() - mark, 0);
  Expect("server copy follows the client outside (1 = yes)",
         (actor.GetPos() - client.pos).Length() < 8.f, 1);

  // 5. A server teleport whose move does not happen on the client (the
  // client carries it out but stays): one new correction, not one per packet.
  mark = g_sent.size();
  actor.Teleport(
    { { kOutside[0] + 10000.f, kOutside[1], kOutside[2] },
      { 0, 0, 0 },
      FormDesc::FromFormId(kWhiterunWorld, partOne.worldState.espmFiles) });
  const Sent moved = g_sent.back();
  for (int i = 0; i < 5; ++i) {
    // sent before the client received it
    listener.OnUpdateMovement(raw, client.Packet(i));
  }
  client.CarryOut(moved); // the move did not take; still at `before`
  for (int i = 0; i < 5; ++i) {
    listener.OnUpdateMovement(raw, client.Packet(i));
  }
  Expect("corrections for 10 packets around a teleport not carried out",
         Since(mark, true), 1);
  const Sent retry = g_sent.back();
  client.CarryOut(retry);
  client.pos = { retry.pos[0], retry.pos[1], retry.pos[2] };
  mark = g_sent.size();
  listener.OnUpdateMovement(raw, client.Packet(0));
  Expect("teleports once the client carried the correction out",
         g_sent.size() - mark, 0);

  std::printf("total sent: %zu teleports, %zu corrections\n", Since(0, false),
              Since(0, true));
  std::printf(failures ? "FAIL\n" : "PASS\n");
  return failures ? 1 : 0;
}
