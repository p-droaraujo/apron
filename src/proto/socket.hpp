// Unix sockets for talking to the local daemon.
#pragma once

#include <filesystem>
#include <string>

namespace apron::proto {

// APRON_SOCKET, else $XDG_RUNTIME_DIR/apron/aprond.sock, else
// $TMPDIR/apron-<uid>/aprond.sock. The directory is created with mode 0700.
std::filesystem::path socket_path();

int listen_unix(const std::filesystem::path& path, std::string* error);
int connect_unix(const std::filesystem::path& path);
bool write_all(int fd, const std::string& bytes);
void set_nonblocking(int fd);

}  // namespace apron::proto
