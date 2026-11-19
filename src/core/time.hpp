#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace apron::timeutil {

// Milissegundos desde a época, em UTC.
int64_t now_ms();
// "2026-11-19T20:31:04.120Z"
std::string iso8601(int64_t ms);
std::optional<int64_t> parse_iso8601(std::string_view text);
// "4s", "3m12s", "1h05m": a forma curta que a interface usa.
std::string short_duration(int64_t ms);
// "20261119-203104", usado nos ids de execução.
std::string compact_stamp(int64_t ms);

}  // namespace apron::timeutil
