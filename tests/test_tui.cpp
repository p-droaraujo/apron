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
  escopo: src/**
  pronto: sim
- [ ] T2 Tarefas
  escopo: src/**
  depende: T1
  pronto: sim
- [ ] T3 Depende
  escopo: src/**
  depende: T2
  pronto: sim
- [ ] T4 Pergunta
  escopo: src/**
  pronto: sim
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
