// apron new | ls | attach | kill | daemon: sessions kept by the local aprond.
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <charconv>
#include <cstdio>
#include <filesystem>

#include "cli/common.hpp"
#include "cli/daemon_client.hpp"
#include "core/time.hpp"

namespace apron::cli {

namespace {

std::atomic<bool> g_winch{false};
void on_winch(int) { g_winch = true; }

void term_size(int& cols, int& rows) {
  winsize ws{};
  cols = 80;
  rows = 24;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    cols = ws.ws_col;
    rows = ws.ws_row;
  }
}

std::optional<DaemonClient> connect(bool autostart) { return connect_to("", autostart); }


constexpr char kDetachKey = 0x1d;  // Ctrl-]

// Raw passthrough between this terminal and a session, until Ctrl-] or exit.
int attach(DaemonClient& c, uint32_t id) {
  int cols, rows;
  term_size(cols, rows);
  json::Object req;
  req.emplace_back("cmd", "sessions.attach");
  req.emplace_back("session", static_cast<long long>(id));
  req.emplace_back("cols", cols);
  req.emplace_back("rows", rows);
  std::string err;
  if (!c.request(std::move(req), &err)) return fail(err == "not_found" ? trf("no session {}", {std::to_string(id)}) : err);

  bool tty = isatty(STDIN_FILENO);
  termios saved{};
  if (tty) {
    tcgetattr(STDIN_FILENO, &saved);
    termios raw = saved;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    std::fprintf(stderr, "%s\r\n", tr("[attached; Ctrl-] detaches]").c_str());
  }
  signal(SIGWINCH, on_winch);

  int rc = 0;
  bool done = false;
  auto drain = [&] {
    while (auto f = c.next(0)) {
      if (f->type == proto::Type::Data && f->channel == id) {
        ssize_t w = ::write(STDOUT_FILENO, f->payload.data(), f->payload.size());
        (void)w;
      } else if (f->type == proto::Type::Ctrl) {
        auto v = json::parse(f->payload);
        if (v && v->get_str("event") == "session.exited") done = true;
      }
    }
  };
  // The replay often arrives with the attach reply and sits queued: poll() would never see it.
  drain();
  while (!done) {
    if (g_winch.exchange(false)) {
      term_size(cols, rows);
      json::Object r;
      r.emplace_back("cmd", "sessions.resize");
      r.emplace_back("session", static_cast<long long>(id));
      r.emplace_back("cols", cols);
      r.emplace_back("rows", rows);
      c.send({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(r)))});
    }
    pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {c.fd(), POLLIN, 0}};
    if (poll(fds, 2, 250) < 0) continue;
    if (fds[0].revents & POLLIN) {
      char buf[4096];
      ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
      if (n <= 0) break;
      std::string in(buf, static_cast<size_t>(n));
      if (auto k = in.find(kDetachKey); k != std::string::npos) {
        in.resize(k);
        done = true;
      }
      if (!in.empty()) c.send({proto::Type::Data, id, in});
    }
    if (fds[1].revents & (POLLIN | POLLHUP)) {
      drain();
      if (fds[1].revents & POLLHUP) {
        rc = 1;
        break;
      }
    }
  }
  if (tty) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    std::fprintf(stderr, "\r\n%s\n", trf("[detached from session {}]", {std::to_string(id)}).c_str());
  }
  return rc;
}

int cmd_new(const Args& args) {
  auto c = connect(true);
  if (!c) return 1;
  int cols, rows;
  term_size(cols, rows);
  json::Array argv;
  for (const auto& p : args.pos) argv.emplace_back(p);
  json::Object req;
  req.emplace_back("cmd", "sessions.spawn");
  req.emplace_back("argv", std::move(argv));
  req.emplace_back("cwd", std::filesystem::current_path().string());
  req.emplace_back("cols", cols);
  req.emplace_back("rows", rows);
  std::string err;
  auto reply = c->request(std::move(req), &err);
  if (!reply) return fail(err);
  uint32_t id = static_cast<uint32_t>(reply->find("session")->num());
  if (args.has("d") || args.has("detach") || !isatty(STDIN_FILENO)) {
    std::printf("%u\n", id);
    return 0;
  }
  return attach(*c, id);
}




int cmd_daemon(const Args& args) {
  std::string sub = args.pos.empty() ? "status" : args.pos[0];
  if (sub == "stop") {
    auto c = connect(false);
    if (!c) return 1;
    json::Object req;
    req.emplace_back("cmd", "daemon.stop");
    std::string err;
    return c->request(std::move(req), &err) ? 0 : fail(err);
  }
  DaemonClient c;
  std::string err;
  if (!c.connect(false, &err)) {
    std::puts(tr("aprond is not running").c_str());
    return 1;
  }
  json::Object req;
  req.emplace_back("cmd", "hello");
  auto reply = c.request(std::move(req), &err);
  if (!reply) return fail(err);
  std::puts(trf("aprond is running on {}", {reply->get_str("machine")}).c_str());
  return 0;
}

}  // namespace

int attach_session(const std::string& host, uint32_t id) {
  auto c = connect_to(host, false);
  if (!c) return 1;
  return attach(*c, id);
}

namespace {

std::optional<DaemonClient> quiet_connect(const std::string& host, std::string* error) {
  DaemonClient c;
  bool ok;
  if (host.empty()) {
    ok = c.connect(true, error);
  } else {
    auto all = load_machines();
    const Machine* m = find_machine(all, host);
    ok = c.connect_ssh(host, m ? m->remote : "apron", error);
  }
  if (!ok) return std::nullopt;
  return std::optional<DaemonClient>(std::move(c));
}

}  // namespace

uint32_t spawn_session(const std::string& host, const std::string& cwd, const std::string& input, std::string* error) {
  auto c = quiet_connect(host, error);
  if (!c) return 0;
  int cols, rows;
  term_size(cols, rows);
  json::Object req;
  req.emplace_back("cmd", "sessions.spawn");
  req.emplace_back("argv", json::Array{});
  req.emplace_back("cwd", cwd);
  req.emplace_back("cols", cols);
  req.emplace_back("rows", rows);
  if (!input.empty()) req.emplace_back("input", input);
  auto reply = c->request(std::move(req), error);
  if (!reply || !reply->find("session")) return 0;
  return static_cast<uint32_t>(reply->find("session")->num());
}

bool kill_session(const std::string& host, uint32_t id, std::string* error) {
  auto c = quiet_connect(host, error);
  if (!c) return false;
  json::Object req;
  req.emplace_back("cmd", "sessions.kill");
  req.emplace_back("session", static_cast<long long>(id));
  return c->request(std::move(req), error).has_value();
}

int cmd_sessions(const std::string& cmd, const Args& args) {
  if (cmd == "new") return cmd_new(args);
  return cmd_daemon(args);
}

}  // namespace apron::cli
