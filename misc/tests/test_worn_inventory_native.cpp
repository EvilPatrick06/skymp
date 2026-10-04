// Engine-boundary fixture: compile the actual AddItemEx body with a queued
// inventory/equipment manager. No JavaScript equip mock supplies the outfit.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <vector>
#include "../../viet/include/TaskQueue.h"
#include "../../skyrim-platform/src/platform_se/skyrim_platform/InventoryQueueFence.h"
using IVM = void;
using StackID = int;
struct FixedString { const char* value; const char* data() const { return value; } };
namespace RE {
enum class FormType { Armor, Light, Weapon, Ammo, ActorCharacter, Misc };
enum class ExtraDataType { kHealth, kEnchantment, kCharge, kTextDisplayData, kSoul, kPoison };
enum class ITEM_REMOVE_REASON { kRemove };
enum class SOUL_LEVEL { None };
struct StaticFunctionTag {};
struct BSExtraData { BSExtraData* next = nullptr; };
struct BaseExtraList { struct PresenceBitfield { uint8_t bits[64] = {}; }; };
struct ExtraDataList {
  struct Data {
    BSExtraData* value = nullptr;
    BaseExtraList::PresenceBitfield presence;
    BSExtraData*& GetData() { return value; }
    BaseExtraList::PresenceBitfield* GetPresence() { return &presence; }
  } _extraData;
  bool HasType(ExtraDataType) { return false; }
  int GetLock() { return 0; }
};
struct BSWriteLockGuard { explicit BSWriteLockGuard(int) {} };
template<class T> T* malloc() { return static_cast<T*>(::operator new(sizeof(T))); }
struct ExtraHealth : BSExtraData { float health; explicit ExtraHealth(float value) : health(value) {} };
struct EnchantmentItem {};
struct AlchemyItem {};
struct ExtraEnchantment : BSExtraData { ExtraEnchantment(EnchantmentItem*, int, bool) {} };
struct ExtraCharge : BSExtraData { float charge = 0; };
struct ExtraTextDisplayData : BSExtraData { std::string name; explicit ExtraTextDisplayData(const char* value) : name(value) {} };
struct ExtraSoul : BSExtraData { explicit ExtraSoul(SOUL_LEVEL) {} };
struct ExtraPoison : BSExtraData { ExtraPoison(AlchemyItem*, int) {} };
struct TESForm {
  uint32_t id; FormType formType;
  static inline std::map<uint32_t, TESForm*> forms;
  TESForm(uint32_t id_, FormType type) : id(id_), formType(type) { forms[id] = this; }
  virtual ~TESForm() = default;
  uint32_t GetFormID() const { return id; }
  template<class T> T* As() { return dynamic_cast<T*>(this); }
  template<class T = TESForm> static T* LookupByID(uint32_t id) { return dynamic_cast<T*>(forms[id]); }
};
struct TESBoundObject : TESForm { using TESForm::TESForm; };
struct TESObjectARMO : TESBoundObject {
  bool shield;
  TESObjectARMO(uint32_t id, bool shield_ = false) : TESBoundObject(id, FormType::Armor), shield(shield_) {}
  bool IsShield() { return shield; }
};
struct TESObjectLIGH : TESBoundObject {
  bool carryable;
  TESObjectLIGH(uint32_t id, bool carried) : TESBoundObject(id, FormType::Light), carryable(carried) {}
  bool CanBeCarried() { return carryable; }
};
struct BGSEquipSlot : TESForm { explicit BGSEquipSlot(uint32_t id) : TESForm(id, FormType::Misc) {} };
struct TESObjectREFR : TESForm {
  std::map<uint32_t, int> bag;
  std::map<uint32_t, ExtraDataList*> extras;
  std::map<uint32_t, BGSEquipSlot*> worn;
  TESObjectREFR(uint32_t id) : TESForm(id, FormType::ActorCharacter) {}
  void AddObjectToContainer(TESBoundObject* item, ExtraDataList* extra, int count, void*) { bag[item->id] += count; extras[item->id] = extra; }
  void RemoveItem(TESBoundObject* item, int count, ITEM_REMOVE_REASON, ExtraDataList*, void*) { bag[item->id] -= count; if(bag[item->id] <= 0){worn.erase(item->id);extras.erase(item->id);} }
};
struct Actor : TESObjectREFR { using TESObjectREFR::TESObjectREFR; };
struct UI { bool paused = false; static UI* GetSingleton() { static UI ui; return &ui; } bool GameIsPaused() { return paused; } };
struct BGSDefaultObjectManager { static void* GetSingleton() { return nullptr; } };
struct ActorEquipManager {
  static ActorEquipManager* GetSingleton() { static ActorEquipManager manager; return &manager; }
  void EquipObject(Actor* actor, TESBoundObject* item, ExtraDataList* extra, int, BGSEquipSlot* slot) {
    assert(actor->bag[item->id] > 0); // Must execute after its addition.
    assert(extra == actor->extras[item->id]); // Must equip the exact added instance.
    actor->worn[item->id] = slot;
  }
  void UnequipObject(Actor* actor, TESBoundObject* item, ExtraDataList* extra, int, BGSEquipSlot*) { assert(!extra || actor->extras.count(item->id)); actor->worn.erase(item->id); }
};
}
struct Queue {
  Viet::TaskQueue<> native;
  int pending = 0;
  template<class T> void AddTask(T fn) { ++pending; native.AddTask([this, fn](const Viet::Void& state){ --pending; fn(state); }); }
  void flush() { native.Update({}); }
} queue;
struct Requirements { Queue* gameThrQ = &queue; } g_nativeCallRequirements;
namespace TESModPlatform {
bool g_worn = false, g_wornLeft = false;
RE::ExtraDataList* CreateExtraDataList() { return new RE::ExtraDataList; }
void AddItemEx(IVM*, StackID, RE::StaticFunctionTag*, RE::TESObjectREFR*, RE::TESForm*, int32_t, float, RE::EnchantmentItem*, int32_t, bool, float, FixedString, int32_t, RE::AlchemyItem*, int32_t);
}
#include "WornInventoryUnderTest.inc"
void add(RE::Actor& actor, RE::TESForm& item, bool worn, bool left = false, const char* name = "", int count = 1, float health = 1) {
  TESModPlatform::g_worn = worn; TESModPlatform::g_wornLeft = left;
  TESModPlatform::AddItemEx(nullptr, 0, nullptr, &actor, &item, count, health, nullptr, 0, false, 0, {name}, 0, nullptr, 0);
}
int main() {
  RE::BGSEquipSlot right(0x13f42), left(0x13f43), both(0x13f45);
  RE::Actor actor(0x14);
  RE::TESObjectARMO shirt(1), boots(2), shield(3, true), spare(4);
  RE::TESBoundObject sword(5, RE::FormType::Weapon), arrows(6, RE::FormType::Ammo);
  RE::TESObjectLIGH torch(7, true), scenery(8, false);
  add(actor, shirt, true, false, "My shirt", 1, 1.5f); add(actor, boots, true); add(actor, spare, false);
  assert(actor.bag.empty() && actor.worn.empty()); queue.flush();
  assert(actor.bag[1] == 1 && actor.bag[2] == 1 && actor.bag[4] == 1);
  assert(actor.worn.count(1) && actor.worn.count(2));
  assert(!actor.worn.count(4));
  assert(actor.worn[1] == nullptr && actor.worn[2] == nullptr); // Biped gear has no hand slot.
  auto name = static_cast<RE::ExtraTextDisplayData*>(actor.extras[1]->_extraData.value);
  assert(name->name == "My shirt");
  auto health = static_cast<RE::ExtraHealth*>(name->next);
  assert(health->health == 1.5f);
  add(actor, shield, false, true); add(actor, sword, false, true); add(actor, arrows, true, false, "", 10); add(actor, torch, false, true);
  queue.flush();
  assert(actor.worn[3] == &right); // Preserve shield deadlock avoidance.
  assert(actor.worn[5] == &left && actor.worn[6] == nullptr && actor.worn[7] == &left);
  assert(actor.bag[6] == 10);
  add(actor, scenery, true); queue.flush(); assert(!actor.worn.count(8));
  RE::UI::GetSingleton()->paused = true;
  add(actor, spare, true); assert(queue.pending == 0);
  RE::UI::GetSingleton()->paused = false;
  TESModPlatform::AddItemEx(nullptr, 0, nullptr, &actor, &spare, 1, 1, nullptr, 0, false, 0, {""}, 0, nullptr, 0);
  queue.flush(); assert(!actor.worn.count(4)); // Rejected calls cannot leak worn state.
  add(actor, shirt, true, false, "My shirt", -1, 1.5f); queue.flush();
  assert(actor.bag[1] == 0 && !actor.worn.count(1)); // No post-removal armor extra use.
  RE::UI::GetSingleton()->paused = true;
  add(actor, shirt, true);
  auto rejected = CreateInventoryQueueFence(queue.native);
  assert(!rejected()); queue.flush(); assert(rejected() && actor.bag[1] == 0);
  RE::UI::GetSingleton()->paused = false;
  add(actor, shirt, true);
  auto accepted = CreateInventoryQueueFence(queue.native);
  assert(!accepted() && actor.bag[1] == 0); queue.flush();
  assert(accepted() && actor.bag[1] == 1 && actor.worn.count(1));
  auto cancelled = CreateInventoryQueueFence(queue.native);
  queue.native.Clear(); queue.flush(); assert(!cancelled());
  std::function<bool()> older, newer;
  bool sawGap = false;
  queue.native.AddTask([&](const Viet::Void&) {
    newer = CreateInventoryQueueFence(queue.native);
    queue.native.AddTask([&](const Viet::Void&) {sawGap = newer() && !older();});
    throw std::runtime_error("native failure");
  });
  older = CreateInventoryQueueFence(queue.native);
  try {queue.flush(); assert(false);} catch (const std::runtime_error&) {}
  assert(!older() && !newer()); queue.flush();
  assert(sawGap && older() && newer()); // A later fence never acknowledges the older gap.
  std::cout << "PASS native worn clothing, exact extras, queue order, hands, ammunition and paused-call isolation\n";
}
