#include <catch2/catch_all.hpp>

#include "HeldKeyRelease.h"

#include <array>
#include <cstdint>
#include <vector>

// THORNSWOOD PATCH (Thornswood #898): key ups for keys the game holds reach
// it even while a panel has the keyboard. DirectX scan codes: Left Shift 42,
// P 25, K 37, Tab 15, E 18.
//
// The pretend game below keeps a key table the way Skyrim's keyboard device
// does: a key is held from the event that says it went down to the event that
// says it came up. A key held at the end of a scenario is a key the game
// repeats every frame for the rest of the session, which is the fault.

namespace {
constexpr uint32_t kShift = 42;
constexpr uint32_t kP = 25;
constexpr uint32_t kK = 37;
constexpr uint32_t kTab = 15;
constexpr uint32_t kE = 18;

// DIDEVICEOBJECTDATA on x64: dwOfs, dwData, dwTimeStamp, dwSequence, uAppData.
struct Dx8Event
{
  uint32_t dwOfs;
  uint32_t dwData;
  uint32_t dwTimeStamp;
  uint32_t dwSequence;
  uint64_t uAppData;
};
static_assert(sizeof(Dx8Event) == 24);

// DIDEVICEOBJECTDATA_DX3: dwOfs, dwData, dwTimeStamp, dwSequence.
struct Dx3Event
{
  uint32_t dwOfs;
  uint32_t dwData;
  uint32_t dwTimeStamp;
  uint32_t dwSequence;
};
static_assert(sizeof(Dx3Event) == 16);

template <class E>
E Down(uint32_t code)
{
  E e{};
  e.dwOfs = code;
  e.dwData = 0x80;
  return e;
}

template <class E>
E Up(uint32_t code)
{
  E e{};
  e.dwOfs = code;
  e.dwData = 0;
  return e;
}

struct Game
{
  std::array<bool, 256> held{};
  std::vector<uint32_t> heard; // every code the game was handed

  size_t HeldCount() const
  {
    size_t n = 0;
    for (bool h : held)
      n += h ? 1 : 0;
    return n;
  }
};

// One GetDeviceData read: the device's buffered events go through the filter
// and whatever is left is what the game gets.
template <class E>
size_t Read(HeldKeyRelease& filter, Game& game, std::vector<E> events,
            bool listening)
{
  const size_t kept =
    filter.Filter(reinterpret_cast<uint8_t*>(events.data()), sizeof(E),
                  events.size(), listening);
  for (size_t i = 0; i < kept; ++i) {
    game.heard.push_back(events[i].dwOfs);
    game.held[events[i].dwOfs & 0xFF] = (events[i].dwData & 0x80) != 0;
  }
  return kept;
}
}

TEST_CASE("While the game listens every event goes through",
          "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  REQUIRE(Read<Dx8Event>(f, g, { Down<Dx8Event>(kShift), Down<Dx8Event>(kP) },
                         true) == 2);
  REQUIRE(g.held[kShift]);
  REQUIRE(g.held[kP]);
  REQUIRE(f.GameHolds(kShift));
  REQUIRE(f.GameHolds(kP));
  REQUIRE(Read<Dx8Event>(f, g, { Up<Dx8Event>(kP), Up<Dx8Event>(kShift) },
                         true) == 2);
  REQUIRE(g.HeldCount() == 0);
  REQUIRE(f.HeldCount() == 0);
}

TEST_CASE("Shift and P let go after a panel takes the keyboard come up in "
          "the game",
          "[HeldKeyRelease]")
{
  // Djinn, 24 September: the party window claimed focus while Shift and P
  // were down for the P in his name.
  HeldKeyRelease f;
  Game g;
  Read<Dx8Event>(f, g, { Down<Dx8Event>(kShift), Down<Dx8Event>(kP) }, true);
  REQUIRE(g.HeldCount() == 2);

  // Focus goes up. Nothing new arrives for a frame, then both keys come up.
  REQUIRE(Read<Dx8Event>(f, g, {}, false) == 0);
  REQUIRE(Read<Dx8Event>(f, g, { Up<Dx8Event>(kP), Up<Dx8Event>(kShift) },
                         false) == 2);

  REQUIRE_FALSE(g.held[kShift]);
  REQUIRE_FALSE(g.held[kP]);
  REQUIRE(g.HeldCount() == 0);
  REQUIRE(f.HeldCount() == 0);
}

