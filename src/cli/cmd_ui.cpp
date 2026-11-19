// apron (sem argumentos): o Andamento no terminal.
#include <unistd.h>

#include <algorithm>

#include "cli/common.hpp"
#include "core/time.hpp"
#include "tui/andamento.hpp"
#include "tui/term.hpp"

namespace apron::cli {

int cmd_ui(const Args&) {
  auto project = require_project();
  if (!project) return 1;
  if (!isatty(STDOUT_FILENO)) return fail("o Andamento precisa de um terminal; fora dele, use apron runs ou apron tasks");

  tui::Terminal term;
  if (!term.ok()) return fail("não consegui colocar o terminal em modo cru");

  tui::Screen screen;
  screen.resize(term.width(), term.height());
  tui::Model model = tui::load_model(*project);
  tui::View view;
  tui::clamp_selection(model, view);

  enum class Mode { Andamento, Log, Task } mode = Mode::Andamento;
  std::string open_run, open_task;
  int scroll = 0;
  int64_t last_load = timeutil::now_ms();

  // ~10 quadros por segundo; os dados recarregam a cada segundo. Um quadro
  // sem mudança não envia nada ao terminal.
  while (true) {
    int64_t now = timeutil::now_ms();
    if (now - last_load >= 1000) {
      model = tui::load_model(*project);
      tui::clamp_selection(model, view);
      last_load = now;
    }
    switch (mode) {
      case Mode::Andamento: tui::render_andamento(screen, model, view, now); break;
      case Mode::Log: tui::render_log(screen, *project, open_run, scroll, now); break;
      case Mode::Task: tui::render_task(screen, *project, open_task); break;
    }
    term.write(screen.flush());

    tui::KeyEvent k = term.poll(100);
    ++view.tick;
    if (k.key == tui::Key::Resize) {
      screen.resize(term.width(), term.height());
      continue;
    }
    if (k.key == tui::Key::Char && k.ch == 'q') {
      if (mode == Mode::Andamento) break;
      mode = Mode::Andamento;
      continue;
    }
    if (mode != Mode::Andamento) {
      if (k.key == tui::Key::Esc) mode = Mode::Andamento;
      else if (k.key == tui::Key::Up || (k.key == tui::Key::Char && k.ch == 'k')) scroll = std::max(0, scroll - 1);
      else if (k.key == tui::Key::Down || (k.key == tui::Key::Char && k.ch == 'j')) ++scroll;
      continue;
    }
    switch (k.key) {
      case tui::Key::Left: --view.lane; view.index = 0; break;
      case tui::Key::Right:
      case tui::Key::Tab: view.lane = (view.lane + 1) % 4; view.index = 0; break;
      case tui::Key::Up: --view.index; break;
      case tui::Key::Down: ++view.index; break;
      case tui::Key::Enter: {
        const auto& items = model.lanes[view.lane];
        if (items.empty()) break;
        const auto& it = items[static_cast<size_t>(view.index)];
        if (!it.run.empty()) {
          open_run = it.run;
          scroll = 0;
          mode = Mode::Log;
        } else {
          open_task = it.task;
          mode = Mode::Task;
        }
        break;
      }
      case tui::Key::Char:
        if (k.ch == 'h') --view.lane, view.index = 0;
        else if (k.ch == 'l') ++view.lane, view.index = 0;
        else if (k.ch == 'k') --view.index;
        else if (k.ch == 'j') ++view.index;
        else if (k.ch == '?') view.help = !view.help;
        break;
      default: break;
    }
    tui::clamp_selection(model, view);
  }
  return 0;
}

}  // namespace apron::cli
