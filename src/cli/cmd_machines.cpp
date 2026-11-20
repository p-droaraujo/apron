// Machines over SSH: `apron machines` and `apron proxy`.
// Each machine runs its own aprond. Another machine reaches it with
// `ssh <host> apron proxy`, which relays the protocol bytes to the local socket.
// The access to a machine is the SSH access; Apron adds no other layer.
#include <poll.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "core/i18n.hpp"

namespace apron::cli {

std::filesystem::path machines_file() {
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return std::filesystem::path(x) / "apron" / "machines";
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : ".") / ".config" / "apron" / "machines";
}

std::vector<Machine> load_machines() {
  std::vector<Machine> out;
  std::ifstream in(machines_file());
  for (std::string line; std::getline(in, line);) {
    if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
    std::istringstream words(line);
    Machine m;
    if (!(words >> m.host)) continue;
    std::string rest;
    std::getline(words, rest);
    if (auto b = rest.find_first_not_of(" \t"); b != std::string::npos) m.remote = rest.substr(b);
    out.push_back(std::move(m));
  }
  return out;
}

const Machine* find_machine(const std::vector<Machine>& all, const std::string& host) {
  for (const auto& m : all)
    if (m.host == host) return &m;
  return nullptr;
}

std::optional<DaemonClient> connect_to(const std::string& host, bool autostart) {
  DaemonClient c;
  std::string err;
  bool ok;
  if (host.empty()) {
    ok = c.connect(autostart, &err);
  } else {
    auto all = load_machines();
    const Machine* m = find_machine(all, host);
    ok = c.connect_ssh(host, m ? m->remote : "apron", &err);
    if (!ok) err = host + ": " + err;
  }
  if (!ok) {
    fail(err);
    return std::nullopt;
  }
  return std::optional<DaemonClient>(std::move(c));
}

namespace {

bool save_machines(const std::vector<Machine>& all) {
  auto path = machines_file();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  for (const auto& m : all) out << m.host << (m.remote == "apron" ? "" : " " + m.remote) << "\n";
  return static_cast<bool>(out);
}

struct Status {
  bool ok = false;
  std::string machine, error;
  int running = 0, sessions = 0, projects = 0;
};

Status probe(const Machine& m) {
  Status s;
  DaemonClient c;
  if (!c.connect_ssh(m.host, m.remote, &s.error)) return s;
  std::string err;
  json::Object hello;
  hello.emplace_back("cmd", "hello");
  if (auto r = c.request(std::move(hello), &err)) s.machine = r->get_str("machine");
  json::Object ls;
  ls.emplace_back("cmd", "sessions.list");
  if (auto r = c.request(std::move(ls), &err))
    if (const json::Value* list = r->find("sessions"))
      for (const auto& x : list->arr()) {
        ++s.sessions;
        if (x.find("alive") && x.find("alive")->boolean()) ++s.running;
      }
  json::Object pl;
  pl.emplace_back("cmd", "projects.list");
  if (auto r = c.request(std::move(pl), &err))
    if (const json::Value* list = r->find("projects")) s.projects = static_cast<int>(list->arr().size());
  s.ok = true;
  return s;
}

int list(const std::vector<Machine>& all) {
  if (all.empty()) {
    std::puts(tr("no machines yet; add one with: apron machines add <ssh host>").c_str());
    return 0;
  }
  // All at once: an unreachable machine waits for its timeout, not the others.
  std::vector<std::future<Status>> probes;
  for (const auto& m : all) probes.push_back(std::async(std::launch::async, probe, m));
  Style st;
  int rc = 0;
  for (size_t i = 0; i < all.size(); ++i) {
    Status s = probes[i].get();
    if (s.ok) {
      std::printf("%s %-14s %s\n", st.blue("●").c_str(), all[i].host.c_str(),
                  st.dim(trf("{} running · {} projects", {std::to_string(s.running), std::to_string(s.projects)})).c_str());
    } else {
      rc = 1;
      std::printf("%s %-14s %s\n", st.dim("○").c_str(), all[i].host.c_str(), st.dim(s.error).c_str());
    }
  }
  return rc;
}

}  // namespace

int cmd_machines(const Args& args) {
  auto all = load_machines();
  std::string sub = args.pos.empty() ? "" : args.pos[0];
  if (sub.empty() || sub == "ls") return list(all);
  if (sub == "add") {
    if (args.pos.size() < 2) return fail(tr("usage: apron machines add <ssh host> [apron command there]"));
    Machine m{args.pos[1], args.pos.size() > 2 ? args.rest(2) : "apron"};
    bool known = false;
    for (auto& x : all)
      if (x.host == m.host) {
        x = m;
        known = true;
      }
    if (!known) all.push_back(m);
    if (!save_machines(all)) return fail(trf("could not write {}", {machines_file().string()}));
    return list({m});
  }
  if (sub == "rm") {
    if (args.pos.size() < 2) return fail(tr("usage: apron machines rm <ssh host>"));
    std::vector<Machine> kept;
    for (auto& x : all)
      if (x.host != args.pos[1]) kept.push_back(x);
    if (kept.size() == all.size()) return fail(trf("no machine {}", {args.pos[1]}));
    return save_machines(kept) ? 0 : fail(trf("could not write {}", {machines_file().string()}));
  }
  return fail(tr("usage: apron machines [add <ssh host> [apron command there] | rm <ssh host>]"));
}

int cmd_proxy(const Args&) {
  // stdin/stdout are the ssh channel: nothing else may be written to stdout.
  DaemonClient c;
  std::string err;
  if (!c.connect(true, &err)) {
    std::fprintf(stderr, "apron proxy: %s\n", err.c_str());
    return 1;
  }
  pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {c.fd(), POLLIN, 0}};
  char buf[65536];
  while (poll(fds, 2, -1) > 0) {
    for (int i = 0; i < 2; ++i) {
      if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      ssize_t n = ::read(fds[i].fd, buf, sizeof buf);
      if (n <= 0) return 0;  // either side closed
      int to = i == 0 ? c.fd() : STDOUT_FILENO;
      for (ssize_t off = 0; off < n;) {
        ssize_t w = ::write(to, buf + off, static_cast<size_t>(n - off));
        if (w <= 0) return 0;
        off += w;
      }
    }
  }
  return 0;
}

}  // namespace apron::cli
