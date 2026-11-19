#include "core/scope.hpp"

#include <array>
#include <cstdio>
#include <set>
#include <sstream>

namespace apron {

namespace {

bool match_segment(std::string_view p, std::string_view s) {
  // Classic wildcard match inside one path segment.
  size_t pi = 0, si = 0, star = std::string_view::npos, mark = 0;
  while (si < s.size()) {
    if (pi < p.size() && (p[pi] == '?' || p[pi] == s[si])) {
      ++pi;
      ++si;
    } else if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = si;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      si = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') ++pi;
  return pi == p.size();
}

std::vector<std::string_view> segments(std::string_view path) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= path.size()) {
    size_t slash = path.find('/', start);
    if (slash == std::string_view::npos) slash = path.size();
    if (slash > start) out.push_back(path.substr(start, slash - start));
    start = slash + 1;
  }
  return out;
}

bool match_from(const std::vector<std::string_view>& p, size_t pi, const std::vector<std::string_view>& s, size_t si) {
  if (pi == p.size()) return si == s.size();
  if (p[pi] == "**") {
    for (size_t k = si; k <= s.size(); ++k)
      if (match_from(p, pi + 1, s, k)) return true;
    return false;
  }
  if (si == s.size()) return false;
  return match_segment(p[pi], s[si]) && match_from(p, pi + 1, s, si + 1);
}

std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

}  // namespace

bool glob_match(std::string_view pattern, std::string_view path) {
  return match_from(segments(pattern), 0, segments(path), 0);
}

std::vector<std::string> split_scope(const std::string& scope) {
  std::vector<std::string> out;
  std::stringstream ss(scope);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

bool always_in_scope(std::string_view path) { return path == ".apron/tasks.md"; }

ScopeReport check_scope(const std::vector<std::string>& files, const std::vector<std::string>& scope) {
  ScopeReport r;
  r.files = files;
  for (const auto& f : files) {
    if (always_in_scope(f)) continue;
    bool inside = false;
    for (const auto& g : scope) inside = inside || glob_match(g, f);
    if (!inside) r.outside.push_back(f);
  }
  return r;
}

std::optional<std::string> capture(const std::string& command, const std::filesystem::path& dir) {
  std::string full = "cd " + shell_quote(dir.string()) + " && " + command + " 2>/dev/null";
  FILE* pipe = popen(full.c_str(), "r");
  if (!pipe) return std::nullopt;
  std::string out;
  std::array<char, 4096> buf{};
  size_t n;
  while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) out.append(buf.data(), n);
  int status = pclose(pipe);
  if (status != 0) return std::nullopt;
  return out;
}

std::optional<std::string> git_head(const std::filesystem::path& root) {
  auto out = capture("git rev-parse HEAD", root);
  if (!out) return std::nullopt;
  std::string head = trim(*out);
  return head.empty() ? std::nullopt : std::optional<std::string>(head);
}

std::optional<std::string> git_commit_at(const std::filesystem::path& root, int64_t ts_ms) {
  auto out = capture("git rev-list -1 --before=@" + std::to_string(ts_ms / 1000) + " HEAD", root);
  if (!out) return std::nullopt;
  std::string commit = trim(*out);
  return commit.empty() ? std::nullopt : std::optional<std::string>(commit);
}

std::vector<std::string> git_changed_files(const std::filesystem::path& root, const std::string& base) {
  std::set<std::string> files;
  auto add_lines = [&](const std::optional<std::string>& text) {
    if (!text) return;
    std::stringstream ss(*text);
    std::string line;
    while (std::getline(ss, line)) {
      line = trim(line);
      if (!line.empty()) files.insert(line);
    }
  };
  // Committed since base, plus what is staged or unstaged in the working tree.
  add_lines(capture("git diff --name-only " + shell_quote(base), root));
  add_lines(capture("git diff --name-only " + shell_quote(base) + " HEAD", root));
  add_lines(capture("git ls-files --others --exclude-standard", root));
  return {files.begin(), files.end()};
}

}  // namespace apron
