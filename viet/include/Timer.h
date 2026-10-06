#pragma once
#include "Promise.h"
#include <chrono>
#include <functional>

namespace Viet {

class Timer
{
public:
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  Timer();

  template <typename T>
  Promise<Void> SetTimer(T&& duration, uint32_t* outTimerId)
  {
    auto endTime = Now() + duration;
    return Set(endTime, outTimerId);
  };

  [[maybe_unused]] bool RemoveTimer(uint32_t timerId);
  void TickTimers();

  // The time timers are set from and compared with: the system clock, unless
  // a test gives a clock it moves itself, so the test decides when a timer is
  // due and not how fast the machine is (Thornswood #2000). An empty clock is
  // the system clock again.
  void SetClock(Clock clock);

private:
  std::chrono::system_clock::time_point Now() const;
  Promise<Void> Set(const std::chrono::system_clock::time_point& endTime,
                    uint32_t* outTimerId);

private:
  struct Impl;
  std::shared_ptr<Impl> pImpl;
};

}
