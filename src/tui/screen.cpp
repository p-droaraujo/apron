#include "tui/screen.hpp"

#include <cstdio>

namespace apron::tui {

namespace {

size_t utf8_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;
}

void sgr(std::string& out, const Style& s) {
  out += "\x1b[0";
  if (s.bold) out += ";1";
  char buf[48];
  if (s.fg.set) {
    std::snprintf(buf, sizeof buf, ";38;2;%u;%u;%u", (s.fg.rgb >> 16) & 0xFF, (s.fg.rgb >> 8) & 0xFF, s.fg.rgb & 0xFF);
    out += buf;
  }
  if (s.bg.set) {
    std::snprintf(buf, sizeof buf, ";48;2;%u;%u;%u", (s.bg.rgb >> 16) & 0xFF, (s.bg.rgb >> 8) & 0xFF, s.bg.rgb & 0xFF);
    out += buf;
  }
  out += 'm';
}

}  // namespace

int text_width(std::string_view text) {
  int w = 0;
  for (size_t i = 0; i < text.size(); i += utf8_len(static_cast<unsigned char>(text[i]))) ++w;
  return w;
}

std::string fit(std::string_view text, int w) {
  if (w <= 0) return "";
  if (text_width(text) <= w) return std::string(text);
  std::string out;
  int cols = 0;
  for (size_t i = 0; i < text.size() && cols < w - 1;) {
    size_t n = utf8_len(static_cast<unsigned char>(text[i]));
    out.append(text.substr(i, n));
    i += n;
    ++cols;
  }
  return out + "…";
}

void Screen::resize(int w, int h) {
  w_ = w < 1 ? 1 : w;
  h_ = h < 1 ? 1 : h;
  front_.assign(static_cast<size_t>(w_ * h_), Cell{});
  back_ = front_;
  full_ = true;
}

void Screen::clear() {
  for (auto& c : back_) c = Cell{};
}

int Screen::put(int x, int y, std::string_view text, Style style, int max_w) {
  if (y < 0 || y >= h_) return 0;
  int limit = max_w < 0 ? w_ - x : max_w;
  int col = 0;
  for (size_t i = 0; i < text.size() && col < limit;) {
    size_t n = utf8_len(static_cast<unsigned char>(text[i]));
    int cx = x + col;
    if (cx >= 0 && cx < w_) back_[static_cast<size_t>(y * w_ + cx)] = Cell{std::string(text.substr(i, n)), style};
    i += n;
    ++col;
  }
  return col;
}

void Screen::fill(int x, int y, int w, std::string_view ch, Style style) {
  for (int i = 0; i < w; ++i) put(x + i, y, ch, style, 1);
}

std::string Screen::flush() {
  std::string out = "\x1b[?2026h";
  Style current;
  bool style_known = false;
  int cur_x = -1, cur_y = -1;
  char buf[32];
  for (int y = 0; y < h_; ++y) {
    for (int x = 0; x < w_; ++x) {
      size_t i = static_cast<size_t>(y * w_ + x);
      if (!full_ && back_[i] == front_[i]) continue;
      if (cur_y != y || cur_x != x) {
        std::snprintf(buf, sizeof buf, "\x1b[%d;%dH", y + 1, x + 1);
        out += buf;
      }
      if (!style_known || !(back_[i].style == current)) {
        sgr(out, back_[i].style);
        current = back_[i].style;
        style_known = true;
      }
      out += back_[i].ch;
      front_[i] = back_[i];
      cur_x = x + 1;
      cur_y = y;
    }
  }
  full_ = false;
  if (cur_y < 0) return "";  // nothing changed: nothing to send
  out += "\x1b[0m\x1b[?2026l";
  return out;
}

}  // namespace apron::tui
