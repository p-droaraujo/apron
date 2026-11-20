// An agent's terminal inside a pane of Apron's screen. libghostty-vt (D5)
// parses what the program writes and keeps the terminal's state; this class
// copies its render state into Apron's cell grid.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "tui/screen.hpp"

namespace apron::tui {

class Vt {
 public:
  Vt(int cols = 80, int rows = 24);
  ~Vt();
  Vt(const Vt&) = delete;
  Vt& operator=(const Vt&) = delete;

  void resize(int cols, int rows);
  int cols() const { return cols_; }
  int rows() const { return rows_; }

  // Bytes from the program. Answers to its questions (cursor position,
  // device attributes) collect for the caller to send back.
  void feed(std::string_view bytes);
  std::string take_replies();

  Cell cell(int x, int y) const;
  int cursor_x() const;
  int cursor_y() const;
  bool cursor_visible() const;
  // The program asked for application cursor keys: arrows go as ESC O A.
  bool app_cursor() const;

  // Draws the screen into `sc` at (x, y), clipped to w × h.
  void draw(Screen& sc, int x, int y, int w, int h, bool show_cursor) const;

 private:
  struct Impl;
  Impl* impl_;
  int cols_, rows_;
  void sync() const;  // copies libghostty-vt's render state when it changed
};

}  // namespace apron::tui
