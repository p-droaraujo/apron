#include "proto/frame.hpp"

namespace apron::proto {

namespace {

void put_u32(std::string& out, uint32_t v) {
  out += static_cast<char>((v >> 24) & 0xFF);
  out += static_cast<char>((v >> 16) & 0xFF);
  out += static_cast<char>((v >> 8) & 0xFF);
  out += static_cast<char>(v & 0xFF);
}

uint32_t get_u32(const std::string& s, size_t at) {
  auto b = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(s[at + i])); };
  return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);
}

}  // namespace

std::string encode(const Frame& frame) {
  std::string out;
  out.reserve(kHeaderSize + frame.payload.size());
  put_u32(out, static_cast<uint32_t>(frame.payload.size()));
  out += static_cast<char>(frame.type);
  put_u32(out, frame.channel);
  out += frame.payload;
  return out;
}

std::optional<Frame> Reader::next() {
  if (broken_ || buf_.size() < kHeaderSize) return std::nullopt;
  uint32_t len = get_u32(buf_, 0);
  uint8_t type = static_cast<uint8_t>(buf_[4]);
  if (len > kMaxPayload || type < 1 || type > 4) {
    broken_ = true;
    return std::nullopt;
  }
  if (buf_.size() < kHeaderSize + len) return std::nullopt;
  Frame f;
  f.type = static_cast<Type>(type);
  f.channel = get_u32(buf_, 5);
  f.payload = buf_.substr(kHeaderSize, len);
  buf_.erase(0, kHeaderSize + len);
  return f;
}

}  // namespace apron::proto
