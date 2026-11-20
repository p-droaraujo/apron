#include "tui/vt.hpp"

#include <ghostty/vt.h>

#include <algorithm>

namespace apron::tui {

namespace {

std::string utf8(uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return s;
}

uint32_t rgb(GhosttyColorRgb c) { return (uint32_t{c.r} << 16) | (uint32_t{c.g} << 8) | c.b; }

}  // namespace

struct Vt::Impl {
  GhosttyTerminal term = nullptr;
  GhosttyRenderState state = nullptr;
  GhosttyRenderStateRowIterator rows = nullptr;
  GhosttyRenderStateRowCells cells = nullptr;
  std::string replies;
  uint16_t cols = 0, rows_n = 0;

  // A copy of the screen, refreshed only after new output.
  bool stale = true;
  std::vector<Cell> grid;
  int cx = 0, cy = 0;
  bool cursor_on = true;
  uint32_t default_fg = 0xd0d0d0, default_bg = 0x000000;

  static void write_pty(GhosttyTerminal, void* self, const uint8_t* data, size_t len) {
    static_cast<Impl*>(self)->replies.append(reinterpret_cast<const char*>(data), len);
  }
  static bool size(GhosttyTerminal, void* self, GhosttySizeReportSize* out) {
    auto* me = static_cast<Impl*>(self);
    out->rows = me->rows_n;
    out->columns = me->cols;
    out->cell_width = 8;
    out->cell_height = 16;
    return true;
  }
  static bool device_attributes(GhosttyTerminal, void*, GhosttyDeviceAttributes* out) {
    out->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
    out->primary.features[0] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
    out->primary.num_features = 1;
    out->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
    out->secondary.firmware_version = 1;
    out->secondary.rom_cartridge = 0;
    out->tertiary.unit_id = 0;
    return true;
  }
  static GhosttyString xtversion(GhosttyTerminal, void*) {
    static const char kName[] = "apron";
    return GhosttyString{reinterpret_cast<const uint8_t*>(kName), sizeof kName - 1};
  }
};

Vt::Vt(int cols, int rows) : impl_(new Impl), cols_(std::max(1, cols)), rows_(std::max(1, rows)) {
  impl_->cols = static_cast<uint16_t>(cols_);
  impl_->rows_n = static_cast<uint16_t>(rows_);
  ghostty_terminal_new(nullptr, &impl_->term, impl_->cols, impl_->rows_n);
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_USERDATA, impl_);
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void*>(&Impl::write_pty));
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void*>(&Impl::size));
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
                       reinterpret_cast<const void*>(&Impl::device_attributes));
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_XTVERSION, reinterpret_cast<const void*>(&Impl::xtversion));
  size_t scrollback = 0;  // the daemon keeps the history; a pane shows the screen
  ghostty_terminal_set(impl_->term, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollback);
  ghostty_render_state_new(nullptr, &impl_->state);
  ghostty_render_state_row_iterator_new(nullptr, &impl_->rows);
  ghostty_render_state_row_cells_new(nullptr, &impl_->cells);
}

Vt::~Vt() {
  ghostty_render_state_row_cells_free(impl_->cells);
  ghostty_render_state_row_iterator_free(impl_->rows);
  ghostty_render_state_free(impl_->state);
  ghostty_terminal_free(impl_->term);
  delete impl_;
}

void Vt::resize(int cols, int rows) {
  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
  impl_->cols = static_cast<uint16_t>(cols_);
  impl_->rows_n = static_cast<uint16_t>(rows_);
  ghostty_terminal_resize(impl_->term, impl_->cols, impl_->rows_n, 8, 16);
  impl_->stale = true;
}

