#include "tui/vt.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace apron::tui {

namespace {

constexpr uint32_t kDefaultFg = 0xe6e8eb;
constexpr uint32_t kDefaultBg = 0x0f1115;

// The 16 colors of the ANSI palette, close to what Ghostty shows by default.
constexpr uint32_t kAnsi[16] = {0x1d1f21, 0xcc6666, 0xb5bd68, 0xf0c674, 0x81a2be, 0xb294bb, 0x8abeb7, 0xc5c8c6,
                                0x666666, 0xd54e53, 0xb9ca4a, 0xe7c547, 0x7aa6da, 0xc397d8, 0x70c0b1, 0xeaeaea};

uint32_t color256(int n) {
  if (n < 16) return kAnsi[n];
  if (n < 232) {
    n -= 16;
    auto level = [](int v) { return v == 0 ? 0 : 55 + v * 40; };
    return static_cast<uint32_t>(level(n / 36) << 16 | level((n / 6) % 6) << 8 | level(n % 6));
  }
  int g = 8 + (n - 232) * 10;
  return static_cast<uint32_t>(g << 16 | g << 8 | g);
}

std::vector<int> parse_params(const std::string& s) {
  std::vector<int> out;
  int cur = -1;
  for (char c : s) {
    if (c >= '0' && c <= '9') cur = (cur < 0 ? 0 : cur) * 10 + (c - '0'), cur = std::min(cur, 65535);
    else if (c == ';' || c == ':') out.push_back(cur), cur = -1;
  }
  out.push_back(cur);
  return out;
}

int arg(const std::vector<int>& p, size_t i, int fallback) {
  return i < p.size() && p[i] > 0 ? p[i] : fallback;
}

}  // namespace

Vt::Vt(int cols, int rows) : cols_(0), rows_(0) { resize(cols, rows); }

Cell Vt::blank() const {
  Cell c;
  c.style.bg = style_.bg;  // erasing paints the current background, as xterm does
  return c;
}

void Vt::resize(int cols, int rows) {
  cols = std::max(1, cols);
  rows = std::max(1, rows);
  auto remap = [&](std::vector<Cell>& g) {
    std::vector<Cell> n(static_cast<size_t>(cols * rows));
    // Keep the bottom of the old screen: that is where the latest output is.
    int shift = std::max(0, std::min(rows_, cy_ + 1) - rows);
    for (int y = 0; y < std::min(rows, rows_); ++y)
      for (int x = 0; x < std::min(cols, cols_); ++x)
        if (y + shift < rows_) n[static_cast<size_t>(y * cols + x)] = g[static_cast<size_t>((y + shift) * cols_ + x)];
    g = std::move(n);
    return shift;
  };
  int shift = 0;
  if (cols_ == 0) {
    main_.assign(static_cast<size_t>(cols * rows), Cell{});
    alt_ = main_;
  } else {
    shift = remap(main_);
    remap(alt_);
  }
  cols_ = cols;
  rows_ = rows;
  cy_ -= shift;
  top_ = 0;
  bottom_ = rows_ - 1;
  clamp_cursor();
}

void Vt::clamp_cursor() {
  cx_ = std::clamp(cx_, 0, cols_ - 1);
  cy_ = std::clamp(cy_, 0, rows_ - 1);
}

std::string Vt::take_replies() {
  std::string r;
  r.swap(replies_);
  return r;
}

void Vt::scroll_up(int n, int top, int bottom) {
  auto& g = grid();
  n = std::min(n, bottom - top + 1);
  for (int y = top; y <= bottom - n; ++y)
    for (int x = 0; x < cols_; ++x) g[static_cast<size_t>(y * cols_ + x)] = g[static_cast<size_t>((y + n) * cols_ + x)];
  for (int y = bottom - n + 1; y <= bottom; ++y)
    for (int x = 0; x < cols_; ++x) g[static_cast<size_t>(y * cols_ + x)] = blank();
}

