// Scope meter: which files a run touched, and which left the task's scope.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace apron {

// Glob match on '/'-separated paths: '**' spans any number of segments
// (including none), '*' and '?' stay inside one segment.
bool glob_match(std::string_view pattern, std::string_view path);

// "src/**, tests/**" -> {"src/**", "tests/**"}
std::vector<std::string> split_scope(const std::string& scope);

struct ScopeReport {
  std::vector<std::string> files;    // touched, relative to the repo root
  std::vector<std::string> outside;  // touched and outside the scope
};

// Files Apron writes itself are never counted against a task.
bool always_in_scope(std::string_view path);

ScopeReport check_scope(const std::vector<std::string>& files, const std::vector<std::string>& scope);

// Runs a command in `dir` and returns its standard output, or nullopt on failure.
std::optional<std::string> capture(const std::string& command, const std::filesystem::path& dir);

std::optional<std::string> git_head(const std::filesystem::path& root);
// The commit HEAD pointed to at a moment, for runs that did not record a base.
std::optional<std::string> git_commit_at(const std::filesystem::path& root, int64_t ts_ms);
// Files changed since `base`: committed, staged, unstaged and untracked.
std::vector<std::string> git_changed_files(const std::filesystem::path& root, const std::string& base);

}  // namespace apron
