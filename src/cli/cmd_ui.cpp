// apron (no arguments): the screen. A sidebar with projects, sessions and
// machines; the board or the activity of a project; details over the board.
//
// Everything a person can do is an element on the screen: the arrows move
// the focus to it, Enter does it, the mouse clicks it. The only other keys
// are Tab and Shift-Tab for the views, Esc to go back, q to quit and Ctrl-]
// to leave an agent's terminal. The footer says what Enter does now.
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
  // The element with the focus. Act::None: the main pane itself, when it
  // shows something to read rather than to act on (Activity, a log).
  tui::Target cur{tui::Act::Card, 1, 0};
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
    if (tab == kActivity) activity = load_activity(local_roots(), 500);
  };
  auto selected_item = [&]() -> const tui::Item* {
    const auto& items = model.lanes[view.lane];
    if (items.empty() || view.index >= static_cast<int>(items.size())) return nullptr;
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
      if (live) b.push_back({it->state == RunState::Waiting ? Action::Answer : Action::Terminal,
                             it->state == RunState::Waiting ? tr("Answer") : tr("Terminal")});
      return b;
    }
    if (it->review) b.push_back({Action::Review, tr("Review")});
    if (live) {
      bool answer = it->state == RunState::Waiting;
      b.push_back({answer ? Action::Answer : Action::Terminal, answer ? tr("Answer") : tr("Terminal")});
    }
    if (!it->run.empty()) b.push_back({Action::Log, tr("Log")});
    else b.push_back({Action::Details, tr("Details")});
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
    drain_focus();
  };
  auto run_action = [&](Action a, const tui::Item* it) {
    switch (a) {
      case Action::Terminal:
      case Action::Answer:
        if (it) open_terminal(remote_host, static_cast<uint32_t>(std::atoi(it->session.c_str())));
        break;
      case Action::Review:
        if (!it) break;
        open_run = it->run;
        stat = run_changes(*project, it->run).stat;
        overlay = Overlay::Review;
        cur = {tui::Act::Button, static_cast<int>(Action::Merge), 0};
        break;
      case Action::Log:
        if (it) open_run = it->run;
        scroll = 0;
        overlay = Overlay::Log;
        cur = {};
        break;
      case Action::Details:
        if (!it) break;
        open_task = it->task;
        scroll = 0;
        overlay = Overlay::Task;
        cur = {};
        break;
      case Action::Merge:
      case Action::Discard: {
        std::string err = a == Action::Merge ? review_merge(*project, open_run) : review_discard(*project, open_run);
        if (err.empty()) {
          say(a == Action::Merge ? tr("merged into main") : tr("discarded; the task is open again"));
          overlay = Overlay::None;
          cur = {tui::Act::Card, view.lane, view.index};
          reload();
        } else {
          say(err);
        }
        break;
      }
      case Action::Return:
        input.clear();
        asking = Ask::Return;
        overlay = Overlay::Prompt;
        break;
    }
  };
  // A new agent starts in the project on the board.
  auto start_new_session = [&] {
    new_host = remote_host;
    new_cwd = !remote_host.empty() ? remote_root
              : project          ? project->root.string()
                                 : std::filesystem::current_path().string();
    new_name = !remote_host.empty() ? remote_name + " · " + remote_host
               : project            ? project->root.filename().string()
                                    : new_cwd;
    input = "claude";
    asking = Ask::NewSession;
    overlay = Overlay::Prompt;
  };
  auto open_project = [&](int gi) {
    if (gi >= static_cast<int>(fleet.projects.size())) return;
    const auto& g = fleet.projects[static_cast<size_t>(gi)];
    if (!g.machine.empty() && !g.root.empty()) {
      remote_host = g.machine;
      remote_root = g.root;
      remote_name = g.name;
      poller.watch(remote_host, remote_root);
      model = {};
    } else if (g.machine.empty() && has_project(g.root)) {
      remote_host.clear();
      poller.watch("", "");
      project = Project{g.root};
      model = tui::load_model(*project);
    } else {
      say(tr("this folder has no .apron/ yet"));
      return;
    }
    view = {};
    tui::clamp_selection(model, view);
    overlay = Overlay::None;
    tab = kBoard;
    cur = {tui::Act::Card, view.lane, view.index};
  };
  auto ask_end_session = [&](int gi, int si) {
    if (gi >= static_cast<int>(fleet.projects.size())) return;
    const auto& g = fleet.projects[static_cast<size_t>(gi)];
    if (si >= static_cast<int>(g.sessions.size())) return;
    const auto& s = g.sessions[static_cast<size_t>(si)];
    kill_host = s.machine;
    kill_id = s.id;
    input = s.machine.empty() ? trf("End session {} ({})?", {std::to_string(s.id), s.what})
                              : trf("End session {} ({}) on {}?", {std::to_string(s.id), s.what, s.machine});
    overlay = Overlay::Confirm;
  };
  auto ask_remove_machine = [&](int mi) {
    if (mi >= static_cast<int>(fleet.machines.size())) return;
    const auto& m = fleet.machines[static_cast<size_t>(mi)];
    if (m.here) return;
    kill_host = m.name;
    kill_id = 0;  // 0: remove the machine from the list
    input = trf("Remove {} from the machines?", {m.name});
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
    overlay = Overlay::None;
    if (tab == kGrid) sync_grid();
    else leave_grid();
    if (tab == kActivity) {
      activity = load_activity(local_roots(), 500);
      activity_scroll = 0;
    }
    cur = tab == kBoard ? tui::Target{tui::Act::Card, view.lane, view.index}
          : tab == kGrid ? tui::Target{tui::Act::Tile, 0, 0}
                         : tui::Target{};
  };
  auto in_sidebar = [](const tui::Target& t) {
    using tui::Act;
    return t.act == Act::Project || t.act == Act::Session || t.act == Act::SessionEnd || t.act == Act::Machine ||
           t.act == Act::MachineRemove || t.act == Act::AddMachine;
  };
  // What Enter does on the element with the focus, for the footer.
  auto enter_label = [&](const std::vector<Button>& buttons) -> std::string {
    auto lower = [](std::string l) {
      if (!l.empty() && l[0] >= 'A' && l[0] <= 'Z') l[0] = static_cast<char>(l[0] - 'A' + 'a');
      return l;
    };
    switch (cur.act) {
      case tui::Act::Project: return tr("open the project");
      case tui::Act::Session:
      case tui::Act::Tile: return tr("open the terminal");
      case tui::Act::SessionEnd: return tr("end the session");
      case tui::Act::MachineRemove: return tr("remove the machine");
      case tui::Act::AddMachine: return tr("add a machine");
      case tui::Act::NewAgent: return tr("start an agent");
      case tui::Act::Card: return buttons.empty() ? "" : lower(buttons.front().label);
      case tui::Act::Button: return lower(tr(cur.a == static_cast<int>(Action::Merge)     ? "Merge"
                                             : cur.a == static_cast<int>(Action::Return)  ? "Return"
                                             : cur.a == static_cast<int>(Action::Discard) ? "Discard"
                                             : cur.a == static_cast<int>(Action::Review)  ? "Review"
                                             : cur.a == static_cast<int>(Action::Log)     ? "Log"
                                             : cur.a == static_cast<int>(Action::Details) ? "Details"
                                             : cur.a == static_cast<int>(Action::Answer)  ? "Answer"
                                                                                          : "Terminal"));
      default: return "";
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
    if (cur.act == tui::Act::Card && tab == kBoard && overlay == Overlay::None) {
      view.lane = cur.a;
      view.index = cur.b;
      tui::clamp_selection(model, view);
    }

    // --- Draw ---------------------------------------------------------------
    screen.clear();
    hits.clear();
    int W = screen.width(), H = screen.height();
    int SW = tui::sidebar_width(W);
    if (SW) tui::render_sidebar(screen, {0, 0, SW, H - 1}, fleet, cur, now, hits);
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
    tui::Rect in = tui::render_main_frame(screen, main_r, title, {tr("Progress"), tr("Agents"), tr("Activity")}, tab,
                                          !in_sidebar(cur), hits);
    const tui::Item* sel = selected_item();
    std::vector<Button> buttons = buttons_for(sel);
    std::vector<Button> review_buttons;
    if (overlay == Overlay::Review) {
      review_buttons = {{Action::Merge, tr("Merge")}, {Action::Return, tr("Return")}, {Action::Discard, tr("Discard")}};
      for (const auto& b : buttons)
        if (b.action == Action::Terminal || b.action == Action::Answer) review_buttons.push_back(b);
      review_buttons.push_back({Action::Log, tr("Log")});
    }
    tui::Screen sub;
    sub.resize(std::max(1, in.w), std::max(1, in.h - (overlay == Overlay::Review ? 2 : 0)));
    bool drew_sub = true;
    if (overlay == Overlay::Log && project) tui::render_log(sub, *project, open_run, scroll, now);
    else if (overlay == Overlay::Task && project) tui::render_task(sub, *project, open_task);
    else if (overlay == Overlay::Review && project)
      tui::render_review(sub, *project, summarize(open_run, load_events(project->run_file(open_run))), stat);
    else if (tab == kActivity) tui::render_activity(sub, activity, activity_scroll, now, project ? project->root.filename().string() : "");
    else drew_sub = false;
    if (overlay == Overlay::Focus && focus) {
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
      if (overlay == Overlay::Review) tui::draw_buttons(screen, in.x, in.y + in.h - 1, in.w, review_buttons, cur, hits);
    } else if (tab == kGrid) {
      std::vector<tui::Tile> tiles;
      for (const auto& w : grid) tiles.push_back({w.title, w.vt.get()});
      tui::render_grid(screen, in, tiles, cur, hits);
    } else if (project || !remote_host.empty()) {
      tui::render_board(screen, in, model, view, cur, buttons, now, hits);
    } else {
      screen.put(in.x, in.y + 1, tr("No project in this folder. Pick one in the sidebar."), {});
    }

    // The focus never points at nothing: when its element is gone, it goes
    // to the nearest thing that is there.
    bool read_only = overlay == Overlay::Log || overlay == Overlay::Task ||
                     (tab == kActivity && overlay == Overlay::None);
    if (overlay != Overlay::Focus && (cur.act != tui::Act::None || !read_only) && !tui::find_hit(hits, cur)) {
      tui::Target was = cur;
      cur = {};
      if (was.act == tui::Act::Button && tab == kBoard && overlay == Overlay::None)
        cur = {tui::Act::Card, view.lane, view.index};
      if (!tui::find_hit(hits, cur)) {
        cur = {};
        for (const auto& h : hits)
          if (h.focusable && (in_sidebar(was) ? in_sidebar(h.t) : !in_sidebar(h.t))) {
            cur = h.t;
            break;
          }
      }
    }

    if (overlay == Overlay::Help) tui::render_help(screen, main_r);
    if (overlay == Overlay::Prompt) {
      std::string what = asking == Ask::NewSession   ? trf("New agent in {}", {new_name})
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
      case Overlay::Focus: keys = {{"ctrl-]", tr("back")}}; break;
      default: {
        std::string what = enter_label(buttons);
        if (!what.empty()) keys.push_back({"enter", what});
        if (overlay != Overlay::None) keys.push_back({"esc", tr("back")});
        keys.push_back({"tab", tr("next view")});
        keys.push_back({"q", tr("quit")});
      }
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
      if (k.key == Key::Esc) overlay = asking == Ask::Return ? Overlay::Review : Overlay::None;
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
          cur = {tui::Act::Card, view.lane, view.index};
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

    // Doing what an element is for: Enter on it, or a click.
    auto activate = [&](const tui::Target& t) {
      switch (t.act) {
        case tui::Act::Tab: set_tab(t.a); break;
        case tui::Act::Project: open_project(t.a); break;
        case tui::Act::Session: {
          if (t.a >= static_cast<int>(fleet.projects.size())) break;
          const auto& g = fleet.projects[static_cast<size_t>(t.a)];
          if (t.b >= static_cast<int>(g.sessions.size())) break;
          const auto& s = g.sessions[static_cast<size_t>(t.b)];
          if (s.state == tui::SessionState::Exited) say(tr("the session has ended"));
          else open_terminal(s.machine, s.id);
          break;
        }
        case tui::Act::SessionEnd: ask_end_session(t.a, t.b); break;
        case tui::Act::Machine:
          if (t.a < static_cast<int>(fleet.machines.size()) && !fleet.machines[static_cast<size_t>(t.a)].error.empty())
            say(fleet.machines[static_cast<size_t>(t.a)].name + ": " + fleet.machines[static_cast<size_t>(t.a)].error);
          break;
        case tui::Act::MachineRemove: ask_remove_machine(t.a); break;
        case tui::Act::AddMachine:
          input.clear();
          asking = Ask::AddMachine;
          overlay = Overlay::Prompt;
          break;
        case tui::Act::Card:
          if (sel && !buttons.empty()) run_action(buttons.front().action, sel);
          break;
        case tui::Act::Button: run_action(static_cast<Action>(t.a), sel); break;
        case tui::Act::NewAgent: start_new_session(); break;
        case tui::Act::Tile:
          if (t.a < static_cast<int>(grid.size()))
            open_terminal(grid[static_cast<size_t>(t.a)].host, grid[static_cast<size_t>(t.a)].id);
          break;
        case tui::Act::None: break;
      }
    };

    // Mouse: a click on something already selected, or on anything that is
    // a button, does it; otherwise it selects.
    if (k.key == Key::Click) {
      const tui::Hit* h = tui::hit_at(hits, k.x, k.y);
      if (overlay == Overlay::Help) {
        overlay = Overlay::None;
        continue;
      }
      if (!h) continue;
      tui::Target t = h->t;
      bool button_like = t.act == tui::Act::Tab || t.act == tui::Act::Button || t.act == tui::Act::SessionEnd ||
                         t.act == tui::Act::MachineRemove || t.act == tui::Act::AddMachine ||
                         t.act == tui::Act::NewAgent;
      bool again = t == cur;
      if (t.act != tui::Act::Tab) cur = t;
      if (cur.act == tui::Act::Card) view.lane = cur.a, view.index = cur.b;
      if (button_like || again) activate(t);
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
      if (overlay == Overlay::Log && !open_run.empty() && sel && sel->review) {
        overlay = Overlay::Review;  // a log opened from the review goes back to it
        cur = {tui::Act::Button, static_cast<int>(Action::Log), 0};
      } else if (overlay != Overlay::None) {
        overlay = Overlay::None;
        cur = tab == kBoard ? tui::Target{tui::Act::Card, view.lane, view.index} : tui::Target{};
      } else if (in_sidebar(cur)) {
        cur = {};  // back to the main pane; the next frame picks its element
        cur = tab == kBoard ? tui::Target{tui::Act::Card, view.lane, view.index}
              : tab == kGrid ? tui::Target{tui::Act::Tile, 0, 0}
                             : tui::Target{};
      }
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
    bool up = k.key == Key::Up || c == 'k';
    bool down = k.key == Key::Down || c == 'j';
    bool left = k.key == Key::Left || c == 'h';
    bool right = k.key == Key::Right || c == 'l';

    if (k.key == Key::Enter) {
      activate(cur);
      continue;
    }
    // The wheel scrolls what is being read.
    bool reading = overlay == Overlay::Log || overlay == Overlay::Task || (tab == kActivity && overlay == Overlay::None);
    if (k.key == Key::WheelUp || k.key == Key::WheelDown) {
      int d = k.key == Key::WheelUp ? -3 : 3;
      if (tab == kActivity && overlay == Overlay::None) activity_scroll = std::max(0, activity_scroll + d);
      else scroll = std::max(0, scroll + d);
      continue;
    }
    if (!(up || down || left || right)) continue;
    // Reading a pane: up and down scroll it; left goes to the sidebar.
    if (reading && cur.act == tui::Act::None) {
      int& sc = tab == kActivity && overlay == Overlay::None ? activity_scroll : scroll;
      if (up) sc = std::max(0, sc - 1);
      if (down) ++sc;
      if (left)
        for (const auto& h : hits)
          if (h.focusable && in_sidebar(h.t)) {
            cur = h.t;
            break;
          }
      continue;
    }

    // On the board, up and down walk a lane even past what fits, and the
    // buttons of a card sit to its right.
    if (tab == kBoard && overlay == Overlay::None && (cur.act == tui::Act::Card || cur.act == tui::Act::Button)) {
      int lane = view.lane, n = static_cast<int>(model.lanes[lane].size());
      if ((down || up) && (cur.act == tui::Act::Card || cur.act == tui::Act::Button)) {
        int to = view.index + (down ? 1 : -1);
        if (to >= 0 && to < n) {
          cur = {tui::Act::Card, lane, to};
          continue;
        }
        if (cur.act == tui::Act::Button && up) {
          cur = {tui::Act::Card, lane, view.index};
          continue;
        }
      }
      if (cur.act == tui::Act::Card && right && !buttons.empty()) {
        cur = {tui::Act::Button, static_cast<int>(buttons.front().action), 0};
        continue;
      }
      if (cur.act == tui::Act::Button && (left || right)) {
        int at = -1;
        for (size_t i = 0; i < buttons.size(); ++i)
          if (static_cast<int>(buttons[i].action) == cur.a) at = static_cast<int>(i);
        int to = at + (right ? 1 : -1);
        if (at >= 0 && to >= 0 && to < static_cast<int>(buttons.size())) {
          cur = {tui::Act::Button, static_cast<int>(buttons[static_cast<size_t>(to)].action), 0};
          continue;
        }
        if (left && at == 0) {
          cur = {tui::Act::Card, lane, view.index};
          continue;
        }
        if (right) cur = {tui::Act::Card, lane, view.index};  // past the last button: on to the next lane
      }
    }
    tui::Dir dir = up ? tui::Dir::Up : down ? tui::Dir::Down : left ? tui::Dir::Left : tui::Dir::Right;
    if (const tui::Hit* h = tui::next_hit(hits, cur, dir)) {
      cur = h->t;
      if (cur.act == tui::Act::Card) view.lane = cur.a, view.index = cur.b;
    } else if (right && in_sidebar(cur) && reading) {
      cur = {};  // into the pane being read
    }
  }
}

}  // namespace apron::cli
