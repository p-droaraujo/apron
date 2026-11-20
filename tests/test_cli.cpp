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

#include "proto/frame.hpp"
#include "proto/socket.hpp"
#include "core/scope.hpp"
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
  CHECK_EQ(line, fs::canonical(dir / "demo").string());  // /var is /private/var on macOS
  fs::remove_all(dir);
}

TEST("cli: machines over ssh: add, list, ls, attach, unreachable, and events.recent") {
  fs::path dir = fs::temp_directory_path() / ("apron-ssh-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / "demo" / ".apron");
  std::ofstream(dir / "demo" / ".apron" / "tasks.md") << "- [ ] D1 Demo\n  scope: **\n  done-when: it shows up\n";
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";
  std::string here = "NO_COLOR=1 APRON_LANG=en APRON_SOCKET=" + (dir / "here.sock").string() +
                     " XDG_CONFIG_HOME=" + (dir / "config").string() + " APRON_FAKE_SSH_DIR=" + dir.string() +
                     " APRON_SSH=" + std::string(APRON_SOURCE_DIR) + "/tests/fake-ssh.sh ";
  // "box" is another machine: its own aprond, its own socket and state.
  std::string box = "cd " + (dir / "demo").string() + " && NO_COLOR=1 APRON_LANG=en APRON_SOCKET=" +
                    (dir / "box.sock").string() + " XDG_STATE_HOME=" + (dir / "box-state").string() +
                    " APRON_AGENT=claude APRON_MACHINE=box ";

  CHECK(sh(here + apron + " machines").find("no machines yet") != std::string::npos);
  // Adding it starts its aprond through the proxy, like ssh would on a real machine.
  std::string added = sh(here + apron + " machines add box " + apron);
  CHECK(added.find("● box") != std::string::npos);
  CHECK(fs::exists(dir / "box.sock"));

  sh(box + apron + " new -d -- sh -c 'echo REMOTE-OK; exec sleep 30'");
  sh(box + apron + " run start D1 --intent 'over there'");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(sh(here + apron + " ls box").find("sleep") != std::string::npos);
  CHECK(sh(here + apron + " machines").find("1 running · 1 projects") != std::string::npos);
  CHECK(sh(here + apron + " attach box:1 </dev/null").find("REMOTE-OK") != std::string::npos);  // the replay

  std::string down = sh(here + apron + " machines add down; echo rc=$?");
  CHECK(down.find("Connection refused") != std::string::npos);
  CHECK(down.find("rc=1") != std::string::npos);
  sh(here + apron + " machines rm down");
  CHECK(sh(here + apron + " machines").find("down") == std::string::npos);

  // events.recent: the newest events of the machine's projects, for a TUI elsewhere.
  int fd = apron::proto::connect_unix(dir / "box.sock");
  CHECK(fd >= 0);
  apron::proto::write_all(fd, apron::proto::encode({apron::proto::Type::Ctrl, 0, R"({"id":1,"cmd":"events.recent","limit":10})"}));
  apron::proto::Reader reader;
  std::string got;
  char buf[65536];
  for (int i = 0; i < 50 && got.empty(); ++i) {
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) break;
    reader.feed(std::string_view(buf, static_cast<size_t>(n)));
    if (auto f = reader.next()) got = f->payload;
  }
  close(fd);
  CHECK(got.find("\"project\":\"demo\"") != std::string::npos);
  CHECK(got.find("over there") != std::string::npos);

  sh(box + apron + " daemon stop");
  fs::remove_all(dir);
}

