#include "cli/fleet.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "cli/daemon_client.hpp"
#include "core/project.hpp"
#include "core/runs.hpp"
#include "core/time.hpp"

namespace apron::cli {

struct FleetPoller::State {
  std::mutex mutex;
  std::condition_variable wake;
  bool stop = false;
  bool poked = false;
  bool done = false;
  tui::Fleet fleet;
};

namespace {

const std::set<std::string> kShells{"sh", "bash", "zsh", "fish", "dash", "ksh", "tcsh", "-zsh", "-bash", "login"};

struct Remote {
  std::unique_ptr<DaemonClient> client;
  int64_t last_try = 0;
  int64_t seen = 0;
  std::string error;
};

std::optional<json::Value> ask(DaemonClient& c, const char* cmd) {
  json::Object req;
  req.emplace_back("cmd", cmd);
  std::string err;
  return c.request(std::move(req), &err);
}

std::string base_name(const std::string& path) {
  std::string p = path;
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  auto slash = p.rfind('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Sessions and projects of one machine, grouped by project folder.
void add_machine(tui::Fleet& fleet, const std::string& host, const json::Value& sessions, const json::Value& projects,
                 const std::map<std::string, RunState>& run_by_session) {
  std::vector<std::string> roots;
  for (const auto& p : projects.arr()) roots.push_back(p.get_str("root"));
  std::map<std::string, size_t> group_of;  // root or folder -> index in fleet.projects
  auto group = [&](const std::string& key, const std::string& root) -> tui::ProjectGroup& {
    auto it = group_of.find(key);
    if (it != group_of.end()) return fleet.projects[it->second];
    group_of[key] = fleet.projects.size();
    fleet.projects.push_back({base_name(key), host, root, {}});
    return fleet.projects.back();
  };
  for (const auto& root : roots) group(root, root);
  for (const auto& s : sessions.arr()) {
    tui::SessionRow row;
    row.machine = host;
    row.id = static_cast<uint32_t>(s.find("id") ? s.find("id")->num() : 0);
    row.cwd = s.get_str("cwd");
    bool alive = s.find("alive") && s.find("alive")->boolean();
    std::string fg = s.get_str("foreground");
    row.what = fg.empty() ? s.get_str("command") : fg;
    row.since_ms = timeutil::parse_iso8601(s.get_str("created")).value_or(0);
    if (!alive) row.state = tui::SessionState::Exited;
    else if (kShells.count(fg)) row.state = tui::SessionState::Idle;
    else row.state = tui::SessionState::Working;
    if (alive && host.empty()) {
      auto r = run_by_session.find(std::to_string(row.id));
      if (r != run_by_session.end() && r->second == RunState::Waiting) row.state = tui::SessionState::Asking;
    }
    // The longest project root that holds the session's folder.
    std::string best;
    for (const auto& root : roots)
      if ((row.cwd == root || row.cwd.rfind(root + "/", 0) == 0) && root.size() > best.size()) best = root;
    group(best.empty() ? row.cwd : best, best).sessions.push_back(std::move(row));
  }
}

void collect(FleetPoller::State& st, std::map<std::string, Remote>& remotes) {
  tui::Fleet fleet;
  int64_t now = timeutil::now_ms();

  // This machine: starts aprond if needed, so its sessions always show.
  tui::MachineRow here;
  here.name = machine_name();
  here.here = true;
  DaemonClient local;
  std::string err;
  if (local.connect(true, &err)) {
    auto s = ask(local, "sessions.list");
    auto p = ask(local, "projects.list");
    if (s && p) {
      std::map<std::string, RunState> run_by_session;
      for (const auto& proj : p->find("projects")->arr())
        for (const auto& r : list_runs(Project{proj.get_str("root")}))
          if (!r.session.empty() && (r.state == RunState::Working || r.state == RunState::Waiting))
            run_by_session[r.session] = r.state;
      add_machine(fleet, "", *s->find("sessions"), *p->find("projects"), run_by_session);
      here.ok = true;
      here.seen_ms = now;
    }
  }
  fleet.machines.push_back(here);

  // The others, over ssh. A connection is kept and reused; a failed one is retried every 10 s.
  auto machines = load_machines();
  for (const auto& m : machines) {
    Remote& r = remotes[m.host];
    if (!r.client && now - r.last_try > 10000) {
      r.last_try = now;
      auto c = std::make_unique<DaemonClient>();
      std::string e;
      if (c->connect_ssh(m.host, m.remote, &e)) r.client = std::move(c), r.error.clear();
      else r.error = e;
    }
    tui::MachineRow row;
    row.name = m.host;
    if (r.client) {
      auto s = ask(*r.client, "sessions.list");
      auto p = ask(*r.client, "projects.list");
      if (s && p && s->find("sessions") && p->find("projects")) {
        add_machine(fleet, m.host, *s->find("sessions"), *p->find("projects"), {});
        r.seen = now;
        row.ok = true;
        for (const auto& x : s->find("sessions")->arr())
          if (x.find("alive") && x.find("alive")->boolean()) ++row.running;
      } else {
        r.client.reset();
        r.error = "lost the connection";
      }
    }
    row.seen_ms = r.seen;
    row.error = r.error;
    fleet.machines.push_back(row);
  }
  for (auto& m : fleet.machines)
    if (m.here) {
      for (const auto& p : fleet.projects)
        if (p.machine.empty())
          for (const auto& s : p.sessions) m.running += s.state != tui::SessionState::Exited;
    }

  // Projects with sessions first, then by name; this machine before the others.
  std::stable_sort(fleet.projects.begin(), fleet.projects.end(), [](const auto& a, const auto& b) {
    if (a.sessions.empty() != b.sessions.empty()) return !a.sessions.empty();
    if (a.machine.empty() != b.machine.empty()) return a.machine.empty();
    return a.name < b.name;
  });
  std::lock_guard lock(st.mutex);
  st.fleet = std::move(fleet);
}

}  // namespace

FleetPoller::FleetPoller() : state_(std::make_shared<State>()) {
  std::thread([st = state_] {
    std::map<std::string, Remote> remotes;
    while (true) {
      collect(*st, remotes);
      std::unique_lock lock(st->mutex);
      st->wake.wait_for(lock, std::chrono::milliseconds(1500), [&] { return st->stop || st->poked; });
      if (st->stop) {
        st->done = true;
        st->wake.notify_all();
        return;
      }
      st->poked = false;
    }
  }).detach();
}

FleetPoller::~FleetPoller() {
  // Usually the worker is waiting and stops at once. If it is stuck on a slow
  // ssh, give up after a moment: it holds its own copy of the state.
  std::unique_lock lock(state_->mutex);
  state_->stop = true;
  state_->wake.notify_all();
  state_->wake.wait_for(lock, std::chrono::milliseconds(300), [&] { return state_->done; });
}

tui::Fleet FleetPoller::snapshot() const {
  std::lock_guard lock(state_->mutex);
  return state_->fleet;
}

void FleetPoller::poke() {
  std::lock_guard lock(state_->mutex);
  state_->poked = true;
  state_->wake.notify_all();
}

}  // namespace apron::cli
