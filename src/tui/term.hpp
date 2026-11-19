// The real terminal: raw mode, alternate screen, size and keys.
#pragma once

#include <string>

namespace apron::tui {

enum class Key { None, Up, Down, Left, Right, Enter, Esc, Tab, Backspace, Char, Resize };

struct KeyEvent {
  Key key = Key::None;
  char ch = 0;
  std::string text;  // the bytes typed, so accented letters survive in text input
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

  // Waits up to `timeout_ms` for a key or a resize.
  KeyEvent poll(int timeout_ms);
  void write(const std::string& bytes);
  // Hands the terminal to something else (an attached session) and takes it back.
  void suspend();
  void resume();

 private:
  int w_ = 80, h_ = 24;
  bool ok_ = false;
  void query_size();
};

}  // namespace apron::tui
