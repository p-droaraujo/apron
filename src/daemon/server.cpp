#include "daemon/server.hpp"

#include "core/activity.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/json.hpp"
#include "core/project.hpp"
#include "daemon/registry.hpp"
#include "daemon/session.hpp"
#include "proto/frame.hpp"
#include "proto/socket.hpp"

namespace apron::daemon {

namespace {

int g_signal_pipe[2] = {-1, -1};

void on_signal(int sig) {
  char c = static_cast<char>(sig);
  ssize_t n = ::write(g_signal_pipe[1], &c, 1);
  (void)n;
}

struct Client {
  int fd = -1;
  proto::Reader reader;
  std::set<uint32_t> attached;
  bool subscribed = false;  // receives every event pushed to the daemon
};

class Server {
 public:
  Server(int listen_fd, Registry registry) : listen_fd_(listen_fd), registry_(std::move(registry)) {}
  int run();

 private:
  int listen_fd_;
  Registry registry_;
  bool stopping_ = false;
  uint32_t next_id_ = 1;
  std::map<uint32_t, std::unique_ptr<Session>> sessions_;
  std::vector<std::unique_ptr<Client>> clients_;

  void accept_client();
  bool handle_client(Client& c);  // false: drop the client
  void handle_ctrl(Client& c, const std::string& payload);
  void reply(Client& c, const json::Value& request, json::Object body, bool ok = true, const std::string& error = "");
  void send(Client& c, proto::Type type, uint32_t channel, const std::string& payload);
  void pump_session(Session& s);
  void reap_children();
};

int Server::run() {
  std::fprintf(stderr, "aprond: listening\n");
  while (!stopping_) {
    std::vector<pollfd> fds;
    fds.push_back({listen_fd_, POLLIN, 0});
    fds.push_back({g_signal_pipe[0], POLLIN, 0});
    for (auto& c : clients_) fds.push_back({c->fd, POLLIN, 0});
    std::vector<Session*> polled;
    for (auto& [id, s] : sessions_)
      if (s->alive()) {
        fds.push_back({s->fd(), POLLIN, 0});
        polled.push_back(s.get());
      }
    if (poll(fds.data(), fds.size(), 1000) < 0) continue;

    if (fds[1].revents & POLLIN) {
      char sigs[16];
      ssize_t n = ::read(g_signal_pipe[0], sigs, sizeof sigs);
      for (ssize_t i = 0; i < n; ++i) {
        if (sigs[i] == SIGCHLD) reap_children();
        else stopping_ = true;
      }
    }
    size_t base = 2;
    std::vector<Client*> dropped;
    for (size_t i = 0; i < clients_.size(); ++i)
      if (fds[base + i].revents & (POLLIN | POLLHUP | POLLERR))
        if (!handle_client(*clients_[i])) dropped.push_back(clients_[i].get());
    base += clients_.size();
    for (size_t i = 0; i < polled.size(); ++i)
      if (fds[base + i].revents & (POLLIN | POLLHUP)) pump_session(*polled[i]);
    for (Client* d : dropped) {
      close(d->fd);
      std::erase_if(clients_, [&](const auto& c) { return c.get() == d; });
    }
    if (fds[0].revents & POLLIN) accept_client();
  }
  for (auto& [id, s] : sessions_) s->kill_tree();
  std::fprintf(stderr, "aprond: stopped\n");
  return 0;
}

void Server::accept_client() {
  int fd = accept(listen_fd_, nullptr, nullptr);
  if (fd < 0) return;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  auto c = std::make_unique<Client>();
  c->fd = fd;
  clients_.push_back(std::move(c));
}

bool Server::handle_client(Client& c) {
  char buf[65536];
  ssize_t n = ::read(c.fd, buf, sizeof buf);
  if (n <= 0) return false;
  c.reader.feed(std::string_view(buf, static_cast<size_t>(n)));
  while (auto f = c.reader.next()) {
    if (f->type == proto::Type::Ctrl) handle_ctrl(c, f->payload);
    else if (f->type == proto::Type::Data) {
      auto it = sessions_.find(f->channel);
      if (it != sessions_.end() && c.attached.count(f->channel)) it->second->write_input(f->payload);
    } else if (f->type == proto::Type::Ping) {
      send(c, proto::Type::Ping, 0, "");
    }
  }
  return !c.reader.broken();
}

void Server::send(Client& c, proto::Type type, uint32_t channel, const std::string& payload) {
  proto::write_all(c.fd, proto::encode({type, channel, payload}));
}

void Server::reply(Client& c, const json::Value& request, json::Object body, bool ok, const std::string& error) {
  json::Object o;
  if (const json::Value* id = request.find("id")) o.emplace_back("id", *id);
  o.emplace_back("ok", ok);
  if (!ok) o.emplace_back("error", error);
  for (auto& kv : body) o.push_back(std::move(kv));
  send(c, proto::Type::Ctrl, 0, json::dump(json::Value(std::move(o))));
}

static int num(const json::Value& v, std::string_view key, int fallback) {
  const json::Value* f = v.find(key);
  return f && f->is_number() ? static_cast<int>(f->num()) : fallback;
}

void Server::handle_ctrl(Client& c, const std::string& payload) {
  auto req = json::parse(payload);
  if (!req || !req->is_object()) return;
  std::string cmd = req->get_str("cmd");

  if (cmd == "hello") {
    json::Object b;
    b.emplace_back("version", static_cast<long long>(proto::kProtocolVersion));
    b.emplace_back("machine", machine_name());
    reply(c, *req, std::move(b));
  } else if (cmd == "sessions.list") {
    json::Array list;
    for (auto& [id, s] : sessions_) list.push_back(s->describe());
    json::Object b;
    b.emplace_back("sessions", std::move(list));
    reply(c, *req, std::move(b));
  } else if (cmd == "sessions.spawn") {
    std::vector<std::string> argv;
    if (const json::Value* a = req->find("argv"); a && a->is_array())
      for (const auto& v : a->arr())
        if (v.is_string()) argv.push_back(v.str());
    if (argv.empty()) {
      const char* shell = std::getenv("SHELL");
      argv.push_back(shell && *shell ? shell : "/bin/sh");
    }
    std::string err;
    uint32_t id = next_id_++;
    Session* s = Session::spawn(id, argv, req->get_str("cwd"), num(*req, "cols", 80), num(*req, "rows", 24), &err);
    if (!s) return reply(c, *req, {}, false, err);
    sessions_[id].reset(s);
    json::Object b;
    b.emplace_back("session", static_cast<long long>(id));
    reply(c, *req, std::move(b));
  } else if (cmd == "sessions.attach" || cmd == "sessions.detach" || cmd == "sessions.resize" ||
             cmd == "sessions.kill") {
    uint32_t id = static_cast<uint32_t>(num(*req, "session", 0));
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return reply(c, *req, {}, false, "not_found");
    Session& s = *it->second;
    if (cmd == "sessions.attach") {
      c.attached.insert(id);
      if (s.alive()) s.resize(num(*req, "cols", 80), num(*req, "rows", 24));
      reply(c, *req, {});
      // Replay the recent output so the client sees where things stand.
      send(c, proto::Type::Data, id, s.replay());
    } else if (cmd == "sessions.detach") {
      c.attached.erase(id);
      reply(c, *req, {});
    } else if (cmd == "sessions.resize") {
      if (s.alive()) s.resize(num(*req, "cols", 80), num(*req, "rows", 24));
      reply(c, *req, {});
    } else {
      s.kill_tree();
      if (!s.alive()) sessions_.erase(it);
      reply(c, *req, {});
    }
  } else if (cmd == "events.push") {
    // A CLI recorded an event in a project's files: learn the project and
    // pass the event on to whoever is watching. No reply: fire and forget.
    std::string root = req->get_str("root");
    if (!root.empty()) registry_.add(root);
    if (const json::Value* ev = req->find("event")) {
      json::Object out;
      out.emplace_back("event", "run.event");
      out.emplace_back("root", root);
      out.emplace_back("data", *ev);
      std::string payload = json::dump(json::Value(std::move(out)));
      for (auto& other : clients_)
        if (other->subscribed) send(*other, proto::Type::Ctrl, 0, payload);
    }
  } else if (cmd == "events.subscribe") {
    c.subscribed = true;
    reply(c, *req, {});
  } else if (cmd == "projects.list") {
    json::Object b;
    b.emplace_back("projects", registry_.list());
    reply(c, *req, std::move(b));
  } else if (cmd == "events.recent") {
    // The newest events of every project on this machine, for a TUI elsewhere.
    std::vector<std::string> roots;
    json::Value projects = registry_.list();
    for (const auto& p : projects.arr()) roots.push_back(p.get_str("root"));
    size_t limit = static_cast<size_t>(std::clamp(num(*req, "limit", 200), 1, 2000));
    json::Array events;
    for (auto& item : load_activity(roots, limit)) {
      json::Object o;
      o.emplace_back("project", item.project);
      o.emplace_back("event", item.event.to_json());
      events.emplace_back(std::move(o));
    }
    json::Object b;
    b.emplace_back("events", std::move(events));
    reply(c, *req, std::move(b));
  } else if (cmd == "daemon.stop") {
    reply(c, *req, {});
    stopping_ = true;
  } else {
    reply(c, *req, {}, false, "unknown_command");
  }
}

void Server::pump_session(Session& s) {
  std::string out = s.read_output();
  if (out.empty()) return;
  for (auto& c : clients_)
    if (c->attached.count(s.id())) send(*c, proto::Type::Data, s.id(), out);
}

void Server::reap_children() {
  int status = 0;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (auto& [id, s] : sessions_) {
      if (s->pid() != pid) continue;
      pump_session(*s);  // whatever it wrote last
      s->mark_exited(status);
      json::Object ev;
      ev.emplace_back("event", "session.exited");
      ev.emplace_back("session", static_cast<long long>(id));
      std::string payload = json::dump(json::Value(std::move(ev)));
      for (auto& c : clients_)
        if (c->attached.count(id)) send(*c, proto::Type::Ctrl, 0, payload);
    }
  }
}

}  // namespace

int serve(const std::filesystem::path& socket) {
  if (pipe(g_signal_pipe) != 0) return 1;
  fcntl(g_signal_pipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(g_signal_pipe[1], F_SETFD, FD_CLOEXEC);
  fcntl(g_signal_pipe[1], F_SETFL, O_NONBLOCK);
  struct sigaction sa {};
  sa.sa_handler = on_signal;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);  // a client that vanishes must not kill the daemon

  std::string err;
  int fd = proto::listen_unix(socket, &err);
  if (fd < 0) {
    std::fprintf(stderr, "aprond: cannot listen on %s: %s\n", socket.c_str(), err.c_str());
    return 1;
  }
  Server server(fd, Registry(Registry::default_file()));
  int rc = server.run();
  close(fd);
  unlink(socket.c_str());
  return rc;
}

}  // namespace apron::daemon
