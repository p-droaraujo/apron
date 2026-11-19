#include "tui/progress.hpp"

#include <algorithm>
#include <map>
#include <sstream>

#include "core/i18n.hpp"
#include "core/review.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::tui {

namespace {

// The v2 palette: color only for state.
const Style kText{Color::hex(0xe6e8eb), {}, false};
const Style kBold{Color::hex(0xe6e8eb), {}, true};
const Style kSoft{Color::hex(0xa3aab5), {}, false};
const Style kDim{Color::hex(0x7d8592), {}, false};
const Style kLine{Color::hex(0x3a404b), {}, false};
const Style kBlue{Color::hex(0x7aa2f7), {}, false};
const Style kBlue2{Color::hex(0x4a6bb0), {}, false};
const Style kAmber{Color::hex(0xe0af68), {}, false};
const Style kRed{Color::hex(0xf7768e), {}, false};
const Style kBadge{Color::hex(0x0f1115), Color::hex(0xe6e8eb), true};
constexpr uint32_t kSelected = 0x1f2430;

Style with_bg(Style s, bool selected) {
  if (selected) s.bg = Color::hex(kSelected);
  return s;
}

Style state_style(RunState s) {
  switch (s) {
    case RunState::Working: return kBlue;
    case RunState::Waiting: return kAmber;
    case RunState::Failed: return kRed;
    default: return kDim;
  }
}

const char* state_glyph(RunState s) {
  switch (s) {
    case RunState::Working: return "●";
    case RunState::Waiting: return "◐";
    case RunState::Blocked: return "⊘";
    case RunState::Done: return "✓";
    case RunState::Failed: return "✕";
    case RunState::Abandoned: return "–";
  }
  return "●";
}

std::string ago(int64_t now, int64_t then) { return timeutil::short_duration(now - then); }

// Titles come from markdown; on screen, backticks help nobody.
std::string plain(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
  return s;
}

}  // namespace

Model load_model(const Project& project) {
  Model m;
  m.project = project.root.filename().string();
  m.machine = machine_name();

  auto file = TaskFile::load(project.tasks_file());
  std::map<std::string, const Task*> by_id;
  if (file)
    for (const auto& t : file->tasks()) by_id[t.id] = &t;

  auto runs = list_runs(project);
  std::map<std::string, bool> task_has_run;
  for (const auto& r : runs) {
    Item it;
    it.from_run = true;
    it.task = r.task;
    it.run = r.id;
    it.state = r.state;
    it.who = r.agent + "@" + r.machine;
    auto found = by_id.find(r.task);
    it.title = plain(found != by_id.end() ? found->second->title : r.intent);
    it.detail = r.last_msg;
    switch (r.state) {
      case RunState::Working:
      case RunState::Blocked:
        it.lane = Lane::Executando;
        it.since_ms = r.started_ms;
        break;
      case RunState::Waiting:
        it.lane = Lane::Portoes;
        it.since_ms = r.last_ms;
        break;
      default:
        // A finished run waits in gates until a person reviews it.
        it.lane = r.needs_review() ? Lane::Portoes : Lane::Feito;
        it.since_ms = r.last_ms;
        if (r.needs_review()) {
          it.detail = tr("waiting for review");
          it.review = true;
        }
    }
    if (r.state == RunState::Working) ++m.working;
    if (r.state == RunState::Waiting || r.needs_review()) ++m.waiting;
    if (r.state == RunState::Blocked) ++m.blocked;
    // A task shows up once: through its most recent run.
    if (task_has_run[r.task] && it.lane == Lane::Feito) continue;
    task_has_run[r.task] = true;
    m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
  }

  if (file) {
    for (const auto& t : file->tasks()) {
      if (task_has_run[t.id]) continue;
      Item it;
      it.task = t.id;
      it.title = plain(t.title);
      it.who = t.field("por");
      if (t.state == ' ') {
        it.lane = Lane::Plano;
        auto why = blockers_to_start(*file, t);
        it.ready = why.empty();
        if (!it.ready) {
          std::string deps;
          for (const auto& d : t.depends()) deps += (deps.empty() ? "" : ", ") + d;
          it.detail = deps.empty() ? why.front() : "← " + deps;
        }
      } else if (t.state == '~') {
        it.lane = Lane::Executando;  // in progress on another machine
        it.detail = tr("on another machine");
      } else if (t.state == '?') {
        it.lane = Lane::Portoes;
        it.state = RunState::Waiting;
        it.detail = tr("waiting for a decision");
      } else if (t.state == 'x') {
        it.lane = Lane::Feito;
        it.state = RunState::Done;
      } else {
        continue;
      }
      m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
    }
  }
  // Plan: ready tasks first, in file order.
  std::stable_sort(m.lanes[0].begin(), m.lanes[0].end(), [](const Item& a, const Item& b) { return a.ready > b.ready; });
  return m;
}

