#include <catch2/catch_all.hpp>

#include "PageHeldKeys.h"

#include <cstdint>
#include <vector>

// THORNSWOOD PATCH (Thornswood #1920): a key the page was told is down comes
// up in the page, and its character is repeated into the page only while the
// page has the keyboard. Virtual key codes: J 0x4A, A 0x41, Backspace 0x08,
// Escape 0x1B.
//
// The pretend listener below does what MyInputListener does with the
// answers: hands the page the change when Change says so, and on every read
// of the keyboard hands the page the character of every key Repeats names.
// A character repeated after the key came up is the fault.

namespace {
constexpr uint32_t kJ = 0x4A;
constexpr uint32_t kA = 0x41;
constexpr uint32_t kBack = 0x08;
constexpr uint32_t kEscape = 0x1B;
constexpr int64_t kDelay = 500;

struct Page
{
  std::vector<uint32_t> downs, ups, repeats;
};

void Key(PageHeldKeys& k, Page& page, uint32_t code, bool down, bool focused,
         int64_t now)
{
  if (k.Change(code, down, focused, now))
    (down ? page.downs : page.ups).push_back(code);
}

// One read of the keyboard.
void Read(const PageHeldKeys& k, Page& page, bool focused, int64_t now)
{
  for (uint32_t i = 0; i < 256; ++i)
    if (k.Repeats(i, focused, now, kDelay))
      page.repeats.push_back(i);
}
}

TEST_CASE("A tap while the page has the keyboard goes to the page and does "
          "not repeat",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  Page page;
  Key(k, page, kA, true, true, 1000);
  Read(k, page, true, 1100);
  Key(k, page, kA, false, true, 1150);
  Read(k, page, true, 5000);
  REQUIRE(page.downs == std::vector<uint32_t>{ kA });
  REQUIRE(page.ups == std::vector<uint32_t>{ kA });
  REQUIRE(page.repeats.empty());
  REQUIRE(k.HeldCount() == 0);
}

TEST_CASE("A key held in the page repeats after half a second, until it is "
          "let go",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  Page page;
  Key(k, page, kBack, true, true, 1000);
  Read(k, page, true, 1400);
  REQUIRE(page.repeats.empty());
  Read(k, page, true, 1600);
  Read(k, page, true, 1700);
  REQUIRE(page.repeats == std::vector<uint32_t>{ kBack, kBack });
  Key(k, page, kBack, false, true, 1750);
  Read(k, page, true, 1800);
  REQUIRE(page.repeats.size() == 2);
}

TEST_CASE("J let go after the journal gives the keyboard back comes up in "
          "the page and never repeats",
          "[PageHeldKeys]")
{
  // The journal is open and has the keyboard. J goes down and the page
  // closes the journal, which gives the keyboard back a moment later; the
  // tap is let go after that.
  PageHeldKeys k;
  Page page;
  Key(k, page, kJ, true, true, 1000);
  Read(k, page, false, 1050);
  Key(k, page, kJ, false, false, 1120);
  REQUIRE(page.ups == std::vector<uint32_t>{ kJ });
  REQUIRE_FALSE(k.PageHolds(kJ));

  // A second later the chat box opens and has the keyboard. Nothing types
  // itself into it.
  Read(k, page, false, 2000);
  Read(k, page, true, 2200);
  Read(k, page, true, 9000);
  REQUIRE(page.repeats.empty());
}

TEST_CASE("Nothing repeats into the page while it does not have the keyboard",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  Page page;
  Key(k, page, kEscape, true, true, 1000);
  // The panel closes on Escape and Escape is held on.
  Read(k, page, false, 1600);
  Read(k, page, false, 3000);
  REQUIRE(page.repeats.empty());
  REQUIRE(k.PageHolds(kEscape));
  Key(k, page, kEscape, false, false, 3100);
  REQUIRE(page.ups == std::vector<uint32_t>{ kEscape });
  Read(k, page, true, 4000);
  REQUIRE(page.repeats.empty());
}

TEST_CASE("Keys pressed while the game has the keyboard reach the page not "
          "at all",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  Page page;
  Key(k, page, kA, true, false, 1000);
  Key(k, page, kA, false, false, 1100);
  Key(k, page, kJ, true, false, 1200);
  Read(k, page, false, 2000);
  Read(k, page, true, 2000);
  REQUIRE(page.downs.empty());
  REQUIRE(page.ups.empty());
  REQUIRE(page.repeats.empty());
  REQUIRE(k.HeldCount() == 0);
}

TEST_CASE("A key up crossing the change of focus goes to the page once, not "
          "again",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  Page page;
  Key(k, page, kA, true, true, 1000);
  Key(k, page, kA, false, false, 1100);
  // Pressed and let go again while the game has the keyboard.
  Key(k, page, kA, true, false, 1200);
  Key(k, page, kA, false, false, 1300);
  REQUIRE(page.ups == std::vector<uint32_t>{ kA });
}

TEST_CASE("A key that went down in the game and comes up in the page is "
          "handed to the page as before",
          "[PageHeldKeys]")
{
  // J opens the journal: down while the game has the keyboard, up after the
  // journal has it. The page has always been handed that key up; it starts
  // nothing repeating.
  PageHeldKeys k;
  Page page;
  Key(k, page, kJ, true, false, 1000);
  Key(k, page, kJ, false, true, 1100);
  REQUIRE(page.downs.empty());
  REQUIRE(page.ups == std::vector<uint32_t>{ kJ });
  Read(k, page, true, 3000);
  REQUIRE(page.repeats.empty());
}

TEST_CASE("Codes past the table go to the page only while it has the "
          "keyboard",
          "[PageHeldKeys]")
{
  PageHeldKeys k;
  REQUIRE(k.Change(256, true, true, 1000));
  REQUIRE(k.Change(256, false, true, 1000));
  REQUIRE_FALSE(k.Change(256, false, false, 1000));
  REQUIRE_FALSE(k.Repeats(256, true, 9000, kDelay));
  REQUIRE(k.HeldCount() == 0);
}
