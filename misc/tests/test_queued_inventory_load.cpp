#include "InventoryLoadEpoch.h"
#include "TaskQueue.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
namespace RE {
enum class FormType {Armor, Ammo};
enum class ExtraDataType {kHealth,kEnchantment,kCharge,kTextDisplayData,kSoul,kPoison};
enum class SOUL_LEVEL {None};
enum class ITEM_REMOVE_REASON {kRemove};
struct ExtraDataList {};
struct ExtraHealth {explicit ExtraHealth(float) {}};
struct ExtraEnchantment {ExtraEnchantment(void*,float,bool) {}};
struct ExtraCharge {float charge;};
struct ExtraTextDisplayData {explicit ExtraTextDisplayData(const char*) {}};
struct ExtraSoul {explicit ExtraSoul(SOUL_LEVEL) {}};
struct ExtraPoison {ExtraPoison(void*,int) {}};
inline std::vector<void*> allocations;
template<class T> T* malloc() {auto p=std::malloc(sizeof(T));allocations.push_back(p);return static_cast<T*>(p);}
struct TESObjectREFR {
 int count=1;
 void AddObjectToContainer(void*,ExtraDataList*,int added,void*) {count+=added;}
 void RemoveItem(void*,int removed,ITEM_REMOVE_REASON,ExtraDataList*,void*) {count-=removed;}
};
inline TESObjectREFR actor;
struct TESForm {
 FormType formType=FormType::Armor;
 template<class T> static T* LookupByID(unsigned) {return &actor;}
};
}
int listAllocations=0;
RE::ExtraDataList* CreateExtraDataList() {++listAllocations;return RE::malloc<RE::ExtraDataList>();}
Viet::TaskQueue<Viet::Void> queue;
struct {Viet::TaskQueue<Viet::Void>* gameThrQ=&queue;} g_nativeCallRequirements;
void QueueActualAdd(int countDelta) {
 const auto inventoryEpoch=InventoryLoadEpoch::Capture();
 auto containerRefr=&RE::actor;
 const unsigned refrId=0x14;
 RE::TESForm form; auto item=&form; auto boundObject=item;
 auto queuedExtra=std::make_shared<RE::ExtraDataList*>(nullptr);
 const float health=2,chargePercent=10,maxCharge=100;
 int placeholder=0;void* enchantment=&placeholder;void* poison=&placeholder;
 const bool removeEnchantmentOnUnequip=false;
 const int soul=1,poisonCount=1;
 const std::string textDisplayData="Saved metadata";
 auto addExtra=[](void*,uint32_t,void*) {};
 // Use the actual production add/remove lambda, not a second implementation.
 // Its item pointer must outlive execution as it does in the engine registry.
 static RE::TESForm persisted;
 item=&persisted;boundObject=item;
#include "InitialQueuedInventoryUnderTest.inc"
}
int main() {
 QueueActualAdd(1);
 QueueActualAdd(-1);
 InventoryLoadEpoch::Advance();
 queue.Update(Viet::Void());
 assert(RE::actor.count==1 && listAllocations==0 && RE::allocations.empty());
 QueueActualAdd(1);
 queue.Update(Viet::Void());
 assert(RE::actor.count==2 && listAllocations==1 && RE::allocations.size()==7);
 for(auto ptr:RE::allocations) std::free(ptr);
}