void clamp_selection(const Model& model, View& view) {
  view.lane = std::clamp(view.lane, 0, 3);
  int n = static_cast<int>(model.lanes[view.lane].size());
  view.index = n == 0 ? 0 : std::clamp(view.index, 0, n - 1);
}

void render_progress(Screen& sc, const Model& m, const View& v, int64_t now) {
  sc.clear();
  int W = sc.width(), H = sc.height();

  // Header: the project and the machine.
  int x = sc.put(1, 0, m.project, kBold);
  sc.put(1 + x + 1, 0, "· " + m.machine, kDim);
  std::string clock = timeutil::iso8601(now).substr(11, 5) + " UTC";
  sc.put(W - text_width(clock) - 1, 0, clock, kDim);

  // Four lanes.
  const std::string names[4] = {tr("PLAN"), tr("RUNNING"), tr("GATES"), tr("DONE")};
  int gap = 3, left = 1;
  int cw = std::max(12, (W - 2 - gap * 3) / 4);
  int top = 2, bottom = H - 2;
  for (int lane = 0; lane < 4; ++lane) {
    int lx = left + lane * (cw + gap);
    const auto& items = m.lanes[lane];
    int hx = sc.put(lx, top, names[lane], kDim);
    sc.put(lx + hx + 1, top, std::to_string(items.size()), kText);
    sc.fill(lx, top + 1, cw, "─", kLine);

    int y = top + 2;
    for (size_t i = 0; i < items.size() && y < bottom; ++i) {
      const Item& it = items[i];
      bool sel = v.lane == lane && v.index == static_cast<int>(i);
      if (lane == 0 || lane == 3) {
        // One line per task.
        const char* g = lane == 3 ? state_glyph(it.state) : it.ready ? "○" : "◌";
        Style gs = lane == 3 ? state_style(it.state) : it.ready ? kSoft : kLine;
        if (lane == 3 && it.state == RunState::Done) gs = kSoft;
        if (sel) sc.fill(lx, y, cw, " ", with_bg(kText, true));
        sc.put(lx, y, g, with_bg(gs, sel));
        int ix = sc.put(lx + 2, y, it.task, with_bg(it.ready || lane == 3 ? kText : kDim, sel));
        std::string tail = lane == 3 && it.since_ms ? ago(now, it.since_ms) : it.detail;
        int tw = text_width(tail);
        int room = cw - 2 - ix - 1 - (tw ? tw + 1 : 0);
        sc.put(lx + 2 + ix + 1, y, fit(it.title, room), with_bg(it.ready || lane == 3 ? kSoft : kDim, sel));
        if (tw) sc.put(lx + cw - tw, y, tail, with_bg(kDim, sel));
        y += 1;
        continue;
      }
      // A three-line card with the side bar in the state color.
      RunState shown = it.review ? RunState::Waiting : it.state;  // review waits for you
      Style bar = state_style(shown);
      bool breathing = it.state == RunState::Working && it.from_run;
      Style gstyle = breathing && (v.tick / 6) % 2 ? kBlue2 : bar;
      for (int r = 0; r < 3 && y + r < bottom; ++r) {
        if (sel) sc.fill(lx, y + r, cw, " ", with_bg(kText, true));
        sc.put(lx, y + r, "▌", with_bg(bar, sel));
      }
      std::string dur = it.since_ms ? ago(now, it.since_ms) : "";
      sc.put(lx + 2, y, state_glyph(shown), with_bg(gstyle, sel));
      sc.put(lx + 4, y, fit(it.who.empty() ? "—" : it.who, cw - 6 - text_width(dur)), with_bg(kSoft, sel));
      sc.put(lx + cw - text_width(dur), y, dur, with_bg(kDim, sel));
      int tx = sc.put(lx + 2, y + 1, it.task, with_bg(kBold, sel));
      sc.put(lx + 3 + tx, y + 1, fit(it.title, cw - 3 - tx), with_bg(kText, sel));
      Style ds = shown == RunState::Waiting ? kAmber : kDim;
      sc.put(lx + 2, y + 2, fit(it.detail, cw - 2), with_bg(ds, sel));
      y += 4;
    }
    if (items.empty()) sc.put(lx, top + 2, "—", kLine);
  }

  // Status bar: real state only.
  int sy = H - 1;
  int bx = sc.put(0, sy, " APRON ", kBadge) + 1;
  if (m.waiting) bx += sc.put(bx, sy, "◐ " + std::to_string(m.waiting), kAmber) + 2;
  if (m.working) bx += sc.put(bx, sy, "● " + std::to_string(m.working), kBlue) + 2;
  if (m.blocked) bx += sc.put(bx, sy, "⊘ " + std::to_string(m.blocked), kDim) + 2;
  std::string hint = v.help ? tr("←→↑↓ move · enter open · v review · y activity · q quit") : tr("? help");
  sc.put(W - text_width(hint) - 1, sy, hint, kDim);
}

