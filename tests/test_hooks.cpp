// The Claude Code hooks, driven through the real apron binary with the JSON
// Claude Code sends on stdin.
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "test.hpp"

namespace fs = std::filesystem;

namespace {

struct Project {
  fs::path dir;
  std::string env;
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";

  Project() {
    dir = fs::temp_directory_path() / ("apron-hook-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / ".apron");
    std::ofstream(dir / ".apron" / "tasks.md") << "- [ ] H1 Hooks\n  scope: **\n  done-when: they work\n";
    env = "cd " + dir.string() + " && APRON_SOCKET=" + (dir / "none.sock").string() +
          " APRON_AGENT=claude APRON_MACHINE=box ";
    run(apron + " run start H1 --intent hooks");
  }
  ~Project() { fs::remove_all(dir); }

  std::string run(const std::string& cmd) {
    std::string out;
    FILE* p = popen((env + cmd + " 2>&1").c_str(), "r");
    char buf[4096];
    size_t n;
    while (p && (n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    if (p) pclose(p);
    return out;
  }
  // Sends one hook event; returns what the hook printed.
  std::string hook(const std::string& json, const std::string& extra_env = "") {
    std::ofstream(dir / "in.json") << json;
    return run(extra_env + apron + " hook < in.json");
  }
  std::string events() {
    std::string all;
    for (const auto& e : fs::directory_iterator(dir / ".apron" / "runs"))
      if (e.path().extension() == ".jsonl") {
        std::ifstream in(e.path());
        std::stringstream ss;
        ss << in.rdbuf();
        all += ss.str();
      }
    return all;
  }
  int count(const std::string& needle) {
    std::string all = events();
    int n = 0;
    for (size_t p = 0; (p = all.find(needle, p)) != std::string::npos; ++p) ++n;
    return n;
  }
  char task_state() {
    std::ifstream in(dir / ".apron" / "tasks.md");
    std::string line;
    std::getline(in, line);
    return line.size() > 3 ? line[3] : '?';
  }
};

std::string pre(const std::string& tool, const std::string& command = "") {
  return R"({"hook_event_name":"PreToolUse","tool_name":")" + tool + R"(","tool_input":{"command":")" + command + R"("}})";
}

}  // namespace

TEST("hooks: one step per phase change, and the agent's own apron calls do not count") {
  Project p;
  p.hook(pre("Read"));
  p.hook(pre("Grep"));  // same phase: nothing new
  p.hook(pre("Edit"));
  p.hook(pre("Bash", "./build/apron report step \\\"x\\\""));
  p.hook(pre("Bash", "cmake --build build"));
  p.hook(pre("Bash", "git commit -m \\\"add test\\\""));  // not a test run
  p.hook(pre("Bash", "ctest --test-dir build"));
  CHECK_EQ(p.count("\"kind\":\"step\""), 4);  // read, edit, build, test
  CHECK_EQ(p.count("builds the project"), 1);
}

TEST("hooks: a permission prompt is an ask; approving it takes the card out of gates") {
  Project p;
  p.hook(R"({"hook_event_name":"Notification","notification_type":"permission_prompt","message":"Claude needs your permission to use Bash"})");
  CHECK_EQ(p.count("\"kind\":\"ask\""), 1);
  CHECK(p.task_state() == '?');
  p.hook(R"({"hook_event_name":"Notification","notification_type":"permission_prompt","message":"again"})");
  CHECK_EQ(p.count("\"kind\":\"ask\""), 1);  // no second ask while one is open
  p.hook(R"({"hook_event_name":"PostToolUse","tool_name":"Bash","tool_input":{"command":"ls"}})");
  CHECK_EQ(p.count("approves and Claude Code resumes"), 1);
  CHECK(p.task_state() == '~');
}

TEST("hooks: the agent's own apron report ask is not undone by PostToolUse") {
  Project p;
  p.run(p.apron + " report ask 'may I change the schema?'");
  p.hook(R"({"hook_event_name":"PostToolUse","tool_name":"Bash","tool_input":{"command":"apron report ask 'may I change the schema?'"}})");
  CHECK_EQ(p.count("resumes"), 0);
  CHECK(p.task_state() == '?');
  p.hook(R"({"hook_event_name":"UserPromptSubmit","prompt":"yes"})");
  CHECK_EQ(p.count("answers and Claude Code resumes"), 1);
  CHECK(p.task_state() == '~');
}

TEST("hooks: Stop nudges once, asks the second time, and can be turned off") {
  Project p;
  std::string out = p.hook(R"({"hook_event_name":"Stop","stop_hook_active":false})");
  CHECK(out.find("\"decision\":\"block\"") != std::string::npos);
  CHECK(out.find("apron run finish") != std::string::npos);
  CHECK(p.hook(R"({"hook_event_name":"Stop"})", "APRON_HOOK_STOP=0 ").empty());
  p.hook(R"({"hook_event_name":"Stop","stop_hook_active":true})");
  CHECK_EQ(p.count("stops with the run open"), 1);
  CHECK(p.hook(R"({"hook_event_name":"Stop","stop_hook_active":false})").empty());  // an ask is open now
}

TEST("hooks: SessionEnd blocks an open run; garbage and other folders do nothing") {
  Project p;
  p.hook(R"({"hook_event_name":"SessionEnd"})");
  CHECK_EQ(p.count("\"kind\":\"blocked\""), 1);
  CHECK(p.hook("not json at all").empty());
  CHECK(p.hook(R"({"hook_event_name":"PreToolUse","tool_name":"Read","cwd":"/"})").empty());
}
