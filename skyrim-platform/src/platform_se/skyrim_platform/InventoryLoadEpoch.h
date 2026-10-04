#pragma once
#include <atomic>
#include <cstdint>

// The player reference ID is reused by a synthetic load. Queued inventory
// writes from the previous world must not resolve it as the new character.
namespace InventoryLoadEpoch {
inline std::atomic<uint64_t> current{0};
inline uint64_t Capture() { return current.load(std::memory_order_acquire); }
inline bool IsCurrent(uint64_t captured) { return Capture() == captured; }
inline void Advance() { current.fetch_add(1, std::memory_order_acq_rel); }
}
