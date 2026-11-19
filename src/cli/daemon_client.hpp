// The CLI side of the daemon connection: requests, replies and frames.
#pragma once

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
      : fd_(other.fd_), next_id_(other.next_id_), reader_(std::move(other.reader_)), queued_(std::move(other.queued_)) {
    other.fd_ = -1;
  }
  // Connects to the local aprond, starting it first if `autostart` and it is not running.
  bool connect(bool autostart, std::string* error);
  // Sends a control request and waits for its reply (other frames are queued).
  std::optional<json::Value> request(json::Object body, std::string* error);
  bool send(const proto::Frame& frame);
  // Next frame from the daemon, waiting up to `timeout_ms` (-1 = forever).
  std::optional<proto::Frame> next(int timeout_ms);
  int fd() const { return fd_; }

 private:
  int fd_ = -1;
  int next_id_ = 1;
  proto::Reader reader_;
  std::vector<proto::Frame> queued_;
  bool start_daemon(std::string* error);
};

}  // namespace apron::cli
