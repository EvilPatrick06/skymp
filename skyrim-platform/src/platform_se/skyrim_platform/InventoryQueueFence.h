#pragma once

#include "TaskQueue.h"
#include <atomic>
#include <functional>
#include <memory>

// Completion of this particular queue position, not a global high watermark:
// TaskQueue can requeue a batch after newer tasks when a task throws.
inline std::function<bool()> CreateInventoryQueueFence(
  Viet::TaskQueue<Viet::Void>& queue)
{
  auto completed = std::make_shared<std::atomic<bool>>(false);
  queue.AddTask([completed](const Viet::Void&) {
    completed->store(true, std::memory_order_release);
  });
  return [completed] {
    return completed->load(std::memory_order_acquire);
  };
}
