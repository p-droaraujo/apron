// apron (no arguments): the screen. A sidebar with projects, sessions and
// machines; the board or the activity of a project; details over the board.
//
// The same keys everywhere: Tab and Shift-Tab change the view, the arrows
// move, Enter opens, Esc goes back one level, q quits. The mouse clicks what
// it points at. The footer lists the keys that work on the current screen.
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "cli/fleet.hpp"
#include "core/review.hpp"
#include "core/time.hpp"
#include "tui/app.hpp"
#include "tui/progress.hpp"
#include "tui/term.hpp"
#include "tui/vt.hpp"

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

// An agent's terminal shown in the main pane: a connection to its session
// and a small terminal emulator fed by it.
struct Focus {
  std::string host;
  uint32_t id = 0;
  DaemonClient client;
  tui::Vt vt;
};

// What the arrows send depends on the mode the program asked for.
std::string translate_keys(const std::string& raw, bool app_cursor) {
  if (!app_cursor || raw.size() != 3 || raw[0] != '\x1b' || raw[1] != '[') return raw;
  if (raw[2] >= 'A' && raw[2] <= 'D') return std::string("\x1bO") + raw[2];
  return raw;
}

bool is_detach(const std::string& raw) {
  return raw == "\x1d" || raw == "\x1b[93;5u" || raw == "\x1b[27;5;93~";
}

// A session watched in the grid: its terminal drawn at the size it has.
struct Watched {
  std::string host;
  uint32_t id = 0;
  std::string title;
  std::unique_ptr<tui::Vt> vt;
};

bool has_project(const std::string& root) {
  std::error_code ec;
  return !root.empty() && std::filesystem::is_directory(std::filesystem::path(root) / ".apron", ec);
}

}  // namespace

