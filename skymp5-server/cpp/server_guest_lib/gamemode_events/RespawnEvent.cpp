#include "RespawnEvent.h"

#include "MpActor.h"
#include "WorldState.h"

RespawnEvent::RespawnEvent(MpActor* actor_, bool shouldTeleport_)
  : actorId(actor_->GetFormId())
  , runtimeIdentity(actor_->GetRuntimeIdentity())
  , shouldTeleport(shouldTeleport_)
{
  if (actor_->GetParent()) {
    actor = std::dynamic_pointer_cast<MpActor>(
      actor_->GetParent()->LookupFormById(actorId));
    if (actor.get() != actor_) actor.reset();
  }
}

const char* RespawnEvent::GetName() const
{
  return "onRespawn";
}

std::string RespawnEvent::GetArgumentsJsonArray() const
{
  std::string result;
  result += "[";
  result += std::to_string(actorId);
  result += "]";
  return result;
}

void RespawnEvent::OnFireSuccess(WorldState*)
{
  if (!actor || !actor->HasRuntimeIdentity(runtimeIdentity)) return;
  actor->SendAndSetDeathState(false, shouldTeleport);

  // TODO: should probably not sending to ourselves. see also RespawnTest.cpp
  actor->SendMessageToActorListeners(
    actor->CreatePropertyMessage_(actor, "isDead", "false"), true);
}
