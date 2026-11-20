// The CLI side of the daemon connection: requests, replies and frames.
#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <string>

#include "core/json.hpp"
#include "proto/frame.hpp"

namespace apron::cli {

class DaemonClient {
 public:
  DaemonClient() = default;
  ~DaemonClient();
  // Owns a socket: movable, never copied (a copy would close it twice).
  DaemonClient(const DaemonClient&) = delete;
  DaemonClient& operator=(const DaemonClient&) = delete;
  DaemonClient(DaemonClient&& other) noexcept
      : fd_(other.fd_), ssh_(other.ssh_), ssh_err_(other.ssh_err_), timeout_ms_(other.timeout_ms_),
        next_id_(other.next_id_), reader_(std::move(other.reader_)), queued_(std::move(other.queued_)) {
    other.fd_ = other.ssh_err_ = -1;
    other.ssh_ = -1;
  }
  // Connects to the local aprond, starting it first if `autostart` and it is not running.
  bool connect(bool autostart, std::string* error);
  // Connects to the aprond of another machine: runs `ssh <host> <remote> proxy`
  // (APRON_SSH replaces `ssh`) and speaks the same protocol through it.
  bool connect_ssh(const std::string& host, const std::string& remote, std::string* error);
  // Sends a control request and waits for its reply (other frames are queued).
  std::optional<json::Value> request(json::Object body, std::string* error);
  bool send(const proto::Frame& frame);
  // Next frame from the daemon, waiting up to `timeout_ms` (-1 = forever).
  std::optional<proto::Frame> next(int timeout_ms);
  int fd() const { return fd_; }

 private:
  int fd_ = -1;
  int ssh_ = -1;      // pid of the ssh child, if any
  int ssh_err_ = -1;  // its stderr, read for the reason when it fails
  int timeout_ms_ = 5000;
  std::string ssh_error() const;
  int next_id_ = 1;
  proto::Reader reader_;
  std::vector<proto::Frame> queued_;
  bool start_daemon(std::string* error);
};

// Other machines, one per line in ~/.config/apron/machines: an ssh host
// (a name from ~/.ssh/config works) and, optionally, the apron command there.
struct Machine {
  std::string host;
  std::string remote = "apron";
};
std::filesystem::path machines_file();
std::vector<Machine> load_machines();
const Machine* find_machine(const std::vector<Machine>& all, const std::string& host);
// Adds a host to the list (or updates its command); returns an error, or "".
std::string add_machine(const std::string& host, const std::string& remote = "apron");

// The local aprond when `host` is empty, else that machine's through ssh.
// Prints the reason and returns nothing when it cannot connect.
std::optional<DaemonClient> connect_to(const std::string& host, bool autostart);

}  // namespace apron::cli
