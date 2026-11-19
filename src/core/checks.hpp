// checks.toml: the commands that say whether the work is good.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apron {

struct Check {
  std::string name;
  std::string run;           // shell command, run from the project root
  int timeout_s = 600;
};

// Reads the [[check]] tables of a checks.toml. Only the subset Apron writes is
// supported: string and integer values, # comments.
std::optional<std::vector<Check>> load_checks(const std::filesystem::path& file, std::string* error = nullptr);
std::vector<Check> parse_checks(const std::string& text, std::string* error = nullptr);

struct CheckResult {
  std::string name;
  bool ok = false;
  int exit_code = -1;
  int64_t ms = 0;
  bool timed_out = false;
  std::string tail;  // last lines of output, kept when it fails
};

CheckResult run_check(const Check& check, const std::filesystem::path& root);

}  // namespace apron
