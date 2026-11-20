// The real terminal: raw mode, alternate screen, size and keys.
#pragma once

#include <string>
#include <vector>

namespace apron::tui {

enum class Key { None, Up, Down, Left, Right, Enter, Esc, Tab, BackTab, Backspace, Char, Resize, Click, WheelUp, WheelDown, Wake };

struct KeyEvent {
  Key key = Key::None;
  char ch = 0;
  std::string text;  // the bytes typed, so accented letters survive in text input
  int x = 0, y = 0;  // Click and wheel: the cell, from 0
  std::string raw;   // the bytes as the terminal sent them, for a program in a pane
};

class Terminal {
 public:
  Terminal();   // enters raw mode and the alternate screen
  ~Terminal();  // restores everything, also on exit by signal
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  int width() const { return w_; }
  int height() const { return h_; }
  bool ok() const { return ok_; }

  // Waits up to `timeout_ms` for a key or a resize; Key::Wake when `extra_fd`
  // (a session's connection) has data first.
  KeyEvent poll(int timeout_ms, const std::vector<int>& extra_fds = {});
  void write(const std::string& bytes);
  // Hands the terminal to something else (an attached session) and takes it back.
  void suspend();
  void resume();

 private:
  int w_ = 80, h_ = 24;
  bool ok_ = false;
  void query_size();
  KeyEvent read_key(int timeout_ms, const std::vector<int>& extra_fds);
  static KeyEvent parse(const char* buf, long n);
};

}  // namespace apron::tui
