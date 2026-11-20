// apron (no arguments): the screen. A sidebar with projects, sessions and
// machines; the board or the activity of a project; details over the board.
//
// The same keys everywhere: Tab and Shift-Tab change the view, the arrows
// move, Enter opens, Esc goes back one level, q quits. The mouse clicks what
// it points at. The footer lists the keys that work on the current screen.
#include <unistd.h>

#include <algorithm>
#include <filesystem>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "cli/fleet.hpp"
#include "core/review.hpp"
#include "core/time.hpp"
#include "tui/app.hpp"
#include "tui/progress.hpp"
#include "tui/term.hpp"

namespace apron::cli {

namespace {

using tui::Action;
using tui::Button;
using tui::Key;

void pop_utf8(std::string& s) {
  while (!s.empty()) {
    unsigned char c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80) break;  // stop after the first byte of a code point
  }
}

bool has_project(const std::string& root) {
  std::error_code ec;
  return !root.empty() && std::filesystem::is_directory(std::filesystem::path(root) / ".apron", ec);
}

}  // namespace

int cmd_ui(const Args&) {
  if (!isatty(STDOUT_FILENO)) return fail(tr("the screen needs a terminal; outside one, use apron log or apron tasks"));
  std::optional<Project> project = find_project(std::filesystem::current_path());

  tui::Terminal term;
  if (!term.ok()) return fail(tr("could not put the terminal in raw mode"));
  tui::Screen screen;
  screen.resize(term.width(), term.height());

  FleetPoller poller;
  tui::Fleet fleet;
  tui::Model model;
  tui::View view;
  if (project) model = tui::load_model(*project);
  tui::clamp_selection(model, view);

  enum class Overlay { None, Log, Task, Review, Help, Prompt, Confirm } overlay = Overlay::None;
  enum class Ask { NewSession, AddMachine, Return } asking = Ask::NewSession;
  int tab = 0;  // 0 the board, 1 the activity
  bool side = false;
  int side_sel = 0;
  std::vector<ActivityItem> activity;
  int activity_scroll = 0, scroll = 0;
  std::string open_run, open_task, stat, input, status;
  std::string kill_host, new_host, new_cwd, new_name;
  // The board shows a project of another machine when remote_host is set.
  std::string remote_host, remote_root, remote_name;
  uint32_t kill_id = 0;
  int64_t last_load = 0, status_until = 0;
  std::vector<tui::Hit> hits;

  auto say = [&](const std::string& s) {
    status = s;
    status_until = timeutil::now_ms() + 4000;
  };
  auto local_roots = [&] {
    std::vector<std::string> roots;
    if (project) roots.push_back(project->root.string());
    for (const auto& g : fleet.projects)
      if (g.machine.empty() && has_project(g.root) && (!project || g.root != project->root.string()))
        roots.push_back(g.root);
    return roots;
  };
  auto reload = [&] {
    fleet = poller.snapshot();
    if (!remote_host.empty()) {
      if (fleet.has_board && fleet.board_host == remote_host && fleet.board_root == remote_root) model = fleet.board;
    } else if (project) {
      model = tui::load_model(*project);
    }
    tui::clamp_selection(model, view);
    if (tab == 1) activity = load_activity(local_roots(), 500);
    int rows = static_cast<int>(tui::side_rows(fleet).size());
    side_sel = std::clamp(side_sel, 0, std::max(0, rows - 1));
  };
  auto selected_item = [&]() -> const tui::Item* {
    const auto& items = model.lanes[view.lane];
    if (side || items.empty()) return nullptr;
    return &items[static_cast<size_t>(view.index)];
  };
  auto session_live = [&](const std::string& host, const std::string& id) {
    for (const auto& g : fleet.projects)
      if (g.machine == host)
        for (const auto& s : g.sessions)
          if (std::to_string(s.id) == id) return s.state != tui::SessionState::Exited;
    return false;
  };
  auto machine_called = [&](const std::string& host) {
    for (const auto& m : fleet.machines)
      if ((host.empty() && m.here) || (!host.empty() && m.name == host)) return m.machine_name;
    return std::string();
  };
  auto buttons_for = [&](const tui::Item* it) {
    std::vector<Button> b;
    if (!it) return b;
    bool live = !it->session.empty() && it->machine == machine_called(remote_host) &&
                session_live(remote_host, it->session);
    if (!remote_host.empty()) {
      // A project on another machine: its terminal is reachable; its files are not here.
      if (live) b.push_back({it->state == RunState::Waiting ? Action::Answer : Action::Terminal, "enter",
                             it->state == RunState::Waiting ? tr("Answer") : tr("Terminal")});
      return b;
    }
    if (it->review) b.push_back({Action::Review, "", tr("Review")});
    if (live) {
      bool answer = it->state == RunState::Waiting;
      b.push_back({answer ? Action::Answer : Action::Terminal, "t", answer ? tr("Answer") : tr("Terminal")});
    }
    if (!it->run.empty()) b.push_back({Action::Log, "o", tr("Log")});
    else b.push_back({Action::Details, "", tr("Details")});
    b.front().key = "enter";
    return b;
  };
  // Hands the screen to a session and takes it back after Ctrl-].
  auto open_terminal = [&](const std::string& host, uint32_t id) {
    term.suspend();
    int rc = attach_session(host, id);
    term.resume();
    screen.resize(term.width(), term.height());
    poller.poke();
    if (rc != 0) say(tr("the session has ended"));
  };
  auto run_action = [&](Action a, const tui::Item& it) {
    switch (a) {
      case Action::Terminal:
      case Action::Answer:
        open_terminal(remote_host, static_cast<uint32_t>(std::atoi(it.session.c_str())));
        break;
      case Action::Review:
        open_run = it.run;
        stat = run_changes(*project, it.run).stat;
        overlay = Overlay::Review;
        break;
      case Action::Log:
        open_run = it.run;
        scroll = 0;
        overlay = Overlay::Log;
        break;
      case Action::Details:
        open_task = it.task;
        overlay = Overlay::Task;
        break;
    }
  };
  auto start_new_session = [&] {
    // Where: the project picked in the sidebar, else the one on the board.
    new_host = remote_host;
    new_cwd = !remote_host.empty() ? remote_root
              : project          ? project->root.string()
                                 : std::filesystem::current_path().string();
    new_name = !remote_host.empty() ? remote_name + " · " + remote_host
               : project            ? project->root.filename().string()
                                    : new_cwd;
    auto rows = tui::side_rows(fleet);
    if (side && side_sel < static_cast<int>(rows.size()) && rows[static_cast<size_t>(side_sel)].kind != tui::SideRow::Kind::Machine) {
      const auto& g = fleet.projects[static_cast<size_t>(rows[static_cast<size_t>(side_sel)].group)];
      new_host = g.machine;
      new_cwd = !g.root.empty() ? g.root : (!g.sessions.empty() ? g.sessions.front().cwd : new_cwd);
      new_name = g.name + (g.machine.empty() ? "" : " · " + g.machine);
    }
    input = "claude";
    asking = Ask::NewSession;
    overlay = Overlay::Prompt;
  };
  auto activate_side = [&] {
    auto rows = tui::side_rows(fleet);
    if (side_sel >= static_cast<int>(rows.size())) return;
    const auto& row = rows[static_cast<size_t>(side_sel)];
    if (row.kind == tui::SideRow::Kind::Machine) {
      const auto& m = fleet.machines[static_cast<size_t>(row.group)];
      if (!m.error.empty()) say(m.name + ": " + m.error);
      return;
    }
    const auto& g = fleet.projects[static_cast<size_t>(row.group)];
    if (row.kind == tui::SideRow::Kind::Session) {
      const auto& s = g.sessions[static_cast<size_t>(row.item)];
      if (s.state == tui::SessionState::Exited) say(tr("the session has ended"));
      else open_terminal(s.machine, s.id);
      return;
    }
    if (!g.machine.empty() && !g.root.empty()) {
      remote_host = g.machine;
      remote_root = g.root;
      remote_name = g.name;
      poller.watch(remote_host, remote_root);
      model = {};
      view = {};
      side = false;
      overlay = Overlay::None;
    } else if (!g.machine.empty()) {
      say(tr("this folder has no .apron/ yet"));
    } else if (has_project(g.root)) {
      remote_host.clear();
      poller.watch("", "");
      project = Project{g.root};
      model = tui::load_model(*project);
      view = {};
      tui::clamp_selection(model, view);
      side = false;
      overlay = Overlay::None;
    } else {
      say(tr("this folder has no .apron/ yet"));
    }
  };
  auto ask_kill = [&] {
    auto rows = tui::side_rows(fleet);
    if (!side || side_sel >= static_cast<int>(rows.size())) return;
    const auto& row = rows[static_cast<size_t>(side_sel)];
    if (row.kind != tui::SideRow::Kind::Session) return;
    const auto& s = fleet.projects[static_cast<size_t>(row.group)].sessions[static_cast<size_t>(row.item)];
    kill_host = s.machine;
    kill_id = s.id;
    input = s.machine.empty() ? trf("End session {} ({})?", {std::to_string(s.id), s.what})
                              : trf("End session {} ({}) on {}?", {std::to_string(s.id), s.what, s.machine});
    overlay = Overlay::Confirm;
  };
  auto set_tab = [&](int t) {
    tab = (t + 2) % 2;
    overlay = Overlay::None;
    if (tab == 1) {
      activity = load_activity(local_roots(), 500);
      activity_scroll = 0;
    }
  };

  while (true) {
    int64_t now = timeutil::now_ms();
    if (now - last_load >= 1000) {
      reload();
      last_load = now;
    }
    if (!status.empty() && now > status_until) status.clear();

    // --- Draw ---------------------------------------------------------------
    screen.clear();
    hits.clear();
    int W = screen.width(), H = screen.height();
    int SW = tui::sidebar_width(W);
    if (SW == 0) side = false;
    if (SW) tui::render_sidebar(screen, {0, 0, SW, H - 1}, fleet, side_sel, side, now, hits);
    tui::Rect main_r{SW, 0, W - SW, H - 1};
    std::string title = !remote_host.empty() ? remote_name + " · " + remote_host
                        : project            ? project->root.filename().string()
                                             : tr("no project here");
    if (overlay == Overlay::Log || overlay == Overlay::Review) title += " · " + open_run;
    if (overlay == Overlay::Task) title += " · " + open_task;
    tui::Rect in = tui::render_main_frame(screen, main_r, title, {tr("Progress"), tr("Activity")}, tab, !side, hits);
    const tui::Item* sel = selected_item();
    std::vector<Button> buttons = buttons_for(sel);
    tui::Screen sub;
    sub.resize(std::max(1, in.w), std::max(1, in.h));
    bool drew_sub = true;
    if (overlay == Overlay::Log && project) tui::render_log(sub, *project, open_run, scroll, now);
    else if (overlay == Overlay::Task && project) tui::render_task(sub, *project, open_task);
    else if (overlay == Overlay::Review && project)
      tui::render_review(sub, *project, summarize(open_run, load_events(project->run_file(open_run))), stat);
    else if (tab == 1) tui::render_activity(sub, activity, activity_scroll, now);
    else drew_sub = false;
    if (drew_sub) screen.blit(sub, in.x, in.y);
    else if (project || !remote_host.empty()) tui::render_board(screen, in, model, view, !side, buttons, now, hits);
    else screen.put(in.x, in.y + 1, tr("No project in this folder. Pick one in the sidebar."), {});

    if (overlay == Overlay::Help) tui::render_help(screen, main_r);
    if (overlay == Overlay::Prompt) {
      std::string what = asking == Ask::NewSession   ? trf("New session in {}", {new_name})
                         : asking == Ask::AddMachine ? tr("Add a machine: its ssh host")
                                                     : tr("Comment for the agent");
      tui::render_prompt(screen, main_r, what, input, true);
    }
    if (overlay == Overlay::Confirm) tui::render_prompt(screen, main_r, tr("End the session"), input, false);

    std::vector<std::pair<std::string, std::string>> keys;
    switch (overlay) {
      case Overlay::Prompt:
      case Overlay::Confirm: keys = {{"enter", tr("confirm")}, {"esc", tr("cancel")}}; break;
      case Overlay::Help: keys = {{"esc", tr("close")}}; break;
      case Overlay::Review: {
        keys = {{"m", tr("merge")}, {"r", tr("return with a comment")}, {"d", tr("discard")}};
        if (sel)
          for (const auto& b : buttons)
            if (b.action == Action::Terminal || b.action == Action::Answer) keys.push_back({"t", b.label});
        keys.push_back({"o", tr("log")});
        keys.push_back({"esc", tr("back")});
        break;
      }
      case Overlay::Log:
      case Overlay::Task: keys = {{"↑↓", tr("scroll")}, {"esc", tr("back")}}; break;
      case Overlay::None:
        if (side) {
          keys = {{"enter", tr("open")}, {"x", tr("end session")}, {"→", tr("board")}};
        } else if (tab == 1) {
          keys = {{"↑↓", tr("scroll")}};
        } else {
          for (const auto& b : buttons) {
            std::string l = b.label;
            if (!l.empty() && l[0] >= 'A' && l[0] <= 'Z') l[0] = static_cast<char>(l[0] - 'A' + 'a');
            keys.push_back({b.key, l});
          }
        }
        keys.push_back({"tab", tr("next view")});
        keys.push_back({"n", tr("new session")});
        keys.push_back({"?", tr("keys")});
        keys.push_back({"q", tr("quit")});
        break;
    }
    tui::render_footer(screen, keys, status);
    term.write(screen.flush());

    // --- Input --------------------------------------------------------------
    tui::KeyEvent k = term.poll(100);
    ++view.tick;
    if (k.key == Key::None) continue;
    if (k.key == Key::Resize) {
      screen.resize(term.width(), term.height());
      continue;
    }

    if (overlay == Overlay::Prompt) {
      if (k.key == Key::Esc) overlay = Overlay::None;
      else if (k.key == Key::Backspace) pop_utf8(input);
      else if (k.key == Key::Char && static_cast<unsigned char>(k.ch) >= 0x20) input += k.text;
      else if (k.key == Key::Enter) {
        overlay = Overlay::None;
        if (asking == Ask::NewSession) {
          std::string err;
          uint32_t id = spawn_session(new_host, new_cwd, input.empty() ? "" : input + "\r", &err);
          if (id) open_terminal(new_host, id);
          else say(err.empty() ? tr("could not start the session") : err);
        } else if (asking == Ask::AddMachine) {
          std::string err = add_machine(input);
          if (err.empty()) say(trf("{} added; connecting", {input}));
          else say(err);
          poller.poke();
        } else {
          std::string err = review_return(*project, open_run, input);
          if (err.empty()) say(tr("returned to the agent"));
          else say(err);
          reload();
        }
      }
      continue;
    }
    if (overlay == Overlay::Confirm) {
      if (k.key == Key::Enter) {
        std::string err;
        if (!kill_session(kill_host, kill_id, &err)) say(err);
        poller.poke();
      }
      if (k.key == Key::Enter || k.key == Key::Esc) overlay = Overlay::None;
      continue;
    }

    // Mouse.
    if (k.key == Key::Click) {
      const tui::Hit* h = tui::hit_at(hits, k.x, k.y);
      if (!h || overlay == Overlay::Help) {
        if (overlay == Overlay::Help) overlay = Overlay::None;
        continue;
      }
      switch (h->act) {
        case tui::Act::Tab: set_tab(h->a); break;
        case tui::Act::Side:
          if (side && side_sel == h->a) activate_side();
          side = true;
          side_sel = h->a;
          break;
        case tui::Act::Card:
          if (!side && view.lane == h->a && view.index == h->b && sel) run_action(buttons.front().action, *sel);
          side = false;
          view.lane = h->a;
          view.index = h->b;
          break;
        case tui::Act::Button:
          if (sel) run_action(static_cast<Action>(h->a), *sel);
          break;
        case tui::Act::NewSession: start_new_session(); break;
        case tui::Act::AddMachine:
          input.clear();
          asking = Ask::AddMachine;
          overlay = Overlay::Prompt;
          break;
        case tui::Act::None: break;
      }
      continue;
    }

    char c = k.key == Key::Char ? k.ch : 0;
    if (k.key == Key::Tab) {
      set_tab(tab + 1);
      continue;
    }
    if (k.key == Key::BackTab) {
      set_tab(tab - 1);
      continue;
    }
    if (k.key == Key::Esc) {
      if (overlay != Overlay::None) overlay = Overlay::None;
      else side = false;
      continue;
    }
    if (c == 'q') return 0;
    if (c == '?') {
      overlay = overlay == Overlay::Help ? Overlay::None : Overlay::Help;
      continue;
    }
    if (overlay == Overlay::Help) continue;
    bool up = k.key == Key::Up || k.key == Key::WheelUp || c == 'k';
    bool down = k.key == Key::Down || k.key == Key::WheelDown || c == 'j';
    bool left = k.key == Key::Left || c == 'h';
    bool right = k.key == Key::Right || c == 'l';

    if (overlay == Overlay::Log || overlay == Overlay::Task) {
      if (up) scroll = std::max(0, scroll - 1);
      if (down) ++scroll;
      continue;
    }
    if (overlay == Overlay::Review) {
      std::string err;
      if (c == 'm') err = review_merge(*project, open_run);
      else if (c == 'd') err = review_discard(*project, open_run);
      else if (c == 'r') {
        input.clear();
        asking = Ask::Return;
        overlay = Overlay::Prompt;
        continue;
      } else if (c == 't' && sel) {
        for (const auto& b : buttons)
          if (b.action == Action::Terminal || b.action == Action::Answer) run_action(b.action, *sel);
        continue;
      } else if (c == 'o') {
        scroll = 0;
        overlay = Overlay::Log;
        continue;
      } else {
        continue;
      }
      if (err.empty()) {
        say(c == 'm' ? tr("merged into main") : tr("discarded; the task is open again"));
        overlay = Overlay::None;
        reload();
      } else {
        say(err);
      }
      continue;
    }
    if (c == 'n') {
      start_new_session();
      continue;
    }
    if (c == '+') {
      input.clear();
      asking = Ask::AddMachine;
      overlay = Overlay::Prompt;
      continue;
    }
    if (tab == 1) {
      if (up) activity_scroll = std::max(0, activity_scroll - 1);
      if (down) ++activity_scroll;
      if (left && SW) side = true;
      if (right) side = false;
      continue;
    }
    if (side) {
      int rows = static_cast<int>(tui::side_rows(fleet).size());
      if (up) side_sel = std::max(0, side_sel - 1);
      if (down) side_sel = std::min(std::max(0, rows - 1), side_sel + 1);
      if (right) side = false;
      if (k.key == Key::Enter) activate_side();
      if (c == 'x') ask_kill();
      continue;
    }
    // The board.
    if (left && view.lane == 0 && SW) {
      side = true;
      continue;
    }
    if (left) --view.lane, view.index = 0;
    if (right) ++view.lane, view.index = 0;
    if (up) --view.index;
    if (down) ++view.index;
    tui::clamp_selection(model, view);
    if (!sel) continue;
    if (k.key == Key::Enter) run_action(buttons.front().action, *sel);
    for (const auto& b : buttons)
      if (b.key.size() == 1 && c == b.key[0]) run_action(b.action, *sel);
  }
}

}  // namespace apron::cli
