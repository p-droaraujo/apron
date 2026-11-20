#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "core/events.hpp"
#include "core/i18n.hpp"
#include "core/time.hpp"
#include "test.hpp"
#include "tui/progress.hpp"
#include "tui/screen.hpp"

using namespace apron;
using namespace apron::tui;
namespace fs = std::filesystem;

namespace {

std::string row(const Screen& s, int y) {
  std::string out;
  for (int x = 0; x < s.width(); ++x) out += s.at(x, y).ch;
  return out;
}

bool screen_has(const Screen& s, const std::string& text) {
  for (int y = 0; y < s.height(); ++y)
    if (row(s, y).find(text) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST("screen: an identical second frame sends nothing") {
  Screen s;
  s.resize(20, 3);
  s.put(0, 0, "olá ◐", Style{});
  CHECK(!s.flush().empty());
  s.put(0, 0, "olá ◐", Style{});
  CHECK(s.flush().empty());
  s.put(4, 0, "●", Style{});
  std::string diff = s.flush();
  CHECK(diff.find("●") != std::string::npos);
  CHECK(diff.find("olá") == std::string::npos);  // only the changed cell
  CHECK(diff.find("\x1b[?2026h") == 0);           // inside synchronized output
}

TEST("screen: width and clipping count code points, not bytes") {
  CHECK_EQ(text_width("decisão ◐"), 9);
  CHECK_EQ(fit("execução longa", 6), std::string("execu…"));
  CHECK_EQ(fit("curto", 10), std::string("curto"));
}

TEST("progress: tasks and runs land in the right lanes") {
  fs::path root = fs::temp_directory_path() / ("apron-ui-" + std::to_string(::getpid()));
  fs::create_directories(root / ".apron");
  {
    std::ofstream(root / ".apron" / "tasks.md") << R"(## v0a
- [x] T1 Eventos
  scope: src/**
  done-when: sim
- [ ] T2 Tarefas
  scope: src/**
  depends: T1
  done-when: sim
- [ ] T3 Depende
  scope: src/**
  depends: T2
  done-when: sim
- [ ] T4 Pergunta
  scope: src/**
  done-when: sim
)";
  }
  Project p{root};
  Event e;
  e.ts_ms = timeutil::now_ms();
  e.run = "r-T4-1";
  e.task = "T4";
  e.agent = "claude";
  e.machine = "nexus";
  e.kind = Kind::RunStarted;
  append_event(p.run_file(e.run), e);
  e.kind = Kind::Ask;
  e.msg = "o schema pode mudar?";
  append_event(p.run_file(e.run), e);

  Model m = load_model(p);
  CHECK_EQ(m.lanes[0].size(), size_t(2));  // T2 ready, T3 waiting on T2
  CHECK(m.lanes[0][0].ready);
  CHECK_EQ(m.lanes[0][0].task, std::string("T2"));
  CHECK_EQ(m.lanes[2].size(), size_t(1));  // T4 waits for a decision
  CHECK_EQ(m.lanes[3].size(), size_t(1));  // T1 done
  CHECK_EQ(m.waiting, 1);

  Screen s;
  s.resize(120, 20);
  View v;
  i18n::set(i18n::Lang::En);
  render_progress(s, m, v, timeutil::now_ms());
  CHECK(screen_has(s, "GATES 1"));
  i18n::set(i18n::Lang::PtBr);
  render_progress(s, m, v, timeutil::now_ms());
  CHECK(screen_has(s, "PORTÕES 1"));
  i18n::set(i18n::Lang::En);
  CHECK(screen_has(s, "o schema pode mudar?"));
  CHECK(screen_has(s, "◐ 1"));
  fs::remove_all(root);
}

TEST("activity: events of several projects, newest first") {
  fs::path a = fs::temp_directory_path() / ("apron-act-a-" + std::to_string(::getpid()));
  fs::path b = fs::temp_directory_path() / ("apron-act-b-" + std::to_string(::getpid()));
  for (const auto& d : {a, b}) fs::create_directories(d / ".apron");
  auto put = [](const fs::path& root, const std::string& run, int64_t ts, const std::string& msg) {
    Event e;
    e.ts_ms = ts;
    e.run = run;
    e.task = "T1";
    e.kind = Kind::Step;
    e.msg = msg;
    append_event(Project{root}.run_file(run), e);
  };
  put(a, "r-a", 1000, "first");
  put(b, "r-b", 3000, "third");
  put(a, "r-a", 2000, "second");
  auto items = load_activity({a.string(), b.string()}, 10);
  CHECK_EQ(items.size(), size_t(3));
  CHECK_EQ(items[0].event.msg, std::string("third"));
  CHECK_EQ(items[0].project, b.filename().string());
  CHECK_EQ(items[2].event.msg, std::string("first"));
  CHECK_EQ(load_activity({a.string(), b.string()}, 2).size(), size_t(2));
  fs::remove_all(a);
  fs::remove_all(b);
}
