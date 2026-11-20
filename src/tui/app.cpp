#include "tui/app.hpp"

#include <algorithm>

#include "core/i18n.hpp"
#include "core/time.hpp"
#include "tui/palette.hpp"

namespace apron::tui {

namespace {

const Style kButton{Color::hex(0xe6e8eb), Color::hex(0x2a303a), false};
const Style kButtonMain{Color::hex(0x0f1115), Color::hex(0x7aa2f7), true};
const Style kTabOn{Color::hex(0xe6e8eb), Color::hex(kSelected), true};

const char* session_glyph(SessionState s) {
  switch (s) {
    case SessionState::Working: return "●";
    case SessionState::Asking: return "◐";
    case SessionState::Idle: return "○";
    case SessionState::Exited: return "·";
  }
  return "○";
}

Style session_style(SessionState s) {
  switch (s) {
    case SessionState::Working: return kBlue;
    case SessionState::Asking: return kAmber;
    default: return kDim;
  }
}

// Right-aligned text inside [x, x + w).
void put_right(Screen& sc, int x, int y, int w, const std::string& text, Style s) {
  int tw = text_width(text);
  if (tw <= w) sc.put(x + w - tw, y, text, s);
}

// --- Cards ------------------------------------------------------------------------

int card_lines(const Item& it, bool selected, bool with_buttons, int inner_w) {
  int n = it.from_run ? 2 : 1;
  if (text_width(it.task) + 1 + text_width(it.title) > inner_w) ++n;  // the title's second line
  if (!it.detail.empty()) ++n;
  if (selected && with_buttons) n += 2;  // a blank line, then the buttons
  return n;
}

Style card_border(const Item& it, bool selected) {
  if (selected) return kBlue;
  if (it.state == RunState::Failed) return kRed;
  if (it.state == RunState::Waiting || it.review) return kAmber;
  return kLine;
}

void draw_card(Screen& sc, int x, int y, int w, const Item& it, bool selected, const std::vector<Button>& buttons,
               int64_t now, int lane, int index, std::vector<Hit>& hits) {
  int h = card_lines(it, selected, !buttons.empty(), w - 4) + 2;
  draw_box(sc, {x, y, w, h}, card_border(it, selected), selected);
  hits.push_back({x, y, w, h, Act::Card, lane, index});
  int iw = w - 4, cx = x + 2, cy = y + 1;
  if (it.from_run) {
    Style gs = state_style(it.state);
    sc.put(cx, cy, state_glyph(it.review ? RunState::Done : it.state), it.review ? kSoft : gs);
    std::string when = it.since_ms ? ago(now, it.since_ms) : "";
    int room = iw - 2 - text_width(when) - 1;
    sc.put(cx + 2, cy, fit(it.who, room), kSoft);
    put_right(sc, cx, cy, iw, when, kDim);
    ++cy;
  }
  int tx = sc.put(cx, cy, it.task, kBold);
  // The title takes two lines before it is cut.
  int first = iw - tx - 1;
  std::string title = it.title;
  if (text_width(title) <= first) {
    sc.put(cx + tx + 1, cy++, title, kText);
  } else {
    size_t cut = 0;
    int cols = 0;
    for (size_t i = 0; i < title.size() && cols < first; ++cols) {
      unsigned char ch = static_cast<unsigned char>(title[i]);
      i += ch < 0x80 ? 1 : (ch >> 5) == 0x6 ? 2 : (ch >> 4) == 0xE ? 3 : 4;
      if (i <= title.size() && (i == title.size() || title[i] == ' ')) cut = i;
    }
    if (cut == 0) cut = std::min(title.size(), static_cast<size_t>(first));
    sc.put(cx + tx + 1, cy++, title.substr(0, cut), kText);
    std::string rest = title.substr(cut);
    while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
    sc.put(cx, cy++, fit(rest, iw), kText);
  }
  if (!it.detail.empty()) {
    Style ds = it.state == RunState::Waiting ? kAmber : kDim;
    sc.put(cx, cy, fit(it.detail, iw), ds);
    ++cy;
  }
  if (selected && !buttons.empty()) {
    ++cy;
    int bx = cx;
    for (size_t i = 0; i < buttons.size(); ++i) {
      std::string label = " " + buttons[i].label + " ";
      int bw = text_width(label);
      if (bx + bw > cx + iw) break;
      sc.put(bx, cy, label, i == 0 ? kButtonMain : kButton);
      hits.push_back({bx, cy, bw, 1, Act::Button, static_cast<int>(buttons[i].action), 0});
      bx += bw + 1;
    }
  }
}

}  // namespace

std::vector<SideRow> side_rows(const Fleet& fleet) {
  std::vector<SideRow> rows;
  for (size_t g = 0; g < fleet.projects.size(); ++g) {
    rows.push_back({SideRow::Kind::Project, static_cast<int>(g), 0});
    for (size_t i = 0; i < fleet.projects[g].sessions.size(); ++i)
      rows.push_back({SideRow::Kind::Session, static_cast<int>(g), static_cast<int>(i)});
  }
  for (size_t m = 0; m < fleet.machines.size(); ++m) rows.push_back({SideRow::Kind::Machine, static_cast<int>(m), 0});
  return rows;
}

const Hit* hit_at(const std::vector<Hit>& hits, int x, int y) {
  // Later hits sit on top (a button inside its card).
  for (auto it = hits.rbegin(); it != hits.rend(); ++it)
    if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return &*it;
  return nullptr;
}

int sidebar_width(int screen_w) {
  if (screen_w < 90) return 0;
  return std::clamp(screen_w / 5, 28, 36);
}

void draw_box(Screen& sc, Rect r, Style border, bool heavy) {
  if (r.w < 2 || r.h < 2) return;
  const char* h = heavy ? "━" : "─";
  const char* v = heavy ? "┃" : "│";
  sc.put(r.x, r.y, heavy ? "┏" : "╭", border);
  sc.put(r.x + r.w - 1, r.y, heavy ? "┓" : "╮", border);
  sc.put(r.x, r.y + r.h - 1, heavy ? "┗" : "╰", border);
  sc.put(r.x + r.w - 1, r.y + r.h - 1, heavy ? "┛" : "╯", border);
  sc.fill(r.x + 1, r.y, r.w - 2, h, border);
  sc.fill(r.x + 1, r.y + r.h - 1, r.w - 2, h, border);
  for (int y = r.y + 1; y < r.y + r.h - 1; ++y) {
    sc.put(r.x, y, v, border);
    sc.put(r.x + r.w - 1, y, v, border);
  }
}

void render_sidebar(Screen& sc, Rect r, const Fleet& fleet, int selected, bool focused, int64_t now,
                    std::vector<Hit>& hits) {
  draw_box(sc, r, focused ? kBlue2 : kLine, false);
  sc.put(r.x + 2, r.y, " apron ", kBold);
  int x = r.x + 2, w = r.w - 4, y = r.y + 1, bottom = r.y + r.h - 1;
  auto rows = side_rows(fleet);
  int row_index = 0;
  auto line = [&](int index, const std::string& left, Style ls, const std::string& right, Style rs, const char* glyph,
                  Style gs, int indent) {
    if (y >= bottom) return;
    bool sel = index == selected;
    Style bgs = sel && focused ? with_bg(kText, true) : kText;
    if (sel && focused) sc.fill(r.x + 1, y, r.w - 2, " ", bgs);
    int cx = x + indent;
    if (glyph) {
      sc.put(cx, y, glyph, sel && focused ? with_bg(gs, true) : gs);
      cx += 2;
    }
    int rw = text_width(right);
    sc.put(cx, y, fit(left, x + w - cx - rw - 1), sel && focused ? with_bg(ls, true) : (sel ? kBold : ls));
    if (rw) put_right(sc, x, y, w, right, sel && focused ? with_bg(rs, true) : rs);
    hits.push_back({r.x + 1, y, r.w - 2, 1, Act::Side, index, 0});
    ++y;
  };

  ++y;
  sc.put(x, y, tr("Projects"), kDim);
  sc.put(x + w - 1, y, "+", kSoft);
  hits.push_back({x + w - 2, y, 3, 1, Act::NewSession, 0, 0});
  y += 2;
  if (fleet.projects.empty()) sc.put(x, y++, tr("none yet"), kDim);
  for (const auto& p : fleet.projects) {
    line(row_index++, p.name, p.root.empty() ? kSoft : kText, p.machine, kDim, nullptr, kText, 0);
    for (const auto& s : p.sessions) {
      std::string who = s.what + (s.machine.empty() ? "" : "@" + s.machine);
      std::string when = s.state == SessionState::Exited ? tr("ended") : ago(now, s.since_ms);
      line(row_index++, who, s.state == SessionState::Exited ? kDim : kSoft, when, kDim, session_glyph(s.state),
           session_style(s.state), 2);
    }
  }
  if (y < bottom - 1) {
    ++y;
    sc.put(x, y, tr("Machines"), kDim);
    sc.put(x + w - 1, y, "+", kSoft);
    hits.push_back({x + w - 2, y, 3, 1, Act::AddMachine, 0, 0});
    y += 2;
  }
  for (const auto& m : fleet.machines) {
    std::string note;
    Style ns = kDim;
    if (m.here) note = tr("this machine");
    else if (m.ok) note = m.running ? trf("{} running", {std::to_string(m.running)}) : "";
    else if (m.seen_ms) note = trf("seen {} ago", {ago(now, m.seen_ms)}), ns = kAmber;
    else note = tr("unreachable"), ns = kAmber;
    line(row_index++, m.name, m.ok || m.here ? kText : kSoft, note, ns, m.ok || m.here ? "●" : "○",
         m.ok || m.here ? kSoft : kDim, 0);
  }
}

Rect render_main_frame(Screen& sc, Rect r, const std::string& title, const std::vector<std::string>& tabs, int active,
                       bool focused, std::vector<Hit>& hits) {
  draw_box(sc, r, focused ? kBlue2 : kLine, false);
  sc.put(r.x + 2, r.y, " " + fit(title, r.w / 2) + " ", kBold);
  int total = 0;
  for (const auto& t : tabs) total += text_width(t) + 3;
  int tx = r.x + r.w - 2 - total;
  for (size_t i = 0; i < tabs.size(); ++i) {
    std::string label = " " + tabs[i] + " ";
    int tw = text_width(label);
    sc.put(tx, r.y, label, static_cast<int>(i) == active ? kTabOn : kDim);
    hits.push_back({tx, r.y, tw, 1, Act::Tab, static_cast<int>(i), 0});
    tx += tw;
    if (i + 1 < tabs.size()) sc.put(tx++, r.y, " ", kDim);
  }
  return {r.x + 2, r.y + 1, r.w - 4, r.h - 2};
}

void render_board(Screen& sc, Rect r, const Model& m, const View& v, bool focused, const std::vector<Button>& buttons,
                  int64_t now, std::vector<Hit>& hits) {
  const std::string names[4] = {tr("Plan"), tr("Running"), tr("Gates"), tr("Done")};
  int gap = 2;
  int lw = std::max(12, (r.w - 3 * gap) / 4);
  int top = r.y + 1;
  for (int lane = 0; lane < 4; ++lane) {
    int lx = r.x + lane * (lw + gap);
    const auto& items = m.lanes[lane];
    int hx = sc.put(lx, top, names[lane], kDim);
    sc.put(lx + hx + 1, top, std::to_string(items.size()), kSoft);
    int y = top + 2, bottom = r.y + r.h;
    bool lane_sel = focused && v.lane == lane;
    if (items.empty()) continue;

    if (lane == 0 || lane == 3) {
      // Plan and Done: plain lists; boxes are for work that is moving.
      int start = std::max(0, (lane_sel ? v.index : 0) - (bottom - y - 3));
      for (int i = start; i < static_cast<int>(items.size()) && y < bottom; ++i, ++y) {
        const Item& it = items[static_cast<size_t>(i)];
        bool sel = lane_sel && i == v.index;
        if (sel) sc.fill(lx, y, lw, " ", with_bg(kText, true));
        const char* g = lane == 3 ? "✓" : it.ready ? "○" : "◌";
        sc.put(lx, y, g, with_bg(lane == 3 || it.ready ? kSoft : kDim, sel));
        int tx = sc.put(lx + 2, y, it.task, with_bg(lane == 3 || it.ready ? kText : kDim, sel));
        sc.put(lx + 3 + tx, y, fit(it.title, lw - tx - 3), with_bg(lane == 3 || it.ready ? kSoft : kDim, sel));
        hits.push_back({lx, y, lw, 1, Act::Card, lane, i});
      }
      if (start + (y - (top + 2)) < static_cast<int>(items.size()) && y < bottom)
        sc.put(lx + 2, y, trf("{} more", {std::to_string(static_cast<int>(items.size()) - start - (y - (top + 2)))}), kDim);
      if (lane_sel && !buttons.empty()) {
        // The buttons of a listed row sit under the list.
        int bx = lx;
        if (y + 1 < bottom)
          for (size_t i = 0; i < buttons.size(); ++i) {
            std::string label = " " + buttons[i].label + " ";
            sc.put(bx, y + 1, label, i == 0 ? kButtonMain : kButton);
            hits.push_back({bx, y + 1, text_width(label), 1, Act::Button, static_cast<int>(buttons[i].action), 0});
            bx += text_width(label) + 1;
          }
      }
      continue;
    }

    // Scroll so the selected card is in view.
    std::vector<int> heights;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
      heights.push_back(card_lines(items[static_cast<size_t>(i)], lane_sel && i == v.index, !buttons.empty(), lw - 4) + 2);
    int start = 0;
    if (lane_sel) {
      auto span = [&](int a, int b) {
        int t = 0;
        for (int i = a; i <= b; ++i) t += heights[static_cast<size_t>(i)];
        return t;
      };
      while (start < v.index && span(start, v.index) > bottom - y) ++start;
    }
    if (start > 0) sc.put(lx + lw - 4, top, "↑" + std::to_string(start), kDim);
    int i = start;
    for (; i < static_cast<int>(items.size()); ++i) {
      int h = heights[static_cast<size_t>(i)];
      if (y + h > bottom) break;
      bool sel = lane_sel && i == v.index;
      draw_card(sc, lx, y, lw, items[static_cast<size_t>(i)], sel, sel ? buttons : std::vector<Button>{}, now, lane, i,
                hits);
      y += h;
    }
    if (i < static_cast<int>(items.size()) && y < bottom)
      sc.put(lx, std::min(y, bottom - 1), trf("{} more", {std::to_string(static_cast<int>(items.size()) - i)}), kDim);
  }
}

void render_footer(Screen& sc, const std::vector<std::pair<std::string, std::string>>& keys, const std::string& status) {
  int y = sc.height() - 1, x = 1;
  sc.fill(0, y, sc.width(), " ", kText);
  if (!status.empty()) {
    sc.put(x, y, fit(status, sc.width() - 2), kAmber);
    return;
  }
  for (const auto& [key, label] : keys) {
    if (x + text_width(key) + text_width(label) + 4 > sc.width()) break;
    x += sc.put(x, y, key, kText) + 1;
    x += sc.put(x, y, label, kDim) + 3;
  }
}

void render_prompt(Screen& sc, Rect r, const std::string& title, const std::string& text, bool typing) {
  int w = std::min(r.w - 4, std::max(48, text_width(title) + 6));
  Rect box{r.x + (r.w - w) / 2, r.y + r.h / 3, w, 5};
  for (int y = box.y; y < box.y + box.h; ++y) sc.fill(box.x, y, box.w, " ", kText);
  draw_box(sc, box, kBlue, false);
  sc.put(box.x + 2, box.y, " " + fit(title, w - 6) + " ", kBold);
  sc.put(box.x + 2, box.y + 2, fit(typing ? text + "▏" : text, w - 4), typing ? kText : kSoft);
}

void render_help(Screen& sc, Rect r) {
  const std::pair<const char*, const char*> keys[] = {
      {"tab", "next view"},          {"shift-tab", "previous view"}, {"←→↑↓ hjkl", "move"},
      {"enter", "open"},             {"esc", "back"},                {"t", "the agent's terminal"},
      {"o", "the run's log"},        {"n", "new session"},           {"+", "add a machine"},
      {"x", "end a session"},        {"ctrl-]", "leave a terminal"}, {"q", "quit"},
  };
  int w = 46, h = static_cast<int>(std::size(keys)) + 4;
  Rect box{r.x + (r.w - w) / 2, r.y + std::max(0, (r.h - h) / 2), w, h};
  for (int y = box.y; y < box.y + box.h; ++y) sc.fill(box.x, y, box.w, " ", kText);
  draw_box(sc, box, kBlue, false);
  sc.put(box.x + 2, box.y, " " + tr("Keys") + " ", kBold);
  int y = box.y + 2;
  for (const auto& [k, what] : keys) {
    sc.put(box.x + 3, y, k, kText);
    sc.put(box.x + 17, y++, tr(what), kSoft);
  }
}

}  // namespace apron::tui
