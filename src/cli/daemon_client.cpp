#include "cli/daemon_client.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <thread>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

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

DaemonClient::~DaemonClient() {
  if (fd_ >= 0) close(fd_);
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
    auto f = next(5000);
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
