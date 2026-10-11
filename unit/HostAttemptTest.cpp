#include "ActionListener.h"
#include "HostMessage.h"
#include "HostStartMessage.h"
#include "TestUtils.hpp"

PartOne& GetPartOne();

TEST_CASE("Hosting an actor before its first movement records the timestamp",
          "[HostAttempt][PartOne]")
{
  auto& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  p.SetUserActor(0, 0xff000000);
  p.CreateActor(0xff000001, { 0, 0, 0 }, 0, 0x3c);
  auto& remote = p.worldState.GetFormAt<MpActor>(0xff000001);
  auto remoteIdx = remote.GetIdx();

  SECTION("No actor has sent movement yet")
  {
    REQUIRE(p.worldState.lastMovUpdateByIdx.empty());
  }
  SECTION("Only an earlier actor has sent movement")
  {
    REQUIRE(remoteIdx > 0);
    p.worldState.lastMovUpdateByIdx.resize(remoteIdx);
    p.worldState.lastMovUpdateByIdx[0] =
      std::chrono::system_clock::now() - std::chrono::seconds(10);
  }
  SECTION("The remote already has an allocated timestamp slot")
  {
    p.worldState.lastMovUpdateByIdx.resize(remoteIdx + 1);
  }

  const auto timestampsBefore = p.worldState.lastMovUpdateByIdx;
  const auto beforeHosting = std::chrono::system_clock::now();
  RawMessageData rawMsgData;
  rawMsgData.userId = 0;
  HostMessage msg;
  msg.remoteId = remote.GetFormId();
  ActionListener listener(p);
  const auto identityBefore = remote.GetRuntimeIdentity();
  const auto bodyBefore = remote.GetRuntimeLifeIdentity();

  listener.OnHostAttempt(rawMsgData, msg);

  REQUIRE(remote.GetRuntimeIdentity() != identityBefore);
  REQUIRE(remote.GetRuntimeLifeIdentity() != bodyBefore);

  REQUIRE(p.worldState.lastMovUpdateByIdx.size() > remoteIdx);
  REQUIRE(p.worldState.lastMovUpdateByIdx[remoteIdx].has_value());
  REQUIRE(*p.worldState.lastMovUpdateByIdx[remoteIdx] >= beforeHosting);
  REQUIRE(p.worldState.hosters.at(remote.GetFormId()) == 0xff000000);
  for (size_t i = 0; i < remoteIdx && i < timestampsBefore.size(); ++i) {
    REQUIRE(p.worldState.lastMovUpdateByIdx[i] == timestampsBefore[i]);
  }
  bool hostStarted = false;
  for (const auto& sent : p.Messages()) {
    if (auto start = dynamic_cast<HostStartMessage*>(sent.message.get())) {
      hostStarted = hostStarted || start->target == remote.GetFormId();
    }
  }
  REQUIRE(hostStarted);

  const auto timestamp = p.worldState.lastMovUpdateByIdx[remoteIdx];
  const auto messageCount = p.Messages().size();
  const auto sameOwner = remote.GetRuntimeIdentity();
  listener.OnHostAttempt(rawMsgData, msg);
  REQUIRE(p.worldState.lastMovUpdateByIdx[remoteIdx] == timestamp);
  REQUIRE(p.Messages().size() == messageCount);
  REQUIRE(remote.GetRuntimeIdentity() == sameOwner);

  DoConnect(p, 1);
  p.CreateActor(0xff000002, {0, 0, 0}, 0, 0x3c);
  p.SetUserActor(1, 0xff000002);
  p.worldState.lastMovUpdateByIdx[remoteIdx] =
    std::chrono::system_clock::now() - std::chrono::seconds(3);
  rawMsgData.userId = 1;
  listener.OnHostAttempt(rawMsgData, msg);
  REQUIRE(p.worldState.hosters.at(remote.GetFormId()) == 0xff000002);
  REQUIRE(remote.GetRuntimeIdentity() != sameOwner);
  REQUIRE(remote.GetRuntimeLifeIdentity() != bodyBefore);
}
