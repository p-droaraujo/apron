// aprond's event loop: one thread, poll() over the socket, clients and PTYs.
#pragma once

#include <filesystem>

namespace apron::daemon {

// Runs until a client sends daemon.stop or the process gets SIGTERM.
int serve(const std::filesystem::path& socket);

}  // namespace apron::daemon
