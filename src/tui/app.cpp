#include "tui/app.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "core/i18n.hpp"
#include "core/time.hpp"
#include "tui/palette.hpp"
#include "tui/vt.hpp"

namespace apron::tui {

namespace {

// One way to show focus: the accent color, on an outline, a bar or a bracket.
const Style kFocus{Color::hex(0x7aa2f7), {}, true};

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

// A dashed box: a place that holds nothing yet.
void draw_dashed(Screen& sc, Rect r, Style border) {
  if (r.w < 2 || r.h < 2) return;
  sc.put(r.x, r.y, "╭", border);
  sc.put(r.x + r.w - 1, r.y, "╮", border);
  sc.put(r.x, r.y + r.h - 1, "╰", border);
  sc.put(r.x + r.w - 1, r.y + r.h - 1, "╯", border);
  sc.fill(r.x + 1, r.y, r.w - 2, "┄", border);
  sc.fill(r.x + 1, r.y + r.h - 1, r.w - 2, "┄", border);
  for (int y = r.y + 1; y < r.y + r.h - 1; ++y) {
    sc.put(r.x, y, "┆", border);
    sc.put(r.x + r.w - 1, y, "┆", border);
  }
}

void draw_new_agent(Screen& sc, Rect r, bool focused, int which, std::vector<Hit>& hits) {
  for (int y = r.y; y < r.y + r.h; ++y) sc.fill(r.x, y, r.w, " ", kText);
  draw_dashed(sc, r, focused ? kFocus : kLine);
  std::string label = "+ " + tr("New agent");
  int lw = text_width(label);
  sc.put(r.x + std::max(1, (r.w - lw) / 2), r.y + r.h / 2, label, focused ? kFocus : kSoft);
  hits.push_back({r.x, r.y, r.w, r.h, {Act::NewAgent, which, 0}});
}

// --- Cards ------------------------------------------------------------------------

int card_lines(const Item& it, bool with_buttons, int inner_w) {
  int n = it.from_run ? 2 : 1;
  if (text_width(it.task) + 1 + text_width(it.title) > inner_w) ++n;  // the title's second line
  if (!it.detail.empty()) ++n;
  if (with_buttons) n += 2;  // a blank line, then the buttons
  return n;
}

Style card_border(const Item& it, bool focused) {
  if (focused) return kFocus;
  if (it.state == RunState::Failed) return kRed;
  if (it.state == RunState::Waiting || it.review) return kAmber;
  return kLine;
}

void draw_card(Screen& sc, int x, int y, int w, const Item& it, bool focused, const std::vector<Button>& buttons,
               const Target& focus, int64_t now, int lane, int index, std::vector<Hit>& hits) {
  int h = card_lines(it, !buttons.empty(), w - 4) + 2;
  draw_box(sc, {x, y, w, h}, card_border(it, focused), focused);
  hits.push_back({x, y, w, h, {Act::Card, lane, index}});
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
  const std::string& title = it.title;
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
  if (!buttons.empty()) draw_buttons(sc, cx, cy + 1, iw, buttons, focus, hits);
}

int center_x(const Hit& h) { return h.x * 2 + h.w; }  // doubled, to stay in integers
int center_y(const Hit& h) { return h.y * 2 + h.h; }

}  // namespace

const Hit* hit_at(const std::vector<Hit>& hits, int x, int y) {
  // Later hits sit on top (a button inside its card).
  for (auto it = hits.rbegin(); it != hits.rend(); ++it)
    if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return &*it;
  return nullptr;
}

const Hit* find_hit(const std::vector<Hit>& hits, const Target& t) {
  for (const auto& h : hits)
    if (h.t == t) return &h;
  return nullptr;
}

const Hit* next_hit(const std::vector<Hit>& hits, const Target& from, Dir dir) {
  const Hit* cur = find_hit(hits, from);
  if (!cur) return nullptr;
  int cx = center_x(*cur), cy = center_y(*cur);
  const Hit* best = nullptr;
  long best_score = LONG_MAX;
  for (const auto& h : hits) {
    if (!h.focusable || &h == cur) continue;
    // What sits inside the current element (a card's buttons) is entered on
    // purpose, not passed through on the way elsewhere.
    if (h.x >= cur->x && h.x + h.w <= cur->x + cur->w && h.y >= cur->y && h.y + h.h <= cur->y + cur->h) continue;
    int hx = center_x(h), hy = center_y(h);
    long along = 0, across = 0;
    switch (dir) {
      case Dir::Up: along = cy - hy, across = std::abs(hx - cx); break;
      case Dir::Down: along = hy - cy, across = std::abs(hx - cx); break;
      case Dir::Left: along = cx - hx, across = std::abs(hy - cy); break;
      case Dir::Right: along = hx - cx, across = std::abs(hy - cy); break;
    }
    if (along <= 0) continue;
    // Only what lies wholly beyond the current element's edge.
    bool beyond = dir == Dir::Up     ? h.y + h.h <= cur->y
                  : dir == Dir::Down ? h.y >= cur->y + cur->h
                  : dir == Dir::Left ? h.x + h.w <= cur->x
                                     : h.x >= cur->x + cur->w;
    if (!beyond) continue;
    // Moving down a column beats jumping to the next one.
    bool vertical = dir == Dir::Up || dir == Dir::Down;
    long score = along + across * (vertical ? 4 : 2);
    if (score < best_score) best_score = score, best = &h;
  }
  return best;
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

int draw_buttons(Screen& sc, int x, int y, int w, const std::vector<Button>& buttons, const Target& focus,
                 std::vector<Hit>& hits) {
  int bx = x;
  for (const auto& b : buttons) {
    int bw = text_width(b.label) + 4;
    if (bx + bw > x + w) break;
    Target t{Act::Button, static_cast<int>(b.action), 0};
    bool on = focus == t;
    sc.put(bx, y, "[", on ? kFocus : kLine);
    sc.put(bx + 2, y, b.label, on ? kFocus : kSoft);
    sc.put(bx + bw - 1, y, "]", on ? kFocus : kLine);
    hits.push_back({bx, y, bw, 1, t});
    bx += bw + 1;
  }
  return bx - x;
}

void render_sidebar(Screen& sc, Rect r, const Fleet& fleet, const Target& focus, int64_t now,
                    std::vector<Hit>& hits) {
  bool inside = focus.act == Act::Project || focus.act == Act::Session || focus.act == Act::SessionEnd ||
                focus.act == Act::Machine || focus.act == Act::MachineRemove || focus.act == Act::AddMachine;
  draw_box(sc, r, inside ? kBlue2 : kLine, false);
  sc.put(r.x + 2, r.y, " apron ", kBold);
  int x = r.x + 2, w = r.w - 4, y = r.y + 1, bottom = r.y + r.h - 1;

  // A row: a bar on its left and the accent color while it has the focus.
  // `action` is the row's button, shown on the right while the row or the
  // button has the focus; otherwise the right side holds `right`.
  auto line = [&](Target t, const std::string& left, Style ls, const std::string& right, Style rs, const char* glyph,
                  Style gs, int indent, const std::string& action, Target action_t) {
    if (y >= bottom) return;
    bool on = focus == t;
    bool show_action = !action.empty() && (on || focus == action_t);
    if (on) sc.put(r.x + 1, y, "▌", kFocus);
    int cx = x + indent;
    if (glyph) {
      sc.put(cx, y, glyph, gs);
      cx += 2;
    }
    std::string rt = show_action ? "" : right;
    int aw = show_action ? text_width(action) + 4 : 0;
    int rw = std::max(text_width(rt), aw);
    sc.put(cx, y, fit(left, x + w - cx - rw - 1), on ? kFocus : ls);
    if (show_action) {
      bool aon = focus == action_t;
      int ax = x + w - aw;
      sc.put(ax, y, "[", aon ? kFocus : kLine);
      sc.put(ax + 2, y, action, aon ? kFocus : kSoft);
      sc.put(ax + aw - 1, y, "]", aon ? kFocus : kLine);
      hits.push_back({r.x + 1, y, ax - r.x - 2, 1, t});
      hits.push_back({ax, y, aw, 1, action_t});
    } else {
      if (!rt.empty()) put_right(sc, x, y, w, rt, rs);
      hits.push_back({r.x + 1, y, r.w - 2, 1, t});
    }
    ++y;
  };

  ++y;
  sc.put(x, y, tr("Projects"), kDim);
  y += 2;
  for (size_t gi = 0; gi < fleet.projects.size(); ++gi) {
    const auto& p = fleet.projects[gi];
    int g = static_cast<int>(gi);
    line({Act::Project, g, 0}, p.name, p.root.empty() ? kSoft : kText, p.machine, kDim, nullptr, kText, 0, "", {});
    for (size_t si = 0; si < p.sessions.size(); ++si) {
      const auto& s = p.sessions[si];
      int i = static_cast<int>(si);
      std::string who = s.what + (s.machine.empty() ? "" : "@" + s.machine);
      std::string when = s.state == SessionState::Exited ? tr("ended") : ago(now, s.since_ms);
      line({Act::Session, g, i}, who, s.state == SessionState::Exited ? kDim : kSoft, when, kDim,
           session_glyph(s.state), session_style(s.state), 2, tr("End"), {Act::SessionEnd, g, i});
    }
  }
  if (y < bottom - 1) {
    ++y;
    sc.put(x, y, tr("Machines"), kDim);
    y += 2;
  }
  for (size_t mi = 0; mi < fleet.machines.size(); ++mi) {
    const auto& m = fleet.machines[mi];
    int i = static_cast<int>(mi);
    std::string note;
    Style ns = kDim;
    if (m.here) note = tr("this machine");
    else if (m.ok) note = m.running ? trf("{} running", {std::to_string(m.running)}) : "";
    else if (m.seen_ms) note = trf("seen {} ago", {ago(now, m.seen_ms)}), ns = kAmber;
    else note = tr("unreachable"), ns = kAmber;
    line({Act::Machine, i, 0}, m.name, m.ok || m.here ? kText : kSoft, note, ns, m.ok || m.here ? "●" : "○",
         m.ok || m.here ? kSoft : kDim, 0, m.here ? "" : tr("Remove"), {Act::MachineRemove, i, 0});
  }
  if (y < bottom) {
    Target t{Act::AddMachine, 0, 0};
    bool on = focus == t;
    if (on) sc.put(r.x + 1, y, "▌", kFocus);
    sc.put(x, y, "+ " + tr("Add machine"), on ? kFocus : kDim);
    hits.push_back({r.x + 1, y, r.w - 2, 1, t});
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
    sc.put(tx, r.y, label, static_cast<int>(i) == active ? kFocus : kDim);
    hits.push_back({tx, r.y, tw, 1, {Act::Tab, static_cast<int>(i), 0}, false});
    tx += tw;
    if (i + 1 < tabs.size()) sc.put(tx++, r.y, " ", kDim);
  }
  return {r.x + 2, r.y + 1, r.w - 4, r.h - 2};
}

void render_board(Screen& sc, Rect r, const Model& m, const View& v, const Target& focus,
                  const std::vector<Button>& buttons, int64_t now, std::vector<Hit>& hits) {
  const std::string names[4] = {tr("Plan"), tr("Running"), tr("Gates"), tr("Done")};
  // The current card shows its buttons while it or one of them has the focus.
  bool on_card = focus == Target{Act::Card, v.lane, v.index} || focus.act == Act::Button;
  // Running and Gates hold boxes with more to say; Plan and Done are lists.
  int gap = 2;
  const int weight[4] = {2, 3, 3, 2};
  int room = std::max(40, r.w - 3 * gap);
  int widths[4], xs[4], x = r.x;
  for (int i = 0; i < 4; ++i) {
    widths[i] = room * weight[i] / 10;
    xs[i] = x;
    x += widths[i] + gap;
  }
  int top = r.y + 1;
  for (int lane = 0; lane < 4; ++lane) {
    int lx = xs[lane], lw = widths[lane];
    const auto& items = m.lanes[lane];
    int hx = sc.put(lx, top, names[lane], kDim);
    sc.put(lx + hx + 1, top, std::to_string(items.size()), kSoft);
    int y = top + 2, bottom = r.y + r.h;
    bool current_lane = v.lane == lane;

    if (lane == 0 || lane == 3) {
      // Plan and Done: plain lists; boxes are for work that is moving.
      int start = std::max(0, (current_lane ? v.index : 0) - (bottom - y - 3));
      for (int i = start; i < static_cast<int>(items.size()) && y < bottom; ++i, ++y) {
        const Item& it = items[static_cast<size_t>(i)];
        bool on = focus == Target{Act::Card, lane, i};
        if (on) sc.put(lx - 1, y, "▌", kFocus);
        const char* g = lane == 3 ? "✓" : it.ready ? "○" : "◌";
        bool lit = lane == 3 || it.ready;
        sc.put(lx, y, g, lit ? kSoft : kDim);
        int tx = sc.put(lx + 2, y, it.task, on ? kFocus : lit ? kText : kDim);
        sc.put(lx + 3 + tx, y, fit(it.title, lw - tx - 3), on ? kFocus : lit ? kSoft : kDim);
        hits.push_back({lx, y, lw, 1, {Act::Card, lane, i}});
      }
      if (start + (y - (top + 2)) < static_cast<int>(items.size()) && y < bottom)
        sc.put(lx + 2, y++, trf("{} more", {std::to_string(static_cast<int>(items.size()) - start - (y - (top + 2)))}),
               kDim);
      // The buttons of a listed row sit under the list.
      if (current_lane && on_card && !buttons.empty() && y + 1 < bottom) draw_buttons(sc, lx, y + 1, lw, buttons, focus, hits);
      continue;
    }

    // Scroll so the current card is in view.
    std::vector<int> heights;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      bool with = current_lane && i == v.index && on_card && !buttons.empty();
      heights.push_back(card_lines(items[static_cast<size_t>(i)], with, lw - 4) + 2);
    }
    int start = 0;
    if (current_lane && !items.empty()) {
      auto span = [&](int a, int b) {
        int t = 0;
        for (int i = a; i <= b; ++i) t += heights[static_cast<size_t>(i)];
        return t;
      };
      int idx = std::min(v.index, static_cast<int>(items.size()) - 1);
      while (start < idx && span(start, idx) > bottom - y - (lane == 1 ? 3 : 0)) ++start;
    }
    if (start > 0) sc.put(lx + lw - 4, top, "↑" + std::to_string(start), kDim);
    int i = start;
    for (; i < static_cast<int>(items.size()); ++i) {
      int h = heights[static_cast<size_t>(i)];
      if (y + h > bottom) break;
      bool with = current_lane && i == v.index && on_card;
      draw_card(sc, lx, y, lw, items[static_cast<size_t>(i)], focus == Target{Act::Card, lane, i},
                with ? buttons : std::vector<Button>{}, focus, now, lane, i, hits);
      y += h;
    }
    if (i < static_cast<int>(items.size()) && y < bottom)
      sc.put(lx, y++, trf("{} more", {std::to_string(static_cast<int>(items.size()) - i)}), kDim);
    // Running always ends with a place to start another agent.
    if (lane == 1 && y + 3 <= bottom) draw_new_agent(sc, {lx, y, lw, 3}, focus == Target{Act::NewAgent, 0, 0}, 0, hits);
  }
}

void render_grid(Screen& sc, Rect r, const std::vector<Tile>& tiles, const Target& focus, std::vector<Hit>& hits) {
  size_t n = tiles.size() + 1;  // the last tile starts a new agent
  int max_cols = std::max(1, r.w / 44);  // a tile narrower than this reads as noise
  int cols = 1;
  while (cols < max_cols && static_cast<size_t>(cols * cols) < n) ++cols;
  cols = std::min(cols, static_cast<int>(n));
  int rows = static_cast<int>((n + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
  int tw = (r.w - (cols - 1)) / cols, th = std::max(4, (r.h - (rows - 1)) / rows);
  for (size_t i = 0; i < n; ++i) {
    int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
    Rect t{r.x + col * (tw + 1), r.y + row * (th + 1), tw, th};
    if (t.y + t.h > r.y + r.h) break;
    if (i == tiles.size()) {
      // Alone, the placeholder stays small; beside agents it takes a tile.
      if (tiles.empty()) t = {r.x, r.y + 1, std::min(r.w, 44), 5};
      draw_new_agent(sc, t, focus == Target{Act::NewAgent, 1, 0}, 1, hits);
      break;
    }
    bool on = focus == Target{Act::Tile, static_cast<int>(i), 0};
    for (int y = t.y; y < t.y + t.h; ++y) sc.fill(t.x, y, t.w, " ", kText);
    draw_box(sc, t, on ? kFocus : kLine, on);
    sc.put(t.x + 2, t.y, " " + fit(tiles[i].title, t.w - 6) + " ", on ? kFocus : kSoft);
    if (tiles[i].vt) tiles[i].vt->draw(sc, t.x + 1, t.y + 1, t.w - 2, t.h - 2, false);
    hits.push_back({t.x, t.y, t.w, t.h, {Act::Tile, static_cast<int>(i), 0}});
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
      {"←→↑↓ hjkl", "move"},   {"enter", "do what is selected"}, {"esc", "back"},
      {"tab", "next view"},    {"ctrl-]", "leave a terminal"},  {"q", "quit"},
  };
  int key_w = 0, what_w = 0;
  for (const auto& [k, what] : keys) {
    key_w = std::max(key_w, text_width(k));
    what_w = std::max(what_w, text_width(tr(what)));
  }
  int w = 3 + key_w + 3 + what_w + 3, h = static_cast<int>(std::size(keys)) + 4;
  Rect box{r.x + (r.w - w) / 2, r.y + std::max(0, (r.h - h) / 2), w, h};
  for (int y = box.y; y < box.y + box.h; ++y) sc.fill(box.x, y, box.w, " ", kText);
  draw_box(sc, box, kBlue, false);
  sc.put(box.x + 2, box.y, " " + tr("Keys") + " ", kBold);
  int y = box.y + 2;
  for (const auto& [k, what] : keys) {
    sc.put(box.x + 3, y, k, kText);
    sc.put(box.x + 3 + key_w + 3, y++, tr(what), kSoft);
  }
}

}  // namespace apron::tui
