#include "daemon/session.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>

#if defined(__APPLE__)
#include <libproc.h>
#include <util.h>
#else
#include <pty.h>
#endif

#include "core/time.hpp"

namespace apron::daemon {

namespace {

constexpr size_t kReplayBytes = 256 * 1024;

std::string process_name(pid_t pid) {
#if defined(__APPLE__)
  char name[PROC_PIDPATHINFO_MAXSIZE] = {0};
  if (proc_name(pid, name, sizeof name) > 0) return name;
  return "";
#else
  std::ifstream in("/proc/" + std::to_string(pid) + "/comm");
  std::string name;
  std::getline(in, name);
  return name;
#endif
}

}  // namespace

Session* Session::spawn(uint32_t id, const std::vector<std::string>& argv, const std::string& cwd, int cols, int rows,
                        std::string* error) {
  winsize ws{};
  ws.ws_col = static_cast<unsigned short>(cols > 0 ? cols : 80);
  ws.ws_row = static_cast<unsigned short>(rows > 0 ? rows : 24);
  int master = -1;
  pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
  if (pid < 0) {
    if (error) *error = std::string("forkpty: ") + std::strerror(errno);
    return nullptr;
  }
  if (pid == 0) {
    if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
    setenv("TERM", "xterm-256color", 0);
    setenv("APRON_SESSION", std::to_string(id).c_str(), 1);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
  fcntl(master, F_SETFD, FD_CLOEXEC);
  auto* s = new Session();
  s->id_ = id;
  s->master_ = master;
  s->pid_ = pid;
  s->cwd_ = cwd;
  for (const auto& a : argv) s->command_ += (s->command_.empty() ? "" : " ") + a;
  s->created_ms_ = timeutil::now_ms();
  return s;
}

Session::~Session() {
  if (master_ >= 0) close(master_);
}

std::string Session::read_output() {
  std::string out;
  char buf[16384];
  while (true) {
    ssize_t n = ::read(master_, buf, sizeof buf);
    if (n > 0) {
      out.append(buf, static_cast<size_t>(n));
      continue;
    }
    break;  // EAGAIN, or EIO once the child is gone
  }
  replay_ += out;
  if (replay_.size() > kReplayBytes) replay_.erase(0, replay_.size() - kReplayBytes);
  return out;
}

void Session::write_input(const std::string& bytes) {
  size_t off = 0;
  while (alive_ && off < bytes.size()) {
    ssize_t n = ::write(master_, bytes.data() + off, bytes.size() - off);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
    if (n <= 0) return;
    off += static_cast<size_t>(n);
  }
}

void Session::resize(int cols, int rows) {
  winsize ws{};
  ws.ws_col = static_cast<unsigned short>(cols);
  ws.ws_row = static_cast<unsigned short>(rows);
  ioctl(master_, TIOCSWINSZ, &ws);
}

void Session::kill_tree() {
  if (!alive_) return;
  kill(-pid_, SIGHUP);  // the session leader's group: the shell and its jobs
  kill(pid_, SIGHUP);
}

void Session::mark_exited(int status) {
  alive_ = false;
  exit_status_ = status;
}

std::string Session::foreground() const {
  if (!alive_) return "";
  pid_t fg = tcgetpgrp(master_);
  if (fg <= 0) return "";
  return process_name(fg);
}

json::Value Session::describe() const {
  json::Object o;
  o.emplace_back("id", static_cast<long long>(id_));
  o.emplace_back("command", command_);
  o.emplace_back("cwd", cwd_);
  o.emplace_back("pid", static_cast<long long>(pid_));
  o.emplace_back("foreground", foreground());
  o.emplace_back("alive", alive_);
  o.emplace_back("created", timeutil::iso8601(created_ms_));
  if (!alive_) o.emplace_back("exit", WIFEXITED(exit_status_) ? WEXITSTATUS(exit_status_) : -1);
  return json::Value(std::move(o));
}

}  // namespace apron::daemon