void Vt::scroll_down(int n, int top, int bottom) {
  auto& g = grid();
  n = std::min(n, bottom - top + 1);
  for (int y = bottom; y >= top + n; --y)
    for (int x = 0; x < cols_; ++x) g[static_cast<size_t>(y * cols_ + x)] = g[static_cast<size_t>((y - n) * cols_ + x)];
  for (int y = top; y < top + n; ++y)
    for (int x = 0; x < cols_; ++x) g[static_cast<size_t>(y * cols_ + x)] = blank();
}

void Vt::erase(int x0, int y0, int x1, int y1) {
  auto& g = grid();
  for (int y = y0; y <= y1; ++y)
    for (int x = (y == y0 ? x0 : 0); x <= (y == y1 ? x1 : cols_ - 1); ++x)
      if (x >= 0 && x < cols_ && y >= 0 && y < rows_) g[static_cast<size_t>(y * cols_ + x)] = blank();
}

void Vt::line_feed() {
  if (cy_ == bottom_) scroll_up(1, top_, bottom_);
  else if (cy_ < rows_ - 1) ++cy_;
}

void Vt::put_char(const std::string& ch) {
  if (wrap_next_ && autowrap_) {
    cx_ = 0;
    line_feed();
  }
  wrap_next_ = false;
  Style s = style_;
  if (reverse_) {
    Color fg = s.fg.set ? s.fg : Color::hex(kDefaultFg);
    Color bg = s.bg.set ? s.bg : Color::hex(kDefaultBg);
    s.fg = bg;
    s.bg = fg;
  }
  grid()[static_cast<size_t>(cy_ * cols_ + cx_)] = Cell{ch, s};
  if (cx_ == cols_ - 1) wrap_next_ = true;
  else ++cx_;
}

