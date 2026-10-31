#pragma once

#include <mutex>

// THORNSWOOD (Thornswood #2384). Who owns the game's Cursor Menu while the
// browser holds focus, and which show or hide is still waiting in the game's
// UI message queue.
//
// browser.setFocused only queues a kShow or kHide (UIMessageQueue::AddMessage);
// the game opens or closes the Cursor Menu on a later UI pass. The old code
// read IsMenuOpen at the moment it queued and treated that as the truth, so a
// lower and raise inside one frame could queue a hide on top of a show the
// game had not handled yet, and a raise right after a lower saw the menu as
// still open and queued no show at all. This keeps what has been asked for
// until the game says it happened (MenuOpenCloseEvent for the Cursor Menu).
//
// It also answers the one real drop: a Cursor Menu that closes while focus is
// still up and no hide of ours is waiting was closed by someone else (a game
// menu that uses the cursor, the console for one, closing). Focus is still
// up, so the cursor is shown again, once, from the event itself. No timer.
//
// Pure logic, no game types, so misc/tests/node/test_cursor_focus_native.cpp
// can build it on its own.
class CursorFocus
{
public:
  enum class Msg
  {
    None,
    Show,
    Hide
  };

  // Focus is being changed to `focus`. `menuOpen` is IsMenuOpen(Cursor Menu)
  // right now. Returns the message to queue, if any.
  Msg OnFocus(bool focus, bool menuOpen)
  {
    std::lock_guard<std::mutex> l(m_);
    if (focus) {
      if (pending_ == Msg::Show) {
        return Msg::None; // a show of ours is already on its way
      }
      if (pending_ == Msg::Hide) {
        // Our hide has not landed yet; what is open now is about to close.
        pending_ = Msg::Show;
        ours_ = true;
        return Msg::Show;
      }
      if (ours_ && menuOpen) {
        return Msg::None;
      }
      if (!menuOpen) {
        pending_ = Msg::Show;
        ours_ = true;
        return Msg::Show;
      }
      return Msg::None; // somebody else's cursor is up; not ours to hide
    }
    if (!ours_) {
      return Msg::None;
    }
    ours_ = false;
    if (pending_ == Msg::Hide) {
      return Msg::None;
    }
    pending_ = Msg::Hide;
    return Msg::Hide;
  }

  // The game opened or closed the Cursor Menu. `focus` is the browser's
  // focus now. Returns the message to queue, if any.
  Msg OnCursorMenu(bool opening, bool focus)
  {
    std::lock_guard<std::mutex> l(m_);
    if (opening) {
      if (pending_ == Msg::Show) {
        pending_ = Msg::None;
      }
      return Msg::None;
    }
    if (pending_ == Msg::Hide) {
      pending_ = Msg::None; // our own hide landed
      return Msg::None;
    }
    if (pending_ == Msg::Show) {
      // A show of ours is still queued behind this close; it reopens it.
      return Msg::None;
    }
    if (!focus) {
      ours_ = false;
      return Msg::None;
    }
    // Closed by someone else while the page still holds focus.
    pending_ = Msg::Show;
    ours_ = true;
    return Msg::Show;
  }

  bool Ours() const
  {
    std::lock_guard<std::mutex> l(m_);
    return ours_;
  }

private:
  mutable std::mutex m_;
  Msg pending_ = Msg::None;
  bool ours_ = false;
};
