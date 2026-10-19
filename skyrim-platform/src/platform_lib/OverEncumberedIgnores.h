#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>

/*
  THORNSWOOD PATCH. Characters the game does not hold to their carry weight
  (Thornswood #1385, Pack Mule: "Being overloaded does not slow you").

  Being overloaded has one effect in Thornswood: the game makes that
  character walk, with no running and no sprinting, on the machine that runs
  it. The server reads carry weight nowhere, and no game setting changes the
  walk. The game decides "overloaded" in Actor::IsOverEncumbered, which
  weighs what the character carries against its CarryWeight. For a character
  on this list, the hook in FridaHooks.cpp makes that answer no, so the game
  never slows it. The inventory still shows the real carry weight, and
  Burden's points still count towards it.

  skymp5-client reads this list too, so the walk it sends for such a
  character (movementGet.ts) is the one the game plays.

  Nothing here knows the game, which is what lets unit\platform_lib_tests
  test it.
*/
class OverEncumberedIgnores
{
public:
  static OverEncumberedIgnores& GetSingleton();

  // ignored=false takes the form off the list.
  void Set(uint32_t formId, bool ignored);

  // True when the game must not hold this form to its carry weight.
  bool Has(uint32_t formId) const;

  size_t Size() const;
  void Clear();

private:
  mutable std::mutex m;
  std::set<uint32_t> formIds;
  // Read without the lock on every call of the game's check, so an empty
  // list costs the hook no lock at all.
  std::atomic<size_t> count{ 0 };
};
