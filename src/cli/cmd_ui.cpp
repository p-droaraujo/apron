// apron (no arguments): the progress view and the Review tab in the terminal.
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "core/review.hpp"
#include "core/time.hpp"
#include "tui/progress.hpp"
#include "tui/term.hpp"

namespace apron::cli {

namespace {

// Every project this machine knows: aprond's registry, else just this one.
std::vector<std::string> machine_projects(const Project& here) {
  std::vector<std::string> roots{here.root.string()};
  DaemonClient c;
  std::string err;
  if (!c.connect(false, &err)) return roots;
  json::Object req;
  req.emplace_back("cmd", "projects.list");
  auto reply = c.request(std::move(req), &err);
  if (const json::Value* list = reply ? reply->find("projects") : nullptr)
    for (const auto& p : list->arr()) {
      std::string root = p.get_str("root");
      if (!root.empty() && root != roots.front()) roots.push_back(root);
    }
  return roots;
}

void pop_utf8(std::string& s) {
  while (!s.empty()) {
    unsigned char c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80) break;  // stop after the first byte of a code point
  }
}

}  // namespace

int cmd_ui(const Args&) {
  auto project = require_project();
  if (!project) return 1;
  if (!isatty(STDOUT_FILENO)) return fail(tr("the progress view needs a terminal; outside one, use apron runs or apron tasks"));

  tui::Terminal term;
  if (!term.ok()) return fail(tr("could not put the terminal in raw mode"));

  tui::Screen screen;
  screen.resize(term.width(), term.height());
  tui::Model model = tui::load_model(*project);
  std::vector<RunSummary> queue = review_queue(*project);
  tui::View view;
  tui::clamp_selection(model, view);

  enum class Mode { Progress, Log, Task, Review, ReviewRun, Activity } mode = Mode::Progress;
  std::vector<ActivityItem> activity;
  int activity_scroll = 0;
  std::string open_run, open_task, stat, status, input;
  bool typing = false;
  int scroll = 0, review_index = 0;
  int64_t last_load = timeutil::now_ms();

  auto reload = [&] {
    if (mode == Mode::Activity) activity = load_activity(machine_projects(*project), 500);
    model = tui::load_model(*project);
    queue = review_queue(*project);
    tui::clamp_selection(model, view);
    review_index = std::clamp(review_index, 0, std::max(0, static_cast<int>(queue.size()) - 1));
  };
  auto open_review = [&](const std::string& run) {
    open_run = run;
    stat = run_changes(*project, run).stat;
    status.clear();
    mode = Mode::ReviewRun;
  };

  // ~10 frames per second; data reloads every second. A frame with no
  // change sends nothing to the terminal.
  while (true) {
    int64_t now = timeutil::now_ms();
    if (now - last_load >= 1000) {
      reload();
      last_load = now;
    }
    int tab = 0;
    switch (mode) {
      case Mode::Progress: tui::render_progress(screen, model, view, now); break;
      case Mode::Log: tui::render_log(screen, *project, open_run, scroll, now); break;
      case Mode::Task: tui::render_task(screen, *project, open_task); break;
      case Mode::Review:
        tab = 1;
        tui::render_review_list(screen, queue, review_index, now);
        break;
      case Mode::Activity:
        tab = 2;
        tui::render_activity(screen, activity, activity_scroll, now);
        break;
      case Mode::ReviewRun: {
        tab = 1;
        auto it = std::find_if(queue.begin(), queue.end(), [&](const RunSummary& r) { return r.id == open_run; });
        RunSummary r = it != queue.end() ? *it : summarize(open_run, load_events(project->run_file(open_run)));
        tui::render_review(screen, *project, r, stat, status, typing ? &input : nullptr);
        break;
      }
    }
    if (mode == Mode::Progress || mode == Mode::Review || mode == Mode::ReviewRun || mode == Mode::Activity)
      tui::render_tabs(screen, tab, static_cast<int>(queue.size()), true);
    term.write(screen.flush());

    tui::KeyEvent k = term.poll(100);
    ++view.tick;
    if (k.key == tui::Key::Resize) {
      screen.resize(term.width(), term.height());
      continue;
    }

    // Typing a comment for the agent.
    if (typing) {
      if (k.key == tui::Key::Esc) typing = false;
      else if (k.key == tui::Key::Backspace) pop_utf8(input);
      else if (k.key == tui::Key::Char && static_cast<unsigned char>(k.ch) >= 0x20) input += k.text;
      else if (k.key == tui::Key::Enter) {
        std::string err = review_return(*project, open_run, input);
        typing = false;
        if (err.empty()) {
          reload();
          mode = Mode::Review;
        } else {
          status = err;
        }
      }
      continue;
    }

    bool quit = k.key == tui::Key::Char && k.ch == 'q';
    // Tabs: v Review, y Activity, Esc back to Progress.
    bool on_tab = mode == Mode::Progress || mode == Mode::Review || mode == Mode::Activity;
    if (on_tab && k.key == tui::Key::Char && k.ch == 'v') {
      mode = Mode::Review;
      continue;
    }
    if (on_tab && k.key == tui::Key::Char && k.ch == 'y') {
      mode = Mode::Activity;
      activity_scroll = 0;
      activity = load_activity(machine_projects(*project), 500);
      continue;
    }
    switch (mode) {
      case Mode::Progress: {
        if (quit) return 0;
        switch (k.key) {
          case tui::Key::Left: --view.lane, view.index = 0; break;
          case tui::Key::Right:
          case tui::Key::Tab: view.lane = (view.lane + 1) % 4, view.index = 0; break;
          case tui::Key::Up: --view.index; break;
          case tui::Key::Down: ++view.index; break;
          case tui::Key::Enter: {
            const auto& items = model.lanes[view.lane];
            if (items.empty()) break;
            const auto& it = items[static_cast<size_t>(view.index)];
            if (it.review) open_review(it.run);
            else if (!it.session.empty() && it.state != RunState::Done && it.machine == machine_name()) {
              // The agent's terminal: hand the screen over, take it back after Ctrl-].
              term.suspend();
              int rc = attach_session(static_cast<uint32_t>(std::atoi(it.session.c_str())));
              term.resume();
              screen.resize(term.width(), term.height());
              if (rc != 0) {
                open_run = it.run;  // the session is gone: show the run instead
                scroll = 0;
                mode = Mode::Log;
              }
            } else if (!it.run.empty()) {
              open_run = it.run;
              scroll = 0;
              mode = Mode::Log;
            } else {
              open_task = it.task;
              mode = Mode::Task;
            }
            break;
          }
          case tui::Key::Char:
            if (k.ch == 'h') --view.lane, view.index = 0;
            else if (k.ch == 'l') ++view.lane, view.index = 0;
            else if (k.ch == 'k') --view.index;
            else if (k.ch == 'j') ++view.index;
            else if (k.ch == '?') view.help = !view.help;
            else if (k.ch == 'o') {  // the run's timeline, even when Enter would attach
              const auto& items = model.lanes[view.lane];
              if (!items.empty() && !items[static_cast<size_t>(view.index)].run.empty()) {
                open_run = items[static_cast<size_t>(view.index)].run;
                scroll = 0;
                mode = Mode::Log;
              }
            }
            break;
          default: break;
        }
        tui::clamp_selection(model, view);
        break;
      }
      case Mode::Log:
      case Mode::Task:
        if (quit || k.key == tui::Key::Esc) mode = Mode::Progress;
        else if (k.key == tui::Key::Up || (k.key == tui::Key::Char && k.ch == 'k')) scroll = std::max(0, scroll - 1);
        else if (k.key == tui::Key::Down || (k.key == tui::Key::Char && k.ch == 'j')) ++scroll;
        break;
      case Mode::Activity:
        if (quit) return 0;
        if (k.key == tui::Key::Esc) mode = Mode::Progress;
        else if (k.key == tui::Key::Up || (k.key == tui::Key::Char && k.ch == 'k')) activity_scroll = std::max(0, activity_scroll - 1);
        else if (k.key == tui::Key::Down || (k.key == tui::Key::Char && k.ch == 'j')) ++activity_scroll;
        break;
      case Mode::Review:
        if (quit) return 0;
        if (k.key == tui::Key::Esc) mode = Mode::Progress;
        else if (k.key == tui::Key::Up || (k.key == tui::Key::Char && k.ch == 'k')) review_index = std::max(0, review_index - 1);
        else if (k.key == tui::Key::Down || (k.key == tui::Key::Char && k.ch == 'j'))
          review_index = std::min(static_cast<int>(queue.size()) - 1, review_index + 1);
        else if (k.key == tui::Key::Enter && !queue.empty()) open_review(queue[static_cast<size_t>(review_index)].id);
        break;
      case Mode::ReviewRun: {
        if (quit || k.key == tui::Key::Esc) {
          mode = Mode::Review;
          break;
        }
        if (k.key != tui::Key::Char) break;
        std::string err;
        if (k.ch == 'm') err = review_merge(*project, open_run);
        else if (k.ch == 'd') err = review_discard(*project, open_run);
        else if (k.ch == 'r') {
          typing = true;
          input.clear();
          break;
        } else {
          break;
        }
        if (err.empty()) {
          reload();
          mode = Mode::Review;
        } else {
          status = err;
        }
        break;
      }
    }
  }
}

}  // namespace apron::cli
