// A session: a terminal the daemon created, with whatever runs inside it.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace apron::daemon {

class Session {
 public:
  static Session* spawn(uint32_t id, const std::vector<std::string>& argv, const std::string& cwd, int cols, int rows,
                        std::string* error);
  ~Session();

  uint32_t id() const { return id_; }
  int fd() const { return master_; }
  pid_t pid() const { return pid_; }
  bool alive() const { return alive_; }

  // Reads what the program wrote; keeps the tail for clients that attach later.
  std::string read_output();
  // Input is queued and written as the program reads it: an agent that stops
  // reading its terminal cannot block the daemon.
  void write_input(const std::string& bytes);
  void flush_input();
  bool has_pending_input() const { return !pending_in_.empty(); }
  void resize(int cols, int rows);
  void kill_tree();
  // Called when the child exits.
  void mark_exited(int status);

  // The last bytes of output (up to 256 KB), replayed on attach.
  std::string replay() const;
  // The program in the foreground: the agent, when one is running.
  std::string foreground() const;
  json::Value describe() const;

 private:
  uint32_t id_ = 0;
  int master_ = -1;
  pid_t pid_ = -1;
  bool alive_ = true;
  int exit_status_ = 0;
  std::string command_;
  std::string cwd_;
  int64_t created_ms_ = 0;
  std::string replay_;  // the last bytes of output, replayed on attach
  std::string pending_in_;
};

}  // namespace apron::daemon