TEST("cli: checks get the run's context, and apron-verify follows the agent's verdict") {
  fs::path dir = fs::temp_directory_path() / ("apron-verify-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / ".apron");
  std::string verify = std::string(APRON_SOURCE_DIR) + "/integrations/verify/apron-verify.sh";
  std::ofstream(dir / ".apron" / "tasks.md") << "- [ ] V1 Greet\n  scope: **\n  done-when: hello.txt says hello\n";
  std::ofstream(dir / ".apron" / "checks.toml")
      << "[[check]]\nname = \"env\"\nrun = \"test -n \\\"$APRON_BASE\\\" && echo \\\"$APRON_TASK_TEXT\\\" | grep -q done-when\"\n"
      << "[[check]]\nname = \"verify\"\nrun = \"" << verify << "\"\n"
      << "[[check]]\nname = \"security\"\nrun = \"" << verify << " security\"\n";
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";
  std::string in = "cd " + dir.string() + " && NO_COLOR=1 APRON_LANG=en APRON_SOCKET=" + (dir / "none.sock").string() +
                   " APRON_AGENT=claude APRON_MACHINE=box ";
  sh(in + "git init -q && git -c user.email=t@t -c user.name=t commit -q --allow-empty -m base");
  sh(in + apron + " run start V1");
  std::ofstream(dir / "hello.txt") << "hello\n";
  std::string prompt = (dir / "prompt.txt").string();
  auto agent = [&](const std::string& answer) {
    return "APRON_VERIFY_AGENT=\"tee " + prompt + " >/dev/null; echo looks right; echo '" + answer + "'\" ";
  };

  std::string pass = sh(in + agent("VERDICT: PASS") + apron + " check env verify; echo rc=$?");
  CHECK(pass.find("✓ env") != std::string::npos);
  CHECK(pass.find("✓ verify") != std::string::npos);
  CHECK(pass.find("rc=0") != std::string::npos);
  std::ifstream p(prompt);
  std::string text((std::istreambuf_iterator<char>(p)), {});
  CHECK(text.find("done-when: hello.txt says hello") != std::string::npos);  // the task
  CHECK(text.find("hello.txt") != std::string::npos);                        // the new file
  CHECK(text.find("did not see how") != std::string::npos);

  CHECK(sh(in + agent("VERDICT: FAIL") + apron + " check verify; echo rc=$?").find("rc=1") != std::string::npos);
  CHECK(sh(in + agent("maybe?") + apron + " check verify").find("no VERDICT line") != std::string::npos);
  sh(in + agent("VERDICT: PASS") + apron + " check security");
  std::ifstream p2(prompt);
  std::string text2((std::istreambuf_iterator<char>(p2)), {});
  CHECK(text2.find("Review only security") != std::string::npos);
  fs::remove_all(dir);
}

TEST("cli: run start warns when its scope meets a run in progress") {
  using apron::globs_may_overlap;
  CHECK(globs_may_overlap("src/**", "src/cli/**"));
  CHECK(globs_may_overlap("src/cli/main.cpp", "src/**"));
  CHECK(globs_may_overlap("**", "docs/**"));
  CHECK(!globs_may_overlap("docs/**", "src/**"));
  CHECK(!globs_may_overlap("README.md", "src/**"));
  CHECK(!globs_may_overlap("web/**", "api/**"));

  fs::path dir = fs::temp_directory_path() / ("apron-lanes-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir / ".apron");
  std::ofstream(dir / ".apron" / "tasks.md") << "- [ ] W1 Web\n  scope: web/**\n  done-when: x\n"
                                             << "- [ ] A1 Api\n  scope: api/**\n  done-when: x\n"
                                             << "- [ ] B1 Both\n  scope: api/auth/**, README.md\n  done-when: x\n";
  std::string apron = std::string(APRON_BINARY_DIR) + "/apron";
  std::string in = "cd " + dir.string() + " && NO_COLOR=1 APRON_LANG=en APRON_SOCKET=" + (dir / "none.sock").string() +
                   " APRON_AGENT=claude APRON_MACHINE=box ";
  sh(in + "git init -q");
  // Each run in its own worktree in real life; here one after another, clearing "current".
  CHECK(sh(in + apron + " run start W1 2>&1").find("warning") == std::string::npos);
  fs::remove(dir / ".apron" / "runs" / "current");
  CHECK(sh(in + apron + " run start A1 2>&1").find("warning") == std::string::npos);  // web and api: clean lanes
  fs::remove(dir / ".apron" / "runs" / "current");
  std::string both = sh(in + apron + " run start B1 2>&1");
  CHECK(both.find("scope meets A1") != std::string::npos);
  CHECK(both.find("scope meets W1") == std::string::npos);
  CHECK(sh(in + apron + " log").find("scope meets A1") != std::string::npos);  // recorded in the run
  fs::remove_all(dir);
}