TEST_CASE("Typing in a panel reaches the game not at all", "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  // K opens the skills panel; the panel claims focus while K is still down.
  Read<Dx8Event>(f, g, { Down<Dx8Event>(kK) }, true);
  REQUIRE(g.heard.size() == 1);

  // In the panel: K comes up, then a word is typed, Tab pressed and let go.
  const size_t kept = Read<Dx8Event>(
    f, g,
    { Up<Dx8Event>(kK), Down<Dx8Event>(kShift), Down<Dx8Event>(kP),
      Up<Dx8Event>(kP), Up<Dx8Event>(kShift), Down<Dx8Event>(kTab),
      Up<Dx8Event>(kTab) },
    false);

  // Only K's key up went through: no key down, and no key up for a key the
  // game never saw go down.
  REQUIRE(kept == 1);
  REQUIRE(g.heard.size() == 2);
  REQUIRE(g.heard[1] == kK);
  REQUIRE(g.HeldCount() == 0);
}

TEST_CASE("A key up is let through once, not again", "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  Read<Dx8Event>(f, g, { Down<Dx8Event>(kE) }, true);
  REQUIRE(Read<Dx8Event>(f, g, { Up<Dx8Event>(kE) }, false) == 1);
  // Pressed and let go again while the panel still has the keyboard.
  REQUIRE(Read<Dx8Event>(f, g, { Down<Dx8Event>(kE), Up<Dx8Event>(kE) },
                         false) == 0);
  REQUIRE(g.heard.size() == 2);
  REQUIRE_FALSE(g.held[kE]);
}

TEST_CASE("A key held right through the panel comes up when it is let go",
          "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  Read<Dx8Event>(f, g, { Down<Dx8Event>(kShift) }, true);
  REQUIRE(Read<Dx8Event>(f, g, {}, false) == 0);
  REQUIRE(g.held[kShift]);
  // Focus goes down again and only then is Shift let go.
  REQUIRE(Read<Dx8Event>(f, g, { Up<Dx8Event>(kShift) }, true) == 1);
  REQUIRE(g.HeldCount() == 0);
}

TEST_CASE("The key up is kept in order among withheld events",
          "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  Read<Dx8Event>(f, g, { Down<Dx8Event>(kShift), Down<Dx8Event>(kK) }, true);
  std::vector<Dx8Event> events = { Down<Dx8Event>(kTab), Up<Dx8Event>(kShift),
                                   Down<Dx8Event>(kP), Up<Dx8Event>(kK) };
  events[1].dwTimeStamp = 111;
  events[3].dwTimeStamp = 222;
  const size_t kept =
    f.Filter(reinterpret_cast<uint8_t*>(events.data()), sizeof(Dx8Event),
             events.size(), false);
  REQUIRE(kept == 2);
  // Packed at the front, whole events, in the order they happened.
  REQUIRE(events[0].dwOfs == kShift);
  REQUIRE(events[0].dwTimeStamp == 111);
  REQUIRE(events[1].dwOfs == kK);
  REQUIRE(events[1].dwTimeStamp == 222);
}

TEST_CASE("The shorter DX3 event form works the same", "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  Game g;
  Read<Dx3Event>(f, g, { Down<Dx3Event>(kShift), Down<Dx3Event>(kP) }, true);
  REQUIRE(Read<Dx3Event>(f, g,
                         { Down<Dx3Event>(kTab), Up<Dx3Event>(kP),
                           Up<Dx3Event>(kShift), Up<Dx3Event>(kTab) },
                         false) == 2);
  REQUIRE(g.HeldCount() == 0);
}

TEST_CASE("No buffer or a stride too short hands the game nothing new",
          "[HeldKeyRelease]")
{
  HeldKeyRelease f;
  REQUIRE(f.Filter(nullptr, sizeof(Dx8Event), 3, false) == 0);
  REQUIRE(f.Filter(nullptr, sizeof(Dx8Event), 3, true) == 3);
  uint8_t tiny[4] = {};
  REQUIRE(f.Filter(tiny, 4, 1, false) == 0);
}
