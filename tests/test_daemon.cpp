#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include "core/json.hpp"
#include "proto/frame.hpp"
#include "proto/socket.hpp"
#include "test.hpp"

using namespace apron;
namespace fs = std::filesystem;

TEST("proto: frames survive being split at any byte") {
  std::string wire = proto::encode({proto::Type::Ctrl, 0, R"({"cmd":"hello"})"}) +
                     proto::encode({proto::Type::Data, 7, std::string("\x00\x01\xff", 3)});
  for (size_t cut = 0; cut <= wire.size(); ++cut) {
    proto::Reader r;
    r.feed(std::string_view(wire).substr(0, cut));
    r.feed(std::string_view(wire).substr(cut));
    auto a = r.next(), b = r.next();
    CHECK(a && a->type == proto::Type::Ctrl && a->payload == R"({"cmd":"hello"})");
    CHECK(b && b->channel == 7 && b->payload == std::string("\x00\x01\xff", 3));
    CHECK(!r.next());
  }
  proto::Reader bad;
  bad.feed(std::string("\xff\xff\xff\xff\x01\x00\x00\x00\x00", 9));  // 4 GB payload
  CHECK(!bad.next());
  CHECK(bad.broken());
}

namespace {

struct Conn {
  int fd = -1;
  proto::Reader reader;
  int next_id = 1;

  std::optional<proto::Frame> next(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      if (auto f = reader.next()) return f;
      int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      deadline - std::chrono::steady_clock::now())
                                      .count());
      if (left <= 0) return std::nullopt;
      pollfd p{fd, POLLIN, 0};
      if (poll(&p, 1, left) <= 0) return std::nullopt;
      char buf[8192];
      ssize_t n = read(fd, buf, sizeof buf);
      if (n <= 0) return std::nullopt;
      reader.feed(std::string_view(buf, static_cast<size_t>(n)));
    }
  }

  std::optional<json::Value> ask(json::Object body) {
    int id = next_id++;
    body.insert(body.begin(), {"id", id});
    proto::write_all(fd, proto::encode({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(body)))}));
    while (auto f = next(3000)) {
      if (f->type != proto::Type::Ctrl) continue;
      auto v = json::parse(f->payload);
      if (v && v->find("id") && static_cast<int>(v->find("id")->num()) == id) return v;
    }
    return std::nullopt;
  }
};

}  // namespace

TEST("aprond: spawn, list with the foreground program, attach, kill, stop") {
  fs::path dir = fs::temp_directory_path() / ("apron-d-" + std::to_string(::getpid()));
  fs::create_directories(dir);
  fs::path sock = dir / "aprond.sock";
  pid_t daemon = fork();
  if (daemon == 0) {
    setenv("APRON_SOCKET", sock.c_str(), 1);
    execl(APRON_BINARY_DIR "/aprond", "aprond", static_cast<char*>(nullptr));
    _exit(127);
  }
  Conn c;
  for (int i = 0; i < 100 && c.fd < 0; ++i) {
    c.fd = proto::connect_unix(sock);
    if (c.fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(c.fd >= 0);
  if (c.fd < 0) {
    kill(daemon, SIGTERM);
    waitpid(daemon, nullptr, 0);
    return;
  }

  auto hello = c.ask({{"cmd", "hello"}});
  CHECK(hello && hello->find("version") && hello->find("version")->num() == proto::kProtocolVersion);

  json::Array argv{"sh", "-c", "echo hello-from-apron; exec sleep 5"};
  auto spawned = c.ask({{"cmd", "sessions.spawn"}, {"argv", argv}, {"cwd", dir.string()}});
  CHECK(spawned && spawned->find("session"));
  long long id = spawned ? static_cast<long long>(spawned->find("session")->num()) : 0;

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto list = c.ask({{"cmd", "sessions.list"}});
  CHECK(list && list->find("sessions") && list->find("sessions")->arr().size() == 1);
  if (list && !list->find("sessions")->arr().empty()) {
    const json::Value& s = list->find("sessions")->arr()[0];
    CHECK_EQ(s.get_str("foreground"), std::string("sleep"));  // the agent, in a real session
    CHECK_EQ(s.get_str("cwd"), dir.string());
  }

  auto attached = c.ask({{"cmd", "sessions.attach"}, {"session", id}});
  CHECK(attached.has_value());
  std::string seen;
  while (auto f = c.next(1000)) {
    if (f->type == proto::Type::Data) seen += f->payload;
    if (seen.find("hello-from-apron") != std::string::npos) break;
  }
  CHECK(seen.find("hello-from-apron") != std::string::npos);  // replayed on attach

  CHECK(c.ask({{"cmd", "sessions.kill"}, {"session", id}}).has_value());
  bool exited = false;
  while (auto f = c.next(2000)) {
    auto v = f->type == proto::Type::Ctrl ? json::parse(f->payload) : std::nullopt;
    if (v && v->get_str("event") == "session.exited") {
      exited = true;
      break;
    }
  }
  CHECK(exited);
  auto missing = c.ask({{"cmd", "sessions.attach"}, {"session", 999}});
  CHECK(missing && missing->get_str("error") == "not_found");

  c.ask({{"cmd", "daemon.stop"}});
  int status = 0;
  waitpid(daemon, &status, 0);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CHECK(!fs::exists(sock));
  close(c.fd);
  fs::remove_all(dir);
}

TEST("aprond: a client that stops reading cannot freeze the daemon") {
  fs::path dir = fs::temp_directory_path() / ("apron-stall-" + std::to_string(::getpid()));
  fs::create_directories(dir);
  fs::path sock = dir / "aprond.sock";
  pid_t daemon = fork();
  if (daemon == 0) {
    setenv("APRON_SOCKET", sock.c_str(), 1);
    execl(APRON_BINARY_DIR "/aprond", "aprond", static_cast<char*>(nullptr));
    _exit(127);
  }
  Conn slow, other;
  for (int i = 0; i < 100 && slow.fd < 0; ++i) {
    slow.fd = proto::connect_unix(sock);
    if (slow.fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  other.fd = proto::connect_unix(sock);
  CHECK(slow.fd >= 0 && other.fd >= 0);

  // A session that floods its terminal, and a client attached to it that never reads.
  json::Array argv{"yes", "a line of output that keeps coming"};
  auto spawned = slow.ask({{"cmd", "sessions.spawn"}, {"argv", argv}, {"cwd", dir.string()}});
  long long id = spawned ? static_cast<long long>(spawned->find("session")->num()) : 0;
  json::Object attach{{"id", 99}, {"cmd", "sessions.attach"}, {"session", id}};
  proto::write_all(slow.fd, proto::encode({proto::Type::Ctrl, 0, json::dump(json::Value(std::move(attach)))}));
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // well past any socket buffer

  // Everyone else is still served, quickly.
  auto t0 = std::chrono::steady_clock::now();
  auto list = other.ask({{"cmd", "sessions.list"}});
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(list.has_value());
  CHECK(ms < 1000);

  other.ask({{"cmd", "sessions.kill"}, {"session", id}});
  other.ask({{"cmd", "daemon.stop"}});
  waitpid(daemon, nullptr, 0);
  close(slow.fd);
  close(other.fd);
  fs::remove_all(dir);
}
