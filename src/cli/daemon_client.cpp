#include "cli/daemon_client.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "cli/common.hpp"
#include "core/i18n.hpp"
#include "proto/socket.hpp"

namespace apron::cli {

namespace {

// aprond is installed next to apron; fall back to PATH.
std::string aprond_path() {
  char buf[PATH_MAX] = {0};
#if defined(__APPLE__)
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) buf[0] = 0;
#else
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) buf[n] = 0;
  else buf[0] = 0;
#endif
  if (buf[0]) {
    auto candidate = std::filesystem::path(buf).parent_path() / "aprond";
    if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
  }
  return "aprond";
}

}  // namespace

bool record(const Project& project, const Event& event, std::string* error) {
  if (!append_event(project.run_file(event.run), event, error)) return false;
  int fd = proto::connect_unix(proto::socket_path());
  if (fd < 0) return true;  // no daemon: the file is enough
  json::Object notice;
  notice.emplace_back("cmd", "events.push");
  notice.emplace_back("root", project.root.string());
  notice.emplace_back("event", event.to_json());
  proto::write_all(fd, proto::encode({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(notice)))}));
  close(fd);
  return true;
}

DaemonClient::~DaemonClient() {
  if (fd_ >= 0) close(fd_);
  if (ssh_err_ >= 0) close(ssh_err_);
  if (ssh_ > 0) {
    kill(ssh_, SIGTERM);
    waitpid(ssh_, nullptr, 0);
  }
}

bool DaemonClient::connect_ssh(const std::string& host, const std::string& remote, std::string* error) {
  std::vector<std::string> argv;
  const char* ssh = std::getenv("APRON_SSH");
  std::istringstream words(ssh && *ssh ? ssh : "ssh");
  for (std::string w; words >> w;) argv.push_back(w);
  for (const char* o : {"-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-o", "ServerAliveInterval=15"})
    argv.emplace_back(o);
  argv.push_back(host);
  argv.push_back(remote + " proxy");

  int sv[2], err[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;
  if (pipe(err) != 0) {
    close(sv[0]);
    close(sv[1]);
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    setsid();  // no terminal: ssh must never prompt over the TUI
    dup2(sv[1], STDIN_FILENO);
    dup2(sv[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);
    close(sv[0]);
    close(sv[1]);
    close(err[0]);
    close(err[1]);
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(a.data());
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(sv[1]);
  close(err[1]);
  fcntl(err[0], F_SETFL, O_NONBLOCK);
  fd_ = sv[0];
  ssh_ = pid;
  ssh_err_ = err[0];
  timeout_ms_ = 20000;  // ssh, then perhaps starting aprond over there
  json::Object hello;
  hello.emplace_back("cmd", "hello");
  std::string e;
  if (request(std::move(hello), &e)) return true;
  std::string why = ssh_error();
  if (error) *error = why.empty() ? e : why;
  return false;
}

std::string DaemonClient::ssh_error() const {
  if (ssh_err_ < 0) return "";
  std::string all;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(ssh_err_, buf, sizeof buf)) > 0) all.append(buf, static_cast<size_t>(n));
  while (!all.empty() && (all.back() == '\n' || all.back() == '\r')) all.pop_back();
  auto nl = all.find_last_of('\n');
  return nl == std::string::npos ? all : all.substr(nl + 1);  // the last line says why
}

bool DaemonClient::start_daemon(std::string* error) {
  auto socket = proto::socket_path();
  auto log = socket.parent_path() / "aprond.log";
  std::string exe = aprond_path();
  pid_t pid = fork();
  if (pid < 0) {
    if (error) *error = tr("could not start aprond");
    return false;
  }
  if (pid == 0) {
    // Detach completely: own session, no terminal, output to a log file.
    setsid();
    if (fork() != 0) _exit(0);
    int null = open("/dev/null", O_RDONLY);
    int out = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    dup2(null, STDIN_FILENO);
    dup2(out, STDOUT_FILENO);
    dup2(out, STDERR_FILENO);
    execlp(exe.c_str(), "aprond", static_cast<char*>(nullptr));
    _exit(127);
  }
  int status;
  waitpid(pid, &status, 0);
  for (int i = 0; i < 40; ++i) {
    fd_ = proto::connect_unix(socket);
    if (fd_ >= 0) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (error) *error = trf("aprond did not start; see {}", {log.string()});
  return false;
}

bool DaemonClient::connect(bool autostart, std::string* error) {
  fd_ = proto::connect_unix(proto::socket_path());
  if (fd_ >= 0) return true;
  if (!autostart) {
    if (error) *error = tr("aprond is not running");
    return false;
  }
  return start_daemon(error);
}

bool DaemonClient::send(const proto::Frame& frame) { return proto::write_all(fd_, proto::encode(frame)); }

std::optional<proto::Frame> DaemonClient::next(int timeout_ms) {
  if (!queued_.empty()) {
    auto f = std::move(queued_.front());
    queued_.erase(queued_.begin());
    return f;
  }
  while (true) {
    if (auto f = reader_.next()) return f;
    pollfd pfd{fd_, POLLIN, 0};
    int r = poll(&pfd, 1, timeout_ms);
    if (r <= 0) return std::nullopt;
    char buf[65536];
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n <= 0) return std::nullopt;
    reader_.feed(std::string_view(buf, static_cast<size_t>(n)));
  }
}

std::optional<json::Value> DaemonClient::request(json::Object body, std::string* error) {
  int id = next_id_++;
  body.insert(body.begin(), {"id", id});
  if (!send({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(body)))})) {
    if (error) *error = tr("lost the connection to aprond");
    return std::nullopt;
  }
  while (true) {
    auto f = next(timeout_ms_);
    if (!f) {
      if (error) *error = tr("aprond did not answer");
      return std::nullopt;
    }
    if (f->type == proto::Type::Ctrl) {
      auto v = json::parse(f->payload);
      const json::Value* rid = v ? v->find("id") : nullptr;
      if (rid && rid->is_number() && static_cast<int>(rid->num()) == id) {
        if (const json::Value* ok = v->find("ok"); ok && ok->is_bool() && !ok->boolean()) {
          if (error) *error = v->get_str("error");
          return std::nullopt;
        }
        return v;
      }
    }
    queued_.push_back(std::move(*f));  // not ours: keep it for the caller
  }
}

}  // namespace apron::cli
