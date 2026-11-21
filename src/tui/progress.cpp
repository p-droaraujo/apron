#include "tui/progress.hpp"

#include <algorithm>
#include <map>
#include <sstream>

#include "core/i18n.hpp"
#include "core/review.hpp"
#include "core/tasks.hpp"
#include "core/time.hpp"
#include "tui/palette.hpp"

namespace apron::tui {

void clamp_selection(const Model& model, View& view) {
  view.lane = std::clamp(view.lane, 0, 3);
  int n = static_cast<int>(model.lanes[view.lane].size());
  view.index = n == 0 ? 0 : std::clamp(view.index, 0, n - 1);
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
      case Kind::Blocked: g = "⊘"; what = tr("no power"); gs = kDim; break;
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
}

void render_review(Screen& sc, const Project& project, const RunSummary& r, const std::string& stat) {
  sc.clear();
  int W = sc.width(), H = sc.height();
  auto events = load_events(project.run_file(r.id));
  int x = sc.put(1, 0, r.task, kBold);
  x += 1 + sc.put(2 + x, 0, "· " + r.agent + "@" + r.machine, kSoft);
  sc.put(3 + x, 0, "· " + timeutil::short_duration(r.last_ms - r.started_ms), kDim);
  sc.put(1, 1, fit(r.last_msg, W - 2), kText);

  // Left: trajectory and checks. Right: scope and changes.
  int mid = W / 2, y = 3;
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

  int ry = 3;
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

}

void render_activity(Screen& sc, const std::vector<ActivityItem>& items, int scroll, int64_t now) {
  sc.clear();
  int W = sc.width(), H = sc.height();
  if (items.empty()) sc.put(1, 0, tr("no activity yet"), kDim);
  // The bottom line explains the glyphs.
  const std::pair<const char*, const char*> legend[] = {
      {"▶", "started"}, {"·", "step"}, {"◐", "asks"}, {"⊘", "blocked"}, {"›", "note"},
      {"✓", "check passed"}, {"✕", "check failed"}, {"■", "finished"}, {"↩", "returned"},
  };
  int lx = 1;
  for (const auto& [g, what] : legend) {
    std::string label = tr(what);
    if (lx + 2 + text_width(label) > W) break;
    lx += sc.put(lx, H - 1, g, kSoft) + 1;
    lx += sc.put(lx, H - 1, label, kDim) + 3;
  }
  int y = 0;
  for (size_t i = static_cast<size_t>(std::max(0, scroll)); i < items.size() && y < H - 2; ++i, ++y) {
    const Event& e = items[i].event;
    std::string g = "·", msg = e.msg;
    Style gs = kBlue, ms = kSoft;
    switch (e.kind) {
      case Kind::RunStarted: g = "▶"; msg = e.extra_str("intent"); ms = kText; break;
      case Kind::Step: break;
      case Kind::Ask: g = "◐"; gs = ms = kAmber; break;
      case Kind::Blocked: g = "⊘"; gs = kDim; break;
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
    int x = 10;
    x += 1 + sc.put(x, y, e.task, kBold);
    x += 1 + sc.put(x, y, fit(e.agent + "@" + e.machine, 18), kDim);
    sc.put(x, y, fit(msg, W - x - 1), ms);
  }
}

}  // namespace apron::tui
