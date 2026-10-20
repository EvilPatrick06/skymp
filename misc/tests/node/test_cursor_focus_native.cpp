// Thornswood #2384. CursorFocus, the Cursor Menu bookkeeping behind
// browser.setFocused (BrowserApiTilted.cpp) and the Cursor Menu close
// handling (EventHandler.cpp), over a stand-in for the game's UI queue.
#include <cassert>
#include <deque>
#include <iostream>
#include "CursorFocus.h"

using M = CursorFocus::Msg;

struct Game
{
  CursorFocus cf;
  std::deque<M> queue;
  bool open = false;
  bool focus = false;
  int shows = 0, hides = 0;

  void Push(M m)
  {
    if (m == M::Show) { queue.push_back(m); ++shows; }
    if (m == M::Hide) { queue.push_back(m); ++hides; }
  }
  void SetFocused(bool f)
  {
    if (f == focus) return;
    focus = f;
    Push(cf.OnFocus(f, open));
  }
  // One game UI pass: every queued message is handled, and each real open
  // or close raises the event.
  void Pass()
  {
    while (!queue.empty()) {
      M m = queue.front();
      queue.pop_front();
      if (m == M::Show && !open) { open = true; Push(cf.OnCursorMenu(true, focus)); }
      if (m == M::Hide && open) { open = false; Push(cf.OnCursorMenu(false, focus)); }
    }
  }
  // Something else (the console closing) takes the cursor down.
  void OtherCloses()
  {
    if (!open) return;
    open = false;
    Push(cf.OnCursorMenu(false, focus));
  }
};

int main()
{
  { // A plain claim: one show, open after the next pass, nothing more.
    Game g;
    g.SetFocused(true);
    assert(!g.open && g.queue.size() == 1);
    g.Pass();
    assert(g.open && g.shows == 1 && g.hides == 0);
    g.SetFocused(false);
    g.Pass();
    assert(!g.open && g.hides == 1);
  }
  { // Raise again before the first show is handled: no second show.
    Game g;
    g.SetFocused(true);
    g.SetFocused(false);
    g.SetFocused(true);
    g.Pass();
    assert(g.open && g.focus);
  }
  { // Lower then raise on an open cursor, same frame: ends open.
    Game g;
    g.SetFocused(true);
    g.Pass();
    g.SetFocused(false);
    g.SetFocused(true);
    assert(g.open); // the old code queued no show here: stale IsMenuOpen
    g.Pass();
    assert(g.open && g.focus);
  }
  { // Something else closes the cursor while focus is up: shown again once.
    Game g;
    g.SetFocused(true);
    g.Pass();
    g.OtherCloses();
    g.Pass();
    assert(g.open && g.shows == 2);
  }
  { // The same close with focus down does nothing.
    Game g;
    g.SetFocused(true);
    g.Pass();
    g.SetFocused(false);
    g.Pass();
    g.OtherCloses();
    g.Pass();
    assert(!g.open && g.shows == 1);
  }
  { // A cursor somebody else opened is not ours to hide; when it closes
    // under our focus, ours comes up.
    Game g;
    g.open = true;
    g.SetFocused(true);
    assert(g.queue.empty());
    g.SetFocused(false);
    assert(g.queue.empty() && g.open);
    g.SetFocused(true);
    g.OtherCloses();
    g.Pass();
    assert(g.open && g.shows == 1);
    g.SetFocused(false);
    g.Pass();
    assert(!g.open);
  }
  std::cout << "cursor focus: 6 cases passed" << std::endl;
  return 0;
}
