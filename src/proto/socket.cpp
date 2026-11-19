#include "proto/socket.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace apron::proto {

std::filesystem::path socket_path() {
  if (const char* env = std::getenv("APRON_SOCKET"); env && *env) return env;
  std::filesystem::path dir;
  if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) dir = std::filesystem::path(xdg) / "apron";
  else {
    const char* tmp = std::getenv("TMPDIR");
    dir = std::filesystem::path(tmp && *tmp ? tmp : "/tmp") / ("apron-" + std::to_string(getuid()));
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  chmod(dir.c_str(), 0700);
  return dir / "aprond.sock";
}

static bool fill_addr(sockaddr_un& addr, const std::filesystem::path& path) {
  std::memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  std::string p = path.string();
  if (p.size() >= sizeof addr.sun_path) return false;
  std::memcpy(addr.sun_path, p.c_str(), p.size() + 1);
  return true;
}

int listen_unix(const std::filesystem::path& path, std::string* error) {
  sockaddr_un addr;
  if (!fill_addr(addr, path)) {
    if (error) *error = "socket path too long: " + path.string();
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) *error = std::strerror(errno);
    return -1;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  unlink(path.c_str());  // a stale socket from a crashed daemon
  mode_t old = umask(0177);
  int rc = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
  umask(old);
  if (rc != 0 || listen(fd, 16) != 0) {
    if (error) *error = std::strerror(errno);
    close(fd);
    return -1;
  }
  return fd;
}

int connect_unix(const std::filesystem::path& path) {
  sockaddr_un addr;
  if (!fill_addr(addr, path)) return -1;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool write_all(int fd, const std::string& bytes) {
  size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = ::write(fd, bytes.data() + off, bytes.size() - off);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

void set_nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

}  // namespace apron::proto