void Vt::feed(std::string_view bytes) {
  for (unsigned char c : bytes) {
    switch (state_) {
      case State::Ground:
        if (utf8_need_) {
          if ((c & 0xC0) == 0x80) {
            utf8_ += static_cast<char>(c);
            if (--utf8_need_ == 0) put_char(utf8_);
            continue;
          }
          utf8_need_ = 0;  // a broken sequence: drop it
        }
        if (c == 0x1b) state_ = State::Esc;
        else if (c == '\r') cx_ = 0, wrap_next_ = false;
        else if (c == '\n' || c == 0x0b || c == 0x0c) line_feed(), wrap_next_ = false;
        else if (c == '\b') cx_ = std::max(0, cx_ - 1), wrap_next_ = false;
        else if (c == '\t') cx_ = std::min(cols_ - 1, (cx_ / 8 + 1) * 8), wrap_next_ = false;
        else if (c < 0x20 || c == 0x7f) {
          // bell and other controls: nothing to draw
        } else if (c < 0x80) {
          put_char(std::string(1, static_cast<char>(c)));
        } else {
          utf8_.assign(1, static_cast<char>(c));
          utf8_need_ = (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 0;
        }
        break;
      case State::Esc:
        state_ = State::Ground;
        switch (c) {
          case '[': state_ = State::Csi, params_.clear(), prefix_ = 0, inter_ = 0; break;
          case ']': state_ = State::Osc; break;
          case 'P': case 'X': case '^': case '_': state_ = State::Str; break;
          case '7': saved_x_ = cx_, saved_y_ = cy_, saved_style_ = style_; break;
          case '8': cx_ = saved_x_, cy_ = saved_y_, style_ = saved_style_, clamp_cursor(); break;
          case 'D': line_feed(); break;
          case 'E': cx_ = 0, line_feed(); break;
          case 'M':
            if (cy_ == top_) scroll_down(1, top_, bottom_);
            else if (cy_ > 0) --cy_;
            break;
          case 'c': {
            int c0 = cols_, r0 = rows_;
            *this = Vt(c0, r0);
            break;
          }
          case '(': case ')': case '*': case '+': case '#': case '%': case ' ': state_ = State::EscInter; break;
          default: break;  // keypad modes and the rest: nothing to draw
        }
        break;
      case State::EscInter:
        state_ = State::Ground;  // charset and similar: one more byte, ignored
        break;
      case State::Csi:
        if (c >= 0x40 && c <= 0x7e) {
          state_ = State::Ground;
          csi(static_cast<char>(c));
        } else if (c == '?' || c == '>' || c == '<' || c == '=') {
          prefix_ = static_cast<char>(c);
        } else if (c >= 0x20 && c <= 0x2f) {
          inter_ = static_cast<char>(c);
        } else if (params_.size() < 64) {
          params_ += static_cast<char>(c);
        }
        break;
      case State::Osc:
        if (c == 0x07) state_ = State::Ground;
        else if (c == 0x1b) state_ = State::OscEsc;
        break;
      case State::OscEsc:
        state_ = c == '\\' ? State::Ground : State::Osc;
        break;
      case State::Str:
        if (c == 0x1b) state_ = State::StrEsc;
        break;
      case State::StrEsc:
        state_ = c == '\\' ? State::Ground : State::Str;
        break;
    }
  }
}

void Vt::set_mode(const std::vector<int>& p, bool on) {
  for (int m : p) {
    switch (m) {
      case 1: app_cursor_ = on; break;
      case 7: autowrap_ = on; break;
      case 25: cursor_on_ = on; break;
      case 47:
      case 1047:
      case 1049:
        if (on == on_alt_) break;
        if (on) {
          saved_x_ = cx_, saved_y_ = cy_;
          on_alt_ = true;
          for (auto& cell : alt_) cell = Cell{};
        } else {
          on_alt_ = false;
          cx_ = saved_x_, cy_ = saved_y_;
          clamp_cursor();
        }
        break;
      default: break;
    }
  }
}

void Vt::sgr(const std::vector<int>& p) {
  for (size_t i = 0; i < p.size(); ++i) {
    int n = p[i] < 0 ? 0 : p[i];
    if (n == 0) style_ = Style{}, reverse_ = false;
    else if (n == 1) style_.bold = true;
    else if (n == 22) style_.bold = false;
    else if (n == 7) reverse_ = true;
    else if (n == 27) reverse_ = false;
    else if (n >= 30 && n <= 37) style_.fg = Color::hex(kAnsi[n - 30]);
    else if (n >= 90 && n <= 97) style_.fg = Color::hex(kAnsi[n - 90 + 8]);
    else if (n >= 40 && n <= 47) style_.bg = Color::hex(kAnsi[n - 40]);
    else if (n >= 100 && n <= 107) style_.bg = Color::hex(kAnsi[n - 100 + 8]);
    else if (n == 39) style_.fg = Color::none();
    else if (n == 49) style_.bg = Color::none();
    else if (n == 38 || n == 48) {
      Color col;
      if (i + 2 < p.size() && p[i + 1] == 5) {
        col = Color::hex(color256(std::clamp(p[i + 2], 0, 255)));
        i += 2;
      } else if (i + 4 < p.size() && p[i + 1] == 2) {
        auto v = [&](size_t k) { return static_cast<uint32_t>(std::clamp(p[k], 0, 255)); };
        col = Color::hex(v(i + 2) << 16 | v(i + 3) << 8 | v(i + 4));
        i += 4;
      } else {
        break;
      }
      (n == 38 ? style_.fg : style_.bg) = col;
    }
  }
}

void Vt::csi(char f) {
  auto p = parse_params(params_);
  if (prefix_ == '?') {
    if (f == 'h') set_mode(p, true);
    else if (f == 'l') set_mode(p, false);
    return;  // DECRQM and kitty queries: no answer means "not supported"
  }
  if (prefix_ || inter_) {
    if (prefix_ == '>' && f == 'c') replies_ += "\x1b[>0;0;0c";
    return;
  }
  wrap_next_ = false;
  switch (f) {
    case 'A': cy_ = std::max(cy_ - arg(p, 0, 1), cy_ >= top_ ? top_ : 0); break;
    case 'B': cy_ = std::min(cy_ + arg(p, 0, 1), cy_ <= bottom_ ? bottom_ : rows_ - 1); break;
    case 'C': case 'a': cx_ = std::min(cols_ - 1, cx_ + arg(p, 0, 1)); break;
    case 'D': cx_ = std::max(0, cx_ - arg(p, 0, 1)); break;
    case 'E': cx_ = 0, cy_ = std::min(rows_ - 1, cy_ + arg(p, 0, 1)); break;
    case 'F': cx_ = 0, cy_ = std::max(0, cy_ - arg(p, 0, 1)); break;
    case 'G': case '`': cx_ = arg(p, 0, 1) - 1; break;
    case 'd': cy_ = arg(p, 0, 1) - 1; break;
    case 'H': case 'f': cy_ = arg(p, 0, 1) - 1, cx_ = arg(p, 1, 1) - 1; break;
    case 'J': {
      int m = p[0] < 0 ? 0 : p[0];
      if (m == 0) erase(cx_, cy_, cols_ - 1, rows_ - 1);
      else if (m == 1) erase(0, 0, cx_, cy_);
      else erase(0, 0, cols_ - 1, rows_ - 1);
      break;
    }
    case 'K': {
      int m = p[0] < 0 ? 0 : p[0];
      if (m == 0) erase(cx_, cy_, cols_ - 1, cy_);
      else if (m == 1) erase(0, cy_, cx_, cy_);
      else erase(0, cy_, cols_ - 1, cy_);
      break;
    }
    case 'L': if (cy_ >= top_ && cy_ <= bottom_) scroll_down(arg(p, 0, 1), cy_, bottom_); break;
    case 'M': if (cy_ >= top_ && cy_ <= bottom_) scroll_up(arg(p, 0, 1), cy_, bottom_); break;
    case 'S': scroll_up(arg(p, 0, 1), top_, bottom_); break;
    case 'T': scroll_down(arg(p, 0, 1), top_, bottom_); break;
    case 'P': case '@': {
      int n = std::min(arg(p, 0, 1), cols_ - cx_);
      auto& g = grid();
      Cell* row = &g[static_cast<size_t>(cy_ * cols_)];
      if (f == 'P') {
        for (int x = cx_; x < cols_; ++x) row[x] = x + n < cols_ ? row[x + n] : blank();
      } else {
        for (int x = cols_ - 1; x >= cx_; --x) row[x] = x - n >= cx_ ? row[x - n] : blank();
      }
      break;
    }
    case 'X': erase(cx_, cy_, std::min(cols_ - 1, cx_ + arg(p, 0, 1) - 1), cy_); break;
    case 'm': sgr(p); break;
    case 'r': {
      int t = arg(p, 0, 1) - 1, b = arg(p, 1, rows_) - 1;
      if (t < b && b < rows_) top_ = t, bottom_ = b;
      cx_ = 0, cy_ = 0;
      break;
    }
    case 's': saved_x_ = cx_, saved_y_ = cy_; break;
    case 'u': cx_ = saved_x_, cy_ = saved_y_; break;
    case 'n':
      if (p[0] == 6) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "\x1b[%d;%dR", cy_ + 1, cx_ + 1);
        replies_ += buf;
      } else if (p[0] == 5) {
        replies_ += "\x1b[0n";
      }
      break;
    case 'c': replies_ += "\x1b[?62;22c"; break;
    default: break;
  }
  clamp_cursor();
}

void Vt::draw(Screen& sc, int x, int y, int w, int h, bool show_cursor) const {
  // The bottom of the screen matters most when the pane is shorter.
  int y0 = std::max(0, std::min(rows_ - h, cy_ - h + 1));
  for (int row = 0; row < std::min(h, rows_); ++row)
    for (int col = 0; col < std::min(w, cols_); ++col) {
      const Cell& c = cell(col, row + y0);
      Style s = c.style;
      bool at_cursor = show_cursor && cursor_on_ && col == cx_ && row + y0 == cy_;
      if (at_cursor) {
        s.fg = Color::hex(kDefaultBg);
        s.bg = Color::hex(kDefaultFg);
      }
      sc.put(x + col, y + row, c.ch.empty() ? " " : c.ch, s, 1);
    }
}

}  // namespace apron::tui
