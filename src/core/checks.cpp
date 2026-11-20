#include "core/checks.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sstream>

#include "core/i18n.hpp"
#include "core/time.hpp"

namespace apron {

namespace {

std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r");
  size_t b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Strips a # comment that is not inside a string.
std::string strip_comment(const std::string& line) {
  bool in_str = false;
  char quote = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (in_str) {
      if (c == '\\' && quote == '"') ++i;
      else if (c == quote) in_str = false;
    } else if (c == '"' || c == '\'') {
      in_str = true;
      quote = c;
    } else if (c == '#') {
      return line.substr(0, i);
    }
  }
  return line;
}

bool parse_string(const std::string& raw, std::string& out) {
  if (raw.size() < 2) return false;
  char q = raw.front();
  if ((q != '"' && q != '\'') || raw.back() != q) return false;
  std::string body = raw.substr(1, raw.size() - 2);
  if (q == '\'') {
    out = body;
    return true;
  }
  out.clear();
  for (size_t i = 0; i < body.size(); ++i) {
    if (body[i] == '\\' && i + 1 < body.size()) {
      char e = body[++i];
      out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
    } else {
      out += body[i];
    }
  }
  return true;
}

}  // namespace

std::vector<Check> parse_checks(const std::string& text, std::string* error) {
  std::vector<Check> out;
  std::stringstream ss(text);
  std::string line;
  int number = 0;
  bool in_check = false;
  while (std::getline(ss, line)) {
    ++number;
    line = trim(strip_comment(line));
    if (line.empty()) continue;
    if (line == "[[check]]") {
      out.emplace_back();
      in_check = true;
      continue;
    }
    if (line.front() == '[') {
      in_check = false;  // another table: not ours
      continue;
    }
    if (!in_check) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      if (error) *error = trf("checks.toml line {}: expected key = value", {std::to_string(number)});
      continue;
    }
    std::string key = trim(line.substr(0, eq));
    std::string raw = trim(line.substr(eq + 1));
    Check& c = out.back();
    std::string value;
    if (key == "name" && parse_string(raw, value)) c.name = value;
    else if (key == "run" && parse_string(raw, value)) c.run = value;
    else if (key == "timeout") c.timeout_s = std::max(1, std::atoi(raw.c_str()));
  }
  // A check without a name or a command is useless; drop it.
  std::vector<Check> valid;
  for (auto& c : out)
    if (!c.name.empty() && !c.run.empty()) valid.push_back(std::move(c));
  return valid;
}

std::optional<std::vector<Check>> load_checks(const std::filesystem::path& file, std::string* error) {
  std::ifstream in(file);
  if (!in) {
    if (error) *error = trf("cannot read {}", {file.string()});
    return std::nullopt;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return parse_checks(ss.str(), error);
}

CheckResult run_check(const Check& check, const std::filesystem::path& root, const Env& env) {
  CheckResult r;
  r.name = check.name;
  int64_t start = timeutil::now_ms();

  int pipefd[2];
  if (pipe(pipefd) != 0) return r;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return r;
  }
  if (pid == 0) {
    setpgid(0, 0);  // own process group, so a timeout can stop the whole tree
    int null = open("/dev/null", O_RDONLY);  // a check never reads the person's terminal
    dup2(null, STDIN_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    for (const auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
    if (chdir(root.c_str()) != 0) _exit(127);
    execl("/bin/sh", "sh", "-c", check.run.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  close(pipefd[1]);

  // Keep only the last lines: enough to see why it failed.
  std::deque<std::string> lines;
  std::string partial;
  char buf[4096];
  int64_t deadline = start + static_cast<int64_t>(check.timeout_s) * 1000;
  while (true) {
    int64_t left = deadline - timeutil::now_ms();
    if (left <= 0) {
      r.timed_out = true;
      kill(-pid, SIGKILL);
      break;
    }
    pollfd pfd{pipefd[0], POLLIN, 0};
    int pr = poll(&pfd, 1, static_cast<int>(std::min<int64_t>(left, 1000)));
    if (pr < 0) break;
    if (pr == 0) continue;
    ssize_t n = read(pipefd[0], buf, sizeof buf);
    if (n <= 0) break;
    partial.append(buf, static_cast<size_t>(n));
    size_t nl;
    while ((nl = partial.find('\n')) != std::string::npos) {
      lines.push_back(partial.substr(0, nl));
      partial.erase(0, nl + 1);
      if (lines.size() > 20) lines.pop_front();
    }
  }
  if (!partial.empty()) lines.push_back(partial);
  close(pipefd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  r.ms = timeutil::now_ms() - start;
  r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  r.ok = !r.timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!r.ok)
    for (const auto& l : lines) r.tail += l + "\n";
  return r;
}

}  // namespace apron