int cmd_ui(const Args&) {
  if (!isatty(STDOUT_FILENO)) return fail(tr("the screen needs a terminal"));
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

  enum class Overlay { None, Log, Task, Review, Help, Prompt, Confirm, Focus } overlay = Overlay::None;
  std::unique_ptr<Focus> focus;
  enum class Ask { NewSession, AddMachine, Return } asking = Ask::NewSession;
  // The views Tab cycles through.
  enum { kBoard = 0, kGrid = 1, kActivity = 2, kViews = 3 };
  int tab = kBoard;
  std::map<std::string, std::unique_ptr<DaemonClient>> grid_clients;  // one connection per machine
  std::vector<Watched> grid;
  int grid_sel = 0;
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
  tui::Rect in_last;

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
    if (tab == kActivity) activity = load_activity(local_roots(), 500);
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
      if (live) b.push_back({it->state == RunState::Waiting ? Action::Answer : Action::Terminal, "t",
                             it->state == RunState::Waiting ? tr("Answer") : tr("Terminal")});
      return b;
    }
    if (it->review) b.push_back({Action::Review, "r", tr("Review")});
    if (live) {
      bool answer = it->state == RunState::Waiting;
      b.push_back({answer ? Action::Answer : Action::Terminal, "t", answer ? tr("Answer") : tr("Terminal")});
    }
    if (!it->run.empty()) b.push_back({Action::Log, "o", tr("Log")});
    else b.push_back({Action::Details, "d", tr("Details")});
    return b;
  };
  // The agent's terminal in the main pane, with the sidebar still in view.
  auto close_focus = [&] {
    if (focus) {
      json::Object req;
      req.emplace_back("cmd", "sessions.detach");
      req.emplace_back("session", static_cast<long long>(focus->id));
      focus->client.send({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(req)))});
    }
    focus.reset();
    if (overlay == Overlay::Focus) overlay = Overlay::None;
  };
  auto drain_focus = [&] {
    if (!focus) return;
    while (auto f = focus->client.next(0)) {
      if (f->type == proto::Type::Data && f->channel == focus->id) {
        focus->vt.feed(f->payload);
      } else if (f->type == proto::Type::Ctrl) {
        auto v = json::parse(f->payload);
        if (v && v->get_str("event") == "session.exited") {
          say(tr("the session has ended"));
          close_focus();
          poller.poke();
          return;
        }
      }
    }
    if (std::string r = focus->vt.take_replies(); !r.empty()) focus->client.send({proto::Type::Data, focus->id, r});
  };
  auto open_terminal = [&](const std::string& host, uint32_t id) {
    close_focus();
    auto f = std::make_unique<Focus>();
    f->host = host;
    f->id = id;
    std::string err;
    bool ok;
    if (host.empty()) {
      ok = f->client.connect(true, &err);
    } else {
      auto all = load_machines();
      const Machine* m = find_machine(all, host);
      ok = f->client.connect_ssh(host, m ? m->remote : "apron", &err);
    }
    if (!ok) {
      say(err);
      return;
    }
    // The session takes the pane's size, so the agent draws for what shows.
    int SW = tui::sidebar_width(screen.width());
    int cols = std::max(20, screen.width() - SW - 4), rows = std::max(5, screen.height() - 3);
    f->vt.resize(cols, rows);
    json::Object req;
    req.emplace_back("cmd", "sessions.attach");
    req.emplace_back("session", static_cast<long long>(id));
    req.emplace_back("cols", cols);
    req.emplace_back("rows", rows);
    if (!f->client.request(std::move(req), &err)) {
      say(err == "not_found" ? tr("the session has ended") : err);
      return;
    }
    focus = std::move(f);
    overlay = Overlay::Focus;
    side = false;
    drain_focus();
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
    if (row.kind == tui::SideRow::Kind::Machine) {
      const auto& m = fleet.machines[static_cast<size_t>(row.group)];
      if (m.here) return;
      kill_host = m.name;
      kill_id = 0;  // 0: remove the machine from the list
      input = trf("Remove {} from the machines?", {m.name});
      overlay = Overlay::Confirm;
      return;
    }
    if (row.kind != tui::SideRow::Kind::Session) return;
    const auto& s = fleet.projects[static_cast<size_t>(row.group)].sessions[static_cast<size_t>(row.item)];
    kill_host = s.machine;
    kill_id = s.id;
    input = s.machine.empty() ? trf("End session {} ({})?", {std::to_string(s.id), s.what})
                              : trf("End session {} ({}) on {}?", {std::to_string(s.id), s.what, s.machine});
    overlay = Overlay::Confirm;
  };
  // The grid watches every live session without resizing it: one connection
  // per machine, every session attached on it, each drawn at its own size.
  auto leave_grid = [&] {
    for (auto& w : grid) {
      auto c = grid_clients.find(w.host);
      if (c == grid_clients.end()) continue;
      json::Object req;
      req.emplace_back("cmd", "sessions.detach");
      req.emplace_back("session", static_cast<long long>(w.id));
      c->second->send({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(req)))});
    }
    grid.clear();
    grid_clients.clear();
  };
  auto sync_grid = [&] {
    std::vector<std::tuple<std::string, uint32_t, std::string, int, int>> live;
    std::map<std::string, bool> reachable;
    for (const auto& m : fleet.machines) reachable[m.here ? "" : m.name] = m.ok || m.here;
    for (const auto& g : fleet.projects)
      for (const auto& s : g.sessions)
        if (s.state != tui::SessionState::Exited && reachable[s.machine])
          live.emplace_back(s.machine, s.id, s.what + (s.machine.empty() ? "" : "@" + s.machine) + " · " + g.name,
                            s.cols, s.rows);
    std::erase_if(grid, [&](const Watched& w) {
      for (const auto& [h, id, t, c, r] : live)
        if (h == w.host && id == w.id) return false;
      return true;
    });
    for (const auto& [host, id, title, cols, rows] : live) {
      bool known = false;
      for (auto& w : grid)
        if (w.host == host && w.id == id) known = true, w.title = title;
      if (known) continue;
      auto& client = grid_clients[host];
      if (!client) {
        auto c = std::make_unique<DaemonClient>();
        std::string err;
        bool ok;
        if (host.empty()) {
          ok = c->connect(true, &err);
        } else {
          auto all = load_machines();
          const Machine* m = find_machine(all, host);
          ok = c->connect_ssh(host, m ? m->remote : "apron", &err);
        }
        if (!ok) {
          grid_clients.erase(host);
          continue;
        }
        client = std::move(c);
      }
      json::Object req;
      req.emplace_back("cmd", "sessions.attach");
      req.emplace_back("session", static_cast<long long>(id));
      std::string err;
      if (!client->request(std::move(req), &err)) continue;
      grid.push_back({host, id, title, std::make_unique<tui::Vt>(cols, rows)});
    }
    grid_sel = std::clamp(grid_sel, 0, std::max(0, static_cast<int>(grid.size()) - 1));
  };
  auto drain_grid = [&] {
    for (auto& [host, client] : grid_clients)
      while (auto f = client->next(0)) {
        if (f->type != proto::Type::Data) continue;
        for (auto& w : grid)
          if (w.host == host && w.id == f->channel) {
            w.vt->feed(f->payload);
            w.vt->take_replies();  // only the terminal the agent draws for answers its questions
          }
      }
  };
  auto set_tab = [&](int t) {
    tab = (t + kViews) % kViews;
    if (tab == kGrid) sync_grid();
    overlay = Overlay::None;
    if (tab != kGrid) leave_grid();
    if (tab == kActivity) {
      activity = load_activity(local_roots(), 500);
      activity_scroll = 0;
    }
  };

  while (true) {
    int64_t now = timeutil::now_ms();
    if (now - last_load >= 1000) {
      reload();
      if (tab == kGrid && overlay != Overlay::Focus) sync_grid();
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
    if (overlay == Overlay::Focus && focus) {
      title = trf("session {}", {std::to_string(focus->id)});
      for (const auto& g : fleet.projects)
        for (const auto& s : g.sessions)
          if (s.id == focus->id && s.machine == focus->host)
            title = s.what + (focus->host.empty() ? "" : "@" + focus->host) + " · " + g.name;
    }
    tui::Rect in = tui::render_main_frame(screen, main_r, title, {tr("Progress"), tr("Agents"), tr("Activity")}, tab, !side, hits);
    const tui::Item* sel = selected_item();
    std::vector<Button> buttons = buttons_for(sel);
    tui::Screen sub;
    sub.resize(std::max(1, in.w), std::max(1, in.h));
    bool drew_sub = true;
    if (overlay == Overlay::Log && project) tui::render_log(sub, *project, open_run, scroll, now);
    else if (overlay == Overlay::Task && project) tui::render_task(sub, *project, open_task);
    else if (overlay == Overlay::Review && project)
      tui::render_review(sub, *project, summarize(open_run, load_events(project->run_file(open_run))), stat);
    else if (tab == kActivity) tui::render_activity(sub, activity, activity_scroll, now);
    else drew_sub = false;
    bool show_grid = !drew_sub && tab == kGrid && overlay != Overlay::Focus;
    if (overlay == Overlay::Focus && focus) {
      drew_sub = true;
      if (focus->vt.cols() != in.w || focus->vt.rows() != in.h) {
        focus->vt.resize(in.w, in.h);
        json::Object r;
        r.emplace_back("cmd", "sessions.resize");
        r.emplace_back("session", static_cast<long long>(focus->id));
        r.emplace_back("cols", in.w);
        r.emplace_back("rows", in.h);
        focus->client.send({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(r)))});
      }
      focus->vt.draw(screen, in.x, in.y, in.w, in.h, true);
    } else if (drew_sub) {
      screen.blit(sub, in.x, in.y);
    } else if (show_grid) {
      std::vector<tui::Tile> tiles;
      for (const auto& w : grid) tiles.push_back({w.title, w.vt.get()});
      tui::render_grid(screen, in, tiles, grid_sel, !side, hits);
      in_last = in;
    }
    else if (project || !remote_host.empty()) tui::render_board(screen, in, model, view, !side, buttons, now, hits);
    else screen.put(in.x, in.y + 1, tr("No project in this folder. Pick one in the sidebar."), {});

    if (overlay == Overlay::Help) tui::render_help(screen, main_r);
    if (overlay == Overlay::Prompt) {
      std::string what = asking == Ask::NewSession   ? trf("New session in {}", {new_name})
                         : asking == Ask::AddMachine ? tr("Add a machine: its ssh host")
                                                     : tr("Comment for the agent");
      tui::render_prompt(screen, main_r, what, input, true);
    }
    if (overlay == Overlay::Confirm)
      tui::render_prompt(screen, main_r, kill_id ? tr("End the session") : tr("Remove the machine"), input, false);

    std::vector<std::pair<std::string, std::string>> keys;
    switch (overlay) {
      case Overlay::Prompt:
      case Overlay::Confirm: keys = {{"enter", tr("confirm")}, {"esc", tr("cancel")}}; break;
      case Overlay::Help: keys = {{"esc", tr("close")}}; break;
      case Overlay::Focus: keys = {{"ctrl-]", tr("back to the board")}, {tr("click"), tr("another session")}}; break;
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
          keys = {{"enter", tr("open")}, {"x", tr("remove")}, {"→", tr("board")}};
        } else if (tab == kGrid) {
          keys = {{"enter", tr("open")}, {"←→↑↓", tr("move")}};
        } else if (tab == kActivity) {
          keys = {{"↑↓", tr("scroll")}};
        } else {
          // The card's own actions sit on its buttons, each with its key.
          if (!buttons.empty()) {
            std::string l = buttons.front().label;
            if (!l.empty() && l[0] >= 'A' && l[0] <= 'Z') l[0] = static_cast<char>(l[0] - 'A' + 'a');
            keys.push_back({"enter", l});
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
    std::vector<int> fds;
    if (focus) fds.push_back(focus->client.fd());
    for (const auto& [h, c] : grid_clients) fds.push_back(c->fd());
    tui::KeyEvent k = term.poll(fds.empty() ? 100 : 50, fds);
    ++view.tick;
    if (focus) drain_focus();
    if (!grid_clients.empty()) drain_grid();
    // Keys the screen does not know (F-keys, Alt combinations, the kitty
    // encoding of Ctrl-]) still matter inside an agent's terminal.
    if (k.key == Key::Wake || (k.key == Key::None && (k.raw.empty() || overlay != Overlay::Focus))) continue;
    if (k.key == Key::Resize) {
      screen.resize(term.width(), term.height());
      continue;
    }

    // In an agent's terminal every key goes to the agent, except Ctrl-].
    if (overlay == Overlay::Focus && focus && k.key != Key::Click && k.key != Key::Resize) {
      if (is_detach(k.raw)) {
        close_focus();
        poller.poke();
      } else if (!k.raw.empty() && k.key != Key::WheelUp && k.key != Key::WheelDown) {
        focus->client.send({proto::Type::Data, focus->id, translate_keys(k.raw, focus->vt.app_cursor())});
      }
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
          poller.poke();
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
        if (kill_id == 0) err = remove_machine(kill_host);
        else if (!kill_session(kill_host, kill_id, &err) && err.empty()) err = tr("could not end the session");
        if (!err.empty()) say(err);
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
        case tui::Act::Tile:
          if (!side && grid_sel == h->a && h->a < static_cast<int>(grid.size()))
            open_terminal(grid[static_cast<size_t>(h->a)].host, grid[static_cast<size_t>(h->a)].id);
          side = false;
          grid_sel = h->a;
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
    if (c == 'q') {
      close_focus();
      return 0;
    }
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
    if (tab == kGrid) {
      int n = static_cast<int>(grid.size());
      int cols = tui::grid_columns(in_last, grid.size());
      if (left && grid_sel % cols == 0 && SW) side = true;
      else if (left) --grid_sel;
      if (right) ++grid_sel;
      if (up) grid_sel -= cols;
      if (down) grid_sel += cols;
      grid_sel = std::clamp(grid_sel, 0, std::max(0, n - 1));
      if (k.key == Key::Enter && grid_sel < n) open_terminal(grid[static_cast<size_t>(grid_sel)].host, grid[static_cast<size_t>(grid_sel)].id);
      continue;
    }
    if (tab == kActivity) {
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
    if (k.key == Key::Enter && !buttons.empty()) run_action(buttons.front().action, *sel);
    for (const auto& b : buttons)
      if (b.key.size() == 1 && c == b.key[0]) run_action(b.action, *sel);
  }
}

}  // namespace apron::cli
