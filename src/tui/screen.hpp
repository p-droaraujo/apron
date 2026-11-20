// A double-buffered cell grid: only what changed becomes escape sequences,
// and the whole frame goes inside synchronized output (mode 2026).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace apron::tui {

struct Color {
  uint32_t rgb = 0;
  bool set = false;  // false = the terminal's default color
  static Color none() { return {}; }
  static Color hex(uint32_t v) { return {v, true}; }
  bool operator==(const Color&) const = default;
};

struct Style {
  Color fg, bg;
  bool bold = false;
  bool operator==(const Style&) const = default;
};

struct Cell {
  std::string ch = " ";  // one UTF-8 code point
  Style style;
  bool operator==(const Cell&) const = default;
};

class Screen {
 public:
  void resize(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }

  void clear();
  // Writes text from (x, y), clipped at `max_w` (or the screen edge).
  // Returns the columns used.
  int put(int x, int y, std::string_view text, Style style, int max_w = -1);
  void fill(int x, int y, int w, std::string_view ch, Style style);
  const Cell& at(int x, int y) const { return back_[static_cast<size_t>(y * w_ + x)]; }
  // Copies another screen's cells here, with its top-left corner at (x, y).
  void blit(const Screen& src, int x, int y);

  // Produces the bytes that take the terminal from the last frame to this one.
  std::string flush();

 private:
  int w_ = 0, h_ = 0;
  std::vector<Cell> front_, back_;
  bool full_ = true;
};

// Columns a text takes (one code point = one column).
int text_width(std::string_view text);
// Cuts text to `w` columns, with an ellipsis when it cuts.
std::string fit(std::string_view text, int w);

}  // namespace apron::tui
