// Tests that go through the real apron and aprond binaries, the way a person
// or an agent uses them. The daemon test alone missed two CLI bugs in T6.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "proto/socket.hpp"
#include "test.hpp"

namespace fs = std::filesystem;

namespace {

std::string sh(const std::string& cmd) {
  std::string out;
  FILE* p = popen((cmd + " 2>&1").c_str(), "r");
  if (!p) return out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
  pclose(p);
  return out;
}

}  // namespace

TEST("cli: a run reported while aprond runs shows up in apron projects, linked to its session") {
  fs::path dir = fs::temp_directory_path() / ("apron-cli-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / "demo" / ".apron");
  std::ofstream(dir / "demo" / ".apron" / "tasks.md") << "- [ ] D1 Demo\n  scope: **\n  done-when: it shows up\n";
  std::string env = "cd " + (dir / "demo").string() + " && NO_COLOR=1 APRON_LANG=en APRON_SOCKET=" +
                    (dir / "d.sock").string() + " XDG_STATE_HOME=" + (dir / "state").string() +
                    " APRON_AGENT=claude APRON_MACHINE=box ";
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";

  // Without a daemon the run still works: the file is the source.
  CHECK(sh(env + apron + " run start D1 --intent demo").find("r-D1-") != std::string::npos);
  CHECK(sh(env + apron + " projects").find("aprond is not running") != std::string::npos);

  pid_t daemon = fork();
  if (daemon == 0) {
    setenv("APRON_SOCKET", (dir / "d.sock").c_str(), 1);
    setenv("XDG_STATE_HOME", (dir / "state").c_str(), 1);
    execl(APRON_BINARY_DIR "/aprond", "aprond", static_cast<char*>(nullptr));
    _exit(127);
  }
  for (int i = 0; i < 100 && !fs::exists(dir / "d.sock"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));

  sh(env + apron + " report ask 'may I change the schema?'");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::string projects = sh(env + apron + " projects");
  CHECK(projects.find("demo") != std::string::npos);
  CHECK(projects.find("◐ 1") != std::string::npos);
  CHECK(projects.find("may I change the schema?") != std::string::npos);

  // A run started inside a session records the session it lives in.
  sh(env + apron + " run finish done");
  sh(env + "APRON_SESSION=7 " + apron + " run start D2 --force");
  std::string runs = sh("cat " + (dir / "demo" / ".apron" / "runs").string() + "/r-D2-*.jsonl");
  CHECK(runs.find("\"session\":\"7\"") != std::string::npos);

  // The registry survives a daemon restart.
  sh(env + apron + " daemon stop");
  waitpid(daemon, nullptr, 0);
  std::ifstream reg(dir / "state" / "apron" / "projects");
  std::string line;
  std::getline(reg, line);
  CHECK_EQ(line, (dir / "demo").string());
  fs::remove_all(dir);
}