void Vt::feed(std::string_view bytes) {
  ghostty_terminal_vt_write(impl_->term, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  impl_->stale = true;
}

std::string Vt::take_replies() {
  std::string out;
  out.swap(impl_->replies);
  return out;
}

void Vt::sync() const {
  Impl& m = *impl_;
  if (!m.stale) return;
  m.stale = false;
  m.grid.assign(static_cast<size_t>(cols_ * rows_), Cell{});
  if (ghostty_render_state_update(m.state, m.term) != GHOSTTY_SUCCESS) return;

  GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  if (ghostty_render_state_colors_get(m.state, &colors) == GHOSTTY_SUCCESS) {
    m.default_fg = rgb(colors.foreground);
    m.default_bg = rgb(colors.background);
  }
  if (ghostty_render_state_get(m.state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &m.rows) != GHOSTTY_SUCCESS) return;

  int y = 0;
  while (y < rows_ && ghostty_render_state_row_iterator_next(m.rows)) {
    if (ghostty_render_state_row_get(m.rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &m.cells) != GHOSTTY_SUCCESS) {
      ++y;
      continue;
    }
    int x = 0;
    while (x < cols_ && ghostty_render_state_row_cells_next(m.cells)) {
      Cell& out = m.grid[static_cast<size_t>(y * cols_ + x)];
      ++x;

      GhosttyCell raw{};
      uint32_t wide = GHOSTTY_CELL_WIDE_NARROW;
      if (ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw) == GHOSTTY_SUCCESS)
        ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
      if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL) {
        out.ch.clear();  // the right half of a wide character drawn to its left
        continue;
      }

      uint32_t len = 0;
      ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &len);
      if (len > 0) {
        uint32_t cps[16];
        ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cps);
        out.ch.clear();
        for (uint32_t i = 0; i < std::min<uint32_t>(len, 16); ++i) out.ch += utf8(cps[i]);
      }

      // Colors come resolved to RGB; a cell without one keeps the
      // terminal's default, so Apron's own background shows through.
      GhosttyColorRgb c{};
      if (ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &c) == GHOSTTY_SUCCESS)
        out.style.fg = Color::hex(rgb(c));
      if (ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &c) == GHOSTTY_SUCCESS)
        out.style.bg = Color::hex(rgb(c));
      GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
      if (ghostty_render_state_row_cells_get(m.cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style) == GHOSTTY_SUCCESS) {
        out.style.bold = style.bold;
        if (style.inverse) {
          Color fg = out.style.fg.set ? out.style.fg : Color::hex(m.default_fg);
          Color bg = out.style.bg.set ? out.style.bg : Color::hex(m.default_bg);
          out.style.fg = bg;
          out.style.bg = fg;
        }
        if (style.invisible) out.ch = " ";
      }
    }
    ++y;
  }

  bool visible = false, in_view = false;
  ghostty_render_state_get(m.state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &visible);
  ghostty_render_state_get(m.state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &in_view);
  m.cursor_on = visible && in_view;
  if (in_view) {
    uint16_t cx = 0, cy = 0;
    ghostty_render_state_get(m.state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
    ghostty_render_state_get(m.state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
    m.cx = cx;
    m.cy = cy;
  }
}

Cell Vt::cell(int x, int y) const {
  sync();
  if (x < 0 || y < 0 || x >= cols_ || y >= rows_) return {};
  return impl_->grid[static_cast<size_t>(y * cols_ + x)];
}

int Vt::cursor_x() const {
  sync();
  return impl_->cx;
}

int Vt::cursor_y() const {
  sync();
  return impl_->cy;
}

bool Vt::cursor_visible() const {
  sync();
  return impl_->cursor_on;
}

bool Vt::app_cursor() const {
  GhosttyTerminalModeConfig mode{};
  mode.mode = GHOSTTY_MODE_DECCKM;
  if (ghostty_terminal_get(impl_->term, GHOSTTY_TERMINAL_DATA_MODE, &mode) != GHOSTTY_SUCCESS) return false;
  return mode.value;
}

void Vt::draw(Screen& sc, int x, int y, int w, int h, bool show_cursor) const {
  sync();
  const Impl& m = *impl_;
  // The bottom of the screen matters most when the pane is shorter.
  int y0 = std::max(0, std::min(rows_ - h, m.cy - h + 1));
  for (int row = 0; row < std::min(h, rows_ - y0); ++row)
    for (int col = 0; col < std::min(w, cols_); ++col) {
      const Cell& c = m.grid[static_cast<size_t>((row + y0) * cols_ + col)];
      Cell out = c;
      if (show_cursor && m.cursor_on && col == m.cx && row + y0 == m.cy) {
        out.style.fg = Color::hex(m.default_bg);
        out.style.bg = Color::hex(m.default_fg);
      }
      // A wide character cut by the pane's right edge would spill over it.
      if (col + 1 == std::min(w, cols_) && col + 1 < cols_ &&
          m.grid[static_cast<size_t>((row + y0) * cols_ + col + 1)].ch.empty())
        out.ch = " ";
      sc.set(x + col, y + row, out);
    }
}

}  // namespace apron::tui
