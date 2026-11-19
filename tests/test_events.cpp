#include <unistd.h>

#include <filesystem>

#include "core/events.hpp"
#include "core/runs.hpp"
#include "core/time.hpp"
#include "test.hpp"

using namespace apron;
namespace fs = std::filesystem;

namespace {

fs::path temp_dir() {
  fs::path dir = fs::temp_directory_path() / ("apron-test-" + std::to_string(::getpid()) + "-" +
                                              std::to_string(timeutil::now_ms()));
  fs::create_directories(dir);
  return dir;
}

Event make(Kind kind, int64_t ts, std::string msg = "") {
  Event e;
  e.ts_ms = ts;
  e.run = "r-T1-1";
  e.task = "T1";
  e.agent = "claude";
  e.machine = "nexus";
  e.kind = kind;
  e.msg = std::move(msg);
  return e;
}

}  // namespace

TEST("time: iso8601 round trip and short durations") {
  int64_t t = 1795120264120;  // 2026-11-19T20:31:04.120Z
  CHECK_EQ(timeutil::iso8601(t), std::string("2026-11-19T20:31:04.120Z"));
  CHECK_EQ(timeutil::parse_iso8601("2026-11-19T20:31:04.120Z").value_or(0), t);
  CHECK_EQ(timeutil::short_duration(4200), std::string("4s"));
  CHECK_EQ(timeutil::short_duration(192000), std::string("3m12s"));
  CHECK_EQ(timeutil::short_duration(3900000), std::string("1h05m"));
}

TEST("events: write and read keep extra fields") {
  fs::path file = temp_dir() / "r.jsonl";
  Event a = make(Kind::RunStarted, 1000);
  a.extra.emplace_back("intent", "fazer o parser");
  Event b = make(Kind::Blocked, 2000, "fila da GPU cheia");
  b.extra.emplace_back("cause", "gpu");
  CHECK(append_event(file, a));
  CHECK(append_event(file, b));
  auto events = load_events(file);
  CHECK_EQ(events.size(), size_t(2));
  CHECK(events[0].kind == Kind::RunStarted);
  CHECK_EQ(events[0].extra_str("intent"), std::string("fazer o parser"));
  CHECK_EQ(events[1].extra_str("cause"), std::string("gpu"));
  CHECK_EQ(events[1].msg, std::string("fila da GPU cheia"));
  fs::remove_all(file.parent_path());
}

TEST("events: corrupt lines become warnings, not errors") {
  fs::path file = temp_dir() / "r.jsonl";
  CHECK(append_event(file, make(Kind::Step, 1000, "um")));
  { FILE* f = std::fopen(file.c_str(), "a"); std::fputs("{quebrado\n", f); std::fclose(f); }
  CHECK(append_event(file, make(Kind::Step, 2000, "dois")));
  std::vector<std::string> warnings;
  auto events = load_events(file, &warnings);
  CHECK_EQ(events.size(), size_t(2));
  CHECK_EQ(warnings.size(), size_t(1));
  fs::remove_all(file.parent_path());
}

TEST("summary: state follows the last event that matters") {
  std::vector<Event> ev{make(Kind::RunStarted, 0), make(Kind::Step, 10, "a")};
  CHECK(summarize("r", ev).state == RunState::Working);
  ev.push_back(make(Kind::Ask, 20, "pode mudar o schema?"));
  CHECK(summarize("r", ev).state == RunState::Waiting);
  ev.push_back(make(Kind::Note, 25, "o schema mora em migrations/"));
  CHECK_EQ(summarize("r", ev).last_msg, std::string("pode mudar o schema?"));
  ev.push_back(make(Kind::Step, 30, "seguiu com a resposta"));
  CHECK(summarize("r", ev).state == RunState::Working);
  Event check = make(Kind::Check, 40);
  check.extra.emplace_back("ok", false);
  ev.push_back(check);
  Event fin = make(Kind::RunFinished, 50, "pronto");
  fin.extra.emplace_back("result", "failed");
  ev.push_back(fin);
  RunSummary s = summarize("r", ev);
  CHECK(s.state == RunState::Failed);
  CHECK_EQ(s.steps, 2);
  CHECK_EQ(s.checks_failed, 1);
  CHECK_EQ(s.last_ms - s.started_ms, int64_t(50));
}
