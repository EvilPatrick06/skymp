#include "EquipmentBinding.h"
#include "NapiHelper.h"
#include <sstream>
#include <stdexcept>

Napi::Value EquipmentBinding::Get(Napi::Env env, ScampServer& scampServer,
                                  uint32_t formId)
{
  auto& partOne = scampServer.GetPartOne();

  auto& actor = partOne->worldState.GetFormAt<MpActor>(formId);
  auto& equipment = actor.GetEquipment();
  auto equipmentDump = equipment.ToJson().dump();
  return NapiHelper::ParseJson(env, equipmentDump);
}

/*
  THORNSWOOD #1560. What an actor wears, written by the server.

  Upstream has a Get here and no Set, so a gamemode could put clothes in a
  pack and mark the inventory entry worn, and that was all: the client dresses
  the character it owns from the equipment record, which only ever filled from
  what that client reported. A new character arrived with an empty record,
  and Thornswood grew a stack of loops that kept asking the client to put the
  clothes on, one of which put back on clothes people had taken off.

  This writes the record itself, for an actor no client owns: a character at
  the moment it is made, before it is handed to the person who made it, and
  NPCs. Everybody who can see the actor is told, the same way EquipBestWeapon
  tells them. An actor somebody's game owns is refused, because that game is
  where they put things on and take them off, and it reports every change.

  Every entry has to be worn and has to be in the actor's inventory, so this
  can dress an actor in what it has and never hand it anything.

  THORNSWOOD #1651. A write that changes nothing but names is taken for any
  actor, owned or not, and told to nobody (MpActor::RenameEquipment). The
  record names each piece as the owner's game last reported it, and the
  gamemode renames pieces in the pack (a Thornswood piece carries its
  condition on its name), so it renames the record's entry in the same step
  and the record never names a piece the pack no longer holds. Nothing worn
  changes, so there is nothing for the owner's game to report or refuse.
*/
void EquipmentBinding::Set(Napi::Env env, ScampServer& scampServer,
                           uint32_t formId, Napi::Value newValue)
{
  auto& partOne = scampServer.GetPartOne();

  auto& actor = partOne->worldState.GetFormAt<MpActor>(formId);

  if (newValue.IsObject()) {
    nlohmann::json renamed =
      nlohmann::json::parse(NapiHelper::Stringify(env, newValue));
    // The change counter is the record's own; a rename leaves it as it is.
    renamed["numChanges"] = 0;
    if (renamed.contains("inv") && renamed["inv"].is_object() &&
        renamed["inv"].contains("entries") &&
        renamed["inv"]["entries"].is_array() &&
        actor.RenameEquipment(Equipment::FromJson(renamed))) {
      return;
    }
  }

  if (partOne->serverState.UserByActor(&actor) != Networking::InvalidUserId) {
    throw std::runtime_error(
      "mp.set can only change 'equipment' for an actor no client owns, "
      "except to rename its entries; the game of the person who owns this "
      "one reports what it wears");
  }

  if (!newValue.IsObject()) {
    throw std::runtime_error(
      "mp.set 'equipment' takes an object like { inv: { entries: [...] } }");
  }

  nlohmann::json j =
    nlohmann::json::parse(NapiHelper::Stringify(env, newValue));
  if (!j.contains("inv") || !j["inv"].is_object() ||
      !j["inv"].contains("entries") || !j["inv"]["entries"].is_array()) {
    throw std::runtime_error(
      "mp.set 'equipment' takes an object like { inv: { entries: [...] } }");
  }
  // The change counter is the record's own, so whatever the caller sent is
  // ignored and MpActor::ReplaceEquipment counts this change.
  j["numChanges"] = 0;
  Equipment equipment = Equipment::FromJson(j);

  const auto& inventory = actor.GetInventory();
  for (const auto& entry : equipment.inv.entries) {
    if (entry.GetWorn() == Inventory::Worn::None) {
      std::stringstream ss;
      ss << "mp.set 'equipment': " << std::hex << entry.baseId
         << " is not marked worn, and the record holds only what is worn";
      throw std::runtime_error(ss.str());
    }
    if (!inventory.HasItem(entry.baseId)) {
      std::stringstream ss;
      ss << "mp.set 'equipment': " << std::hex << entry.baseId
         << " is not in its inventory";
      throw std::runtime_error(ss.str());
    }
  }

  actor.ReplaceEquipment(equipment);
}
