#include "core/time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace apron::timeutil {

int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::tm to_utc(int64_t ms) {
  std::time_t secs = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  gmtime_r(&secs, &tm);
  return tm;
}

std::string iso8601(int64_t ms) {
  std::tm tm = to_utc(ms);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
  return buf;
}

std::optional<int64_t> parse_iso8601(std::string_view text) {
  int y, mo, d, h, mi, s, frac = 0;
  std::string copy(text);
  int n = std::sscanf(copy.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%3dZ", &y, &mo, &d, &h, &mi, &s, &frac);
  if (n < 6) return std::nullopt;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = s;
  std::time_t secs = timegm(&tm);
  return static_cast<int64_t>(secs) * 1000 + (n == 7 ? frac : 0);
}

std::string short_duration(int64_t ms) {
  if (ms < 0) ms = 0;
  int64_t s = ms / 1000;
  char buf[32];
  if (s < 60) std::snprintf(buf, sizeof buf, "%llds", static_cast<long long>(s));
  else if (s < 3600) std::snprintf(buf, sizeof buf, "%lldm%02llds", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
  else std::snprintf(buf, sizeof buf, "%lldh%02lldm", static_cast<long long>(s / 3600), static_cast<long long>((s % 3600) / 60));
  return buf;
}

std::string compact_stamp(int64_t ms) {
  std::tm tm = to_utc(ms);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
  return buf;
}

}  // namespace apron::timeutil
