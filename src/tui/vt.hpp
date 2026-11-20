// A small terminal emulator: enough of xterm to show what coding agents
// draw (colors, cursor movement, erasing, scroll regions, the alternate
// screen) inside a pane of Apron's own screen.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tui/screen.hpp"

namespace apron::tui {

class Vt {
 public:
  Vt(int cols = 80, int rows = 24);
  void resize(int cols, int rows);
  int cols() const { return cols_; }
  int rows() const { return rows_; }

  // Bytes from the program. Answers to its questions (cursor position,
  // device attributes) collect in replies() for the caller to send back.
  void feed(std::string_view bytes);
  std::string take_replies();

  const Cell& cell(int x, int y) const { return grid()[static_cast<size_t>(y * cols_ + x)]; }
  int cursor_x() const { return cx_; }
  int cursor_y() const { return cy_; }
  bool cursor_visible() const { return cursor_on_; }
  // The program asked for application cursor keys: arrows go as ESC O A.
  bool app_cursor() const { return app_cursor_; }

  // Draws the screen into `sc` at (x, y), clipped to w × h.
  void draw(Screen& sc, int x, int y, int w, int h, bool show_cursor) const;

 private:
  int cols_, rows_;
  std::vector<Cell> main_, alt_;
  bool on_alt_ = false;
  int cx_ = 0, cy_ = 0;
  bool wrap_next_ = false;
  int top_ = 0, bottom_ = 0;  // scroll region, inclusive
  int saved_x_ = 0, saved_y_ = 0;
  Style saved_style_;
  Style style_;
  bool reverse_ = false;
  bool cursor_on_ = true, autowrap_ = true, app_cursor_ = false;
  std::string replies_;

  enum class State { Ground, Esc, EscInter, Csi, Osc, OscEsc, Str, StrEsc } state_ = State::Ground;
  std::string params_, utf8_;
  char prefix_ = 0, inter_ = 0;
  size_t utf8_need_ = 0;

  std::vector<Cell>& grid() { return on_alt_ ? alt_ : main_; }
  const std::vector<Cell>& grid() const { return on_alt_ ? alt_ : main_; }
  Cell blank() const;
  void put_char(const std::string& ch);
  void line_feed();
  void scroll_up(int n, int top, int bottom);
  void scroll_down(int n, int top, int bottom);
  void erase(int x0, int y0, int x1, int y1);  // inclusive, row-major
  void csi(char final);
  void sgr(const std::vector<int>& p);
  void set_mode(const std::vector<int>& p, bool on);
  void clamp_cursor();
};

}  // namespace apron::tui
