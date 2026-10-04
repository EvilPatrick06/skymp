#include "InventoryLoadEpoch.h"
#include "InventoryQueueFence.h"
#include <cassert>
#include <stdexcept>
int main() {
  Viet::TaskQueue<Viet::Void> queue;
  int shirts=1, boots=1, equips=0;
  const auto previous=InventoryLoadEpoch::Capture();
  queue.AddTask([&](const Viet::Void&) {
    if (InventoryLoadEpoch::IsCurrent(previous)) ++shirts;
  });
  queue.AddTask([&](const Viet::Void&) {
    if (InventoryLoadEpoch::IsCurrent(previous)) boots=0;
  });
  queue.AddTask([&](const Viet::Void&) {
    if (InventoryLoadEpoch::IsCurrent(previous)) ++equips;
  });
  auto finished=CreateInventoryQueueFence(queue);
  InventoryLoadEpoch::Advance(); // The new saved inventory is now loaded.
  assert(!finished());
  queue.Update(Viet::Void());
  assert(finished() && shirts==1 && boots==1 && equips==0);
  const auto loaded=InventoryLoadEpoch::Capture();
  queue.AddTask([&](const Viet::Void&) {
    if (InventoryLoadEpoch::IsCurrent(loaded)) ++shirts;
  });
  queue.Update(Viet::Void());
  assert(shirts==2); // Current-world inventory writes still run.
  queue.AddTask([](const Viet::Void&) {throw std::runtime_error("transient");});
  queue.AddTask([&](const Viet::Void&) {
    if (InventoryLoadEpoch::IsCurrent(loaded)) shirts=0;
  });
  auto requeued=CreateInventoryQueueFence(queue);
  try {queue.Update(Viet::Void());} catch (const std::runtime_error&) {}
  assert(!requeued());
  InventoryLoadEpoch::Advance();
  queue.Update(Viet::Void());
  assert(requeued() && shirts==2); // Requeued old batches cannot cross loads.
}
