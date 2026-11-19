// Wire protocol frames: a fixed 9-byte header, then the payload.
//   u32 length (payload bytes, big endian) | u8 type | u32 channel (big endian)
// Channel 0 carries control messages; each attached session gets its own channel.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apron::proto {

enum class Type : uint8_t { Ctrl = 1, Data = 2, Snap = 3, Ping = 4 };

inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr size_t kHeaderSize = 9;
inline constexpr uint32_t kMaxPayload = 16 * 1024 * 1024;

struct Frame {
  Type type = Type::Ctrl;
  uint32_t channel = 0;
  std::string payload;
};

std::string encode(const Frame& frame);

// Accumulates bytes from a stream and hands out whole frames.
class Reader {
 public:
  void feed(std::string_view bytes) { buf_.append(bytes); }
  // Next complete frame, or nullopt if more bytes are needed.
  std::optional<Frame> next();
  bool broken() const { return broken_; }  // an oversized or unknown frame

 private:
  std::string buf_;
  bool broken_ = false;
};

}  // namespace apron::proto