void render_log(Screen& sc, const Project& project, const std::string& run, int scroll, int64_t now) {
  sc.clear();
  auto events = load_events(project.run_file(run));
  RunSummary s = summarize(run, events);
  int W = sc.width(), H = sc.height();
  int x = sc.put(1, 0, s.task, kBold);
  x += 1 + sc.put(2 + x, 0, "· " + s.agent + "@" + s.machine, kSoft);
  x += 1 + sc.put(2 + x, 0, "· " + timeutil::short_duration(s.last_ms - s.started_ms), kDim);
  sc.put(3 + x, 0, std::string(state_glyph(s.state)) + " " + label(s.state), state_style(s.state));
  sc.put(1, 1, run, kLine);
  (void)now;

  int y = 3;
  for (size_t i = static_cast<size_t>(std::max(0, scroll)); i < events.size() && y < H - 1; ++i, ++y) {
    const Event& e = events[i];
    sc.put(1, y, timeutil::short_duration(e.ts_ms - s.started_ms), kDim);
    std::string g = "·", what = tr("step"), msg = e.msg;
    Style gs = kBlue, ls = kSoft, ms = kText;
    switch (e.kind) {
      case Kind::RunStarted: g = "▶"; what = tr("started"); msg = e.extra_str("intent"); break;
      case Kind::Step: break;
      case Kind::Ask: g = "◐"; what = tr("question"); gs = ls = ms = kAmber; break;
      case Kind::Blocked: g = "⊘"; what = tr("no power"); gs = kDim; msg = "(" + e.extra_str("cause") + ") " + msg; break;
      case Kind::Note: g = "›"; what = tr("note"); gs = kDim; ms = kSoft; break;
      case Kind::Check: {
        const json::Value* ok = e.extra_find("ok");
        bool passed = ok && ok->is_bool() && ok->boolean();
        g = passed ? "✓" : "✕";
        gs = passed ? kSoft : kRed;
        what = tr("check");
        msg = e.extra_str("name");
        break;
      }
      case Kind::Review: {
        std::string action = e.extra_str("action");
        g = action == "merged" ? "✓" : action == "returned" ? "↩" : "–";
        what = tr("review");
        gs = action == "returned" ? kAmber : kSoft;
        msg = tr(action == "returned" ? "returned to the agent" : action) + (e.msg.empty() ? "" : " · " + e.msg);
        break;
      }
      case Kind::RunFinished: g = "■"; what = tr("finished"); gs = kText; msg = e.extra_str("result") + (e.msg.empty() ? "" : " · " + e.msg); break;
    }
    sc.put(9, y, g, gs);
    sc.put(11, y, what, ls);
    sc.put(24, y, fit(msg, W - 25), ms);
  }
  if (s.files >= 0 && y + 1 < H - 1) {
    ++y;
    std::string line = trf("scope: {} files, {} outside", {std::to_string(s.files), std::to_string(s.outside.size())});
    sc.put(1, y++, line, s.outside.empty() ? kDim : kAmber);
    for (const auto& f : s.outside)
      if (y < H - 1) sc.put(3, y++, "✕ " + f, kAmber);
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  std::string back = tr("↑↓ scroll · esc back");
  sc.put(W - text_width(back) - 1, H - 1, back, kDim);
}

void render_task(Screen& sc, const Project& project, const std::string& id) {
  sc.clear();
  auto file = TaskFile::load(project.tasks_file());
  const Task* t = file ? file->find(id) : nullptr;
  int W = sc.width(), H = sc.height();
  if (!t) {
    sc.put(1, 0, trf("{} is not in tasks.md", {id}), kDim);
  } else {
    int x = sc.put(1, 0, t->id, kBold);
    sc.put(2 + x, 0, fit(plain(t->title), W - 3 - x), kText);
    sc.put(1, 1, t->section, kLine);
    int y = 3;
    for (const auto& [k, val] : t->fields) {
      if (y >= H - 1) break;
      sc.put(1, y, k, kDim);
      sc.put(12, y, fit(val, W - 13), kText);
      ++y;
    }
    auto why = blockers_to_start(*file, *t);
    if (t->state == ' ') {
      ++y;
      if (why.empty()) sc.put(1, y, "○ " + tr("ready to pick up"), kSoft);
      for (const auto& w : why)
        if (y < H - 1) sc.put(1, y++, "◌ " + w, kDim);
    }
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  std::string back = tr("esc back");
  sc.put(W - text_width(back) - 1, H - 1, back, kDim);
}

void render_tabs(Screen& sc, int active, int review_count, bool activity) {
  std::string names[3] = {tr("Progress"), tr("Review"), tr("Activity")};
  int shown = activity ? 3 : 2;
  if (review_count > 0) names[1] += " " + std::to_string(review_count);
  int total = 0;
  for (int i = 0; i < shown; ++i) total += text_width(names[i]) + 4;
  int x = std::max(20, (sc.width() - total) / 2);
  for (int i = 0; i < shown; ++i) {
    Style s = i == active ? Style{Color::hex(0xe6e8eb), Color::hex(kSelected), false} : kDim;
    if (i == 1 && review_count > 0 && i != active) s = kAmber;
    x += sc.put(x, 0, " " + names[i] + " ", s) + 2;
  }
}

void render_review_list(Screen& sc, const std::vector<RunSummary>& queue, int index, int64_t now) {
  sc.clear();
  int W = sc.width(), H = sc.height();
  int hx = sc.put(1, 2, tr("REVIEW"), kDim);
  sc.put(2 + hx, 2, std::to_string(queue.size()), kText);
  sc.fill(1, 3, W - 2, "─", kLine);
  if (queue.empty()) sc.put(1, 4, tr("nothing to review"), kDim);
  int y = 4;
  for (size_t i = 0; i < queue.size() && y + 1 < H - 1; ++i, y += 3) {
    const RunSummary& r = queue[i];
    bool sel = static_cast<int>(i) == index;
    bool bad = r.checks_failed > 0 || r.state == RunState::Failed;
    for (int k = 0; k < 2; ++k) {
      if (sel) sc.fill(1, y + k, W - 2, " ", with_bg(kText, true));
      sc.put(1, y + k, "▌", with_bg(bad ? kRed : kAmber, sel));
    }
    int x = 3 + sc.put(3, y, r.task, with_bg(kBold, sel));
    x += 1 + sc.put(x + 1, y, r.agent + "@" + r.machine, with_bg(kSoft, sel));
    if (r.checks_ok + r.checks_failed)
      x += 2 + sc.put(x + 2, y, "checks " + std::to_string(r.checks_ok) + "/" + std::to_string(r.checks_ok + r.checks_failed),
                      with_bg(bad ? kRed : kSoft, sel));
    if (!r.outside.empty())
      x += 2 + sc.put(x + 2, y, "✕" + std::to_string(r.outside.size()) + " " + tr("outside"), with_bg(kAmber, sel));
    std::string when = ago(now, r.last_ms);
    sc.put(W - 2 - text_width(when), y, when, with_bg(kDim, sel));
    sc.put(3, y + 1, fit(r.last_msg, W - 5), with_bg(kText, sel));
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  std::string hint = tr("↑↓ move · enter open · esc back · q quit");
  sc.put(W - text_width(hint) - 1, H - 1, hint, kDim);
}

void render_review(Screen& sc, const Project& project, const RunSummary& r, const std::string& stat,
                   const std::string& status, const std::string* input) {
  sc.clear();
  int W = sc.width(), H = sc.height();
  auto events = load_events(project.run_file(r.id));
  int x = sc.put(1, 2, r.task, kBold);
  x += 1 + sc.put(2 + x, 2, "· " + r.agent + "@" + r.machine, kSoft);
  sc.put(3 + x, 2, "· " + timeutil::short_duration(r.last_ms - r.started_ms), kDim);
  sc.put(1, 3, fit(r.last_msg, W - 2), kText);

  // Left: trajectory and checks. Right: scope and changes.
  int mid = W / 2, y = 5;
  sc.put(1, y++, tr("trajectory"), kDim);
  for (const auto& e : events) {
    if (y >= H - 4) break;
    if (e.kind == Kind::Step || e.kind == Kind::Ask || e.kind == Kind::Blocked) {
      const char* g = e.kind == Kind::Ask ? "◐" : e.kind == Kind::Blocked ? "⊘" : "·";
      sc.put(1, y, g, e.kind == Kind::Ask ? kAmber : kBlue);
      sc.put(3, y++, fit(e.msg, mid - 5), kSoft);
    }
  }
  ++y;
  sc.put(1, y++, "checks", kDim);
  for (const auto& e : events) {
    if (e.kind != Kind::Check || y >= H - 3) continue;
    const json::Value* ok = e.extra_find("ok");
    bool passed = ok && ok->is_bool() && ok->boolean();
    sc.put(1, y, passed ? "✓" : "✕", passed ? kSoft : kRed);
    sc.put(3, y++, e.extra_str("name"), kText);
  }

  int ry = 5;
  if (r.files >= 0) {
    std::string line = trf("scope: {} files, {} outside", {std::to_string(r.files), std::to_string(r.outside.size())});
    sc.put(mid, ry++, line, r.outside.empty() ? kDim : kAmber);
    for (const auto& f : r.outside)
      if (ry < H - 4) sc.put(mid + 2, ry++, fit("✕ " + f, W - mid - 3), kAmber);
    ++ry;
  }
  sc.put(mid, ry++, tr("changes"), kDim);
  if (stat.empty()) sc.put(mid, ry++, fit(tr("no commit yet: uncommitted changes since the run started"), W - mid - 1), kDim);
  std::istringstream lines(stat);
  std::string line;
  while (std::getline(lines, line) && ry < H - 3) sc.put(mid, ry++, fit(line, W - mid - 1), kSoft);

  if (!status.empty()) sc.put(1, H - 3, fit(status, W - 2), kAmber);
  if (input) {
    std::string prompt = tr("comment for the agent (enter sends, esc cancels): ");
    int px = sc.put(1, H - 2, prompt, kAmber);
    sc.put(1 + px, H - 2, fit(*input + "▏", W - 2 - px), kText);
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  std::string hint = tr("m merge · r return with a comment · d discard · esc back");
  sc.put(W - text_width(hint) - 1, H - 1, hint, kDim);
}

std::vector<ActivityItem> load_activity(const std::vector<std::string>& roots, size_t limit) {
  std::vector<ActivityItem> out;
  for (const auto& root : roots) {
    Project p{root};
    std::string name = p.root.filename().string();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(p.runs_dir(), ec)) {
      if (entry.path().extension() != ".jsonl") continue;
      for (auto& e : load_events(entry.path())) out.push_back({name, std::move(e)});
    }
  }
  std::sort(out.begin(), out.end(), [](const ActivityItem& a, const ActivityItem& b) { return a.event.ts_ms > b.event.ts_ms; });
  if (out.size() > limit) out.resize(limit);
  return out;
}

void render_activity(Screen& sc, const std::vector<ActivityItem>& items, int scroll, int64_t now) {
  sc.clear();
  int W = sc.width(), H = sc.height();
  if (items.empty()) sc.put(1, 2, tr("no activity yet"), kDim);
  int y = 2;
  for (size_t i = static_cast<size_t>(std::max(0, scroll)); i < items.size() && y < H - 1; ++i, ++y) {
    const Event& e = items[i].event;
    std::string g = "·", msg = e.msg;
    Style gs = kBlue, ms = kSoft;
    switch (e.kind) {
      case Kind::RunStarted: g = "▶"; msg = e.extra_str("intent"); ms = kText; break;
      case Kind::Step: break;
      case Kind::Ask: g = "◐"; gs = ms = kAmber; break;
      case Kind::Blocked: g = "⊘"; gs = kDim; msg = "(" + e.extra_str("cause") + ") " + msg; break;
      case Kind::Note: g = "›"; gs = kDim; ms = kDim; break;
      case Kind::Check: {
        const json::Value* ok = e.extra_find("ok");
        bool passed = ok && ok->is_bool() && ok->boolean();
        g = passed ? "✓" : "✕";
        gs = passed ? kSoft : kRed;
        msg = "check " + e.extra_str("name");
        break;
      }
      case Kind::RunFinished: g = "■"; gs = kText; ms = kText; msg = e.extra_str("result") + (e.msg.empty() ? "" : " · " + e.msg); break;
      case Kind::Review: {
        std::string action = e.extra_str("action");
        g = action == "returned" ? "↩" : action == "merged" ? "✓" : "–";
        gs = action == "returned" ? kAmber : kSoft;
        msg = tr(action == "returned" ? "returned to the agent" : action) + (e.msg.empty() ? "" : " · " + e.msg);
        break;
      }
    }
    std::string when = ago(now, e.ts_ms);
    sc.put(1, y, when, kDim);
    sc.put(8, y, g, gs);
    int x = 10 + sc.put(10, y, fit(items[i].project, 14), kSoft);
    x = std::max(x + 1, 25);
    x += 1 + sc.put(x, y, e.task, kBold);
    x += 1 + sc.put(x, y, fit(e.agent + "@" + e.machine, 18), kDim);
    sc.put(x, y, fit(msg, W - x - 1), ms);
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  std::string hint = tr("↑↓ scroll · esc back");
  sc.put(W - text_width(hint) - 1, H - 1, hint, kDim);
}

}  // namespace apron::tui
