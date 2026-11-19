// aprond: the daemon that owns the terminals and agents of one machine.
#include <cstdio>
#include <cstring>

#include "daemon/server.hpp"
#include "proto/socket.hpp"

int main(int argc, char** argv) {
  if (argc > 1 && (!std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h"))) {
    std::puts("aprond — keeps this machine's sessions alive\n\n"
              "  aprond            run in the foreground on the default socket\n\n"
              "The socket is APRON_SOCKET, else $XDG_RUNTIME_DIR/apron/aprond.sock,\n"
              "else $TMPDIR/apron-<uid>/aprond.sock. `apron new` starts aprond on demand.");
    return 0;
  }
  return apron::daemon::serve(apron::proto::socket_path());
}
