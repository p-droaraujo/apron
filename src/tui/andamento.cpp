#include "tui/andamento.hpp"

#include <algorithm>
#include <map>

#include "core/tasks.hpp"
#include "core/time.hpp"

namespace apron::tui {

namespace {

// A paleta da linguagem v2: cor só para estado.
const Style kText{Color::hex(0xe6e8eb), {}, false};
const Style kBold{Color::hex(0xe6e8eb), {}, true};
const Style kSoft{Color::hex(0xa3aab5), {}, false};
const Style kDim{Color::hex(0x7d8592), {}, false};
const Style kLine{Color::hex(0x3a404b), {}, false};
const Style kBlue{Color::hex(0x7aa2f7), {}, false};
const Style kBlue2{Color::hex(0x4a6bb0), {}, false};
const Style kAmber{Color::hex(0xe0af68), {}, false};
const Style kRed{Color::hex(0xf7768e), {}, false};
const Style kBadge{Color::hex(0x0f1115), Color::hex(0xe6e8eb), true};
constexpr uint32_t kSelected = 0x1f2430;

Style with_bg(Style s, bool selected) {
  if (selected) s.bg = Color::hex(kSelected);
  return s;
}

Style state_style(RunState s) {
  switch (s) {
    case RunState::Working: return kBlue;
    case RunState::Waiting: return kAmber;
    case RunState::Failed: return kRed;
    default: return kDim;
  }
}

const char* state_glyph(RunState s) {
  switch (s) {
    case RunState::Working: return "●";
    case RunState::Waiting: return "◐";
    case RunState::Blocked: return "⊘";
    case RunState::Done: return "✓";
    case RunState::Failed: return "✕";
    case RunState::Abandoned: return "–";
  }
  return "●";
}

std::string ago(int64_t now, int64_t then) { return timeutil::short_duration(now - then); }

// Títulos vêm do markdown; na tela, crases não ajudam ninguém.
std::string plain(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
  return s;
}

}  // namespace

Model load_model(const Project& project) {
  Model m;
  m.project = project.root.filename().string();
  m.machine = machine_name();

  auto file = TaskFile::load(project.tasks_file());
  std::map<std::string, const Task*> by_id;
  if (file)
    for (const auto& t : file->tasks()) by_id[t.id] = &t;

  auto runs = list_runs(project);
  std::map<std::string, bool> task_has_run;
  for (const auto& r : runs) {
    Item it;
    it.from_run = true;
    it.task = r.task;
    it.run = r.id;
    it.state = r.state;
    it.who = r.agent + "@" + r.machine;
    auto found = by_id.find(r.task);
    it.title = plain(found != by_id.end() ? found->second->title : r.intent);
    it.detail = r.last_msg;
    switch (r.state) {
      case RunState::Working:
      case RunState::Blocked:
        it.lane = Lane::Executando;
        it.since_ms = r.started_ms;
        break;
      case RunState::Waiting:
      case RunState::Failed:
        it.lane = Lane::Portoes;
        it.since_ms = r.last_ms;
        break;
      default:
        it.lane = Lane::Feito;
        it.since_ms = r.last_ms;
    }
    if (r.state == RunState::Working) ++m.working;
    if (r.state == RunState::Waiting) ++m.waiting;
    if (r.state == RunState::Blocked) ++m.blocked;
    // Uma tarefa aparece uma vez só: pela execução mais recente.
    if (task_has_run[r.task] && it.lane == Lane::Feito) continue;
    task_has_run[r.task] = true;
    m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
  }

  if (file) {
    for (const auto& t : file->tasks()) {
      if (task_has_run[t.id]) continue;
      Item it;
      it.task = t.id;
      it.title = plain(t.title);
      it.who = t.field("por");
      if (t.state == ' ') {
        it.lane = Lane::Plano;
        auto why = blockers_to_start(*file, t);
        it.ready = why.empty();
        if (!it.ready) {
          std::string deps;
          for (const auto& d : t.depends()) deps += (deps.empty() ? "" : ", ") + d;
          it.detail = deps.empty() ? why.front() : "← " + deps;
        }
      } else if (t.state == '~') {
        it.lane = Lane::Executando;  // em andamento noutra máquina
        it.detail = "em outra máquina";
      } else if (t.state == '?') {
        it.lane = Lane::Portoes;
        it.state = RunState::Waiting;
        it.detail = "espera uma decisão";
      } else if (t.state == 'x') {
        it.lane = Lane::Feito;
        it.state = RunState::Done;
      } else {
        continue;
      }
      m.lanes[static_cast<int>(it.lane)].push_back(std::move(it));
    }
  }
  // Plano: as prontas primeiro, na ordem do arquivo.
  std::stable_sort(m.lanes[0].begin(), m.lanes[0].end(), [](const Item& a, const Item& b) { return a.ready > b.ready; });
  return m;
}

void clamp_selection(const Model& model, View& view) {
  view.lane = std::clamp(view.lane, 0, 3);
  int n = static_cast<int>(model.lanes[view.lane].size());
  view.index = n == 0 ? 0 : std::clamp(view.index, 0, n - 1);
}

void render_andamento(Screen& sc, const Model& m, const View& v, int64_t now) {
  sc.clear();
  int W = sc.width(), H = sc.height();

  // Cabeçalho: o projeto e a máquina.
  int x = sc.put(1, 0, m.project, kBold);
  sc.put(1 + x + 1, 0, "· " + m.machine, kDim);
  std::string clock = timeutil::iso8601(now).substr(11, 5) + " UTC";
  sc.put(W - text_width(clock) - 1, 0, clock, kDim);

  // Quatro faixas.
  const char* names[4] = {"PLANO", "EXECUTANDO", "PORTÕES", "FEITO"};
  int gap = 3, left = 1;
  int cw = std::max(12, (W - 2 - gap * 3) / 4);
  int top = 2, bottom = H - 2;
  for (int lane = 0; lane < 4; ++lane) {
    int lx = left + lane * (cw + gap);
    const auto& items = m.lanes[lane];
    int hx = sc.put(lx, top, names[lane], kDim);
    sc.put(lx + hx + 1, top, std::to_string(items.size()), kText);
    sc.fill(lx, top + 1, cw, "─", kLine);

    int y = top + 2;
    for (size_t i = 0; i < items.size() && y < bottom; ++i) {
      const Item& it = items[i];
      bool sel = v.lane == lane && v.index == static_cast<int>(i);
      if (lane == 0 || lane == 3) {
        // Uma linha por tarefa.
        const char* g = lane == 3 ? state_glyph(it.state) : it.ready ? "○" : "◌";
        Style gs = lane == 3 ? state_style(it.state) : it.ready ? kSoft : kLine;
        if (lane == 3 && it.state == RunState::Done) gs = kSoft;
        if (sel) sc.fill(lx, y, cw, " ", with_bg(kText, true));
        sc.put(lx, y, g, with_bg(gs, sel));
        int ix = sc.put(lx + 2, y, it.task, with_bg(it.ready || lane == 3 ? kText : kDim, sel));
        std::string tail = lane == 3 && it.since_ms ? ago(now, it.since_ms) : it.detail;
        int tw = text_width(tail);
        int room = cw - 2 - ix - 1 - (tw ? tw + 1 : 0);
        sc.put(lx + 2 + ix + 1, y, fit(it.title, room), with_bg(it.ready || lane == 3 ? kSoft : kDim, sel));
        if (tw) sc.put(lx + cw - tw, y, tail, with_bg(kDim, sel));
        y += 1;
        continue;
      }
      // Cartão de três linhas com a barra lateral na cor do estado.
      Style bar = state_style(it.state);
      bool breathing = it.state == RunState::Working && it.from_run;
      Style gstyle = breathing && (v.tick / 6) % 2 ? kBlue2 : bar;
      for (int r = 0; r < 3 && y + r < bottom; ++r) {
        if (sel) sc.fill(lx, y + r, cw, " ", with_bg(kText, true));
        sc.put(lx, y + r, "▌", with_bg(bar, sel));
      }
      std::string dur = it.since_ms ? ago(now, it.since_ms) : "";
      sc.put(lx + 2, y, state_glyph(it.state), with_bg(gstyle, sel));
      sc.put(lx + 4, y, fit(it.who.empty() ? "—" : it.who, cw - 6 - text_width(dur)), with_bg(kSoft, sel));
      sc.put(lx + cw - text_width(dur), y, dur, with_bg(kDim, sel));
      int tx = sc.put(lx + 2, y + 1, it.task, with_bg(kBold, sel));
      sc.put(lx + 3 + tx, y + 1, fit(it.title, cw - 3 - tx), with_bg(kText, sel));
      Style ds = it.state == RunState::Waiting ? kAmber : kDim;
      sc.put(lx + 2, y + 2, fit(it.detail, cw - 2), with_bg(ds, sel));
      y += 4;
    }
    if (items.empty()) sc.put(lx, top + 2, "—", kLine);
  }

  // Barra de status: só estado real.
  int sy = H - 1;
  int bx = sc.put(0, sy, " APRON ", kBadge) + 1;
  if (m.waiting) bx += sc.put(bx, sy, "◐ " + std::to_string(m.waiting), kAmber) + 2;
  if (m.working) bx += sc.put(bx, sy, "● " + std::to_string(m.working), kBlue) + 2;
  if (m.blocked) bx += sc.put(bx, sy, "⊘ " + std::to_string(m.blocked), kDim) + 2;
  std::string hint = v.help ? "←→↑↓ navegar · enter abrir · esc voltar · q sair" : "? ajuda";
  sc.put(W - text_width(hint) - 1, sy, hint, kDim);
}

void render_log(Screen& sc, const Project& project, const std::string& run, int scroll, int64_t now) {
  sc.clear();
  auto events = load_events(project.run_file(run));
  RunSummary s = summarize(run, events);
  int W = sc.width(), H = sc.height();
  int x = sc.put(1, 0, s.task, kBold);
  x += 1 + sc.put(2 + x, 0, "· " + s.agent + "@" + s.machine, kSoft);
  x += 1 + sc.put(2 + x, 0, "· " + timeutil::short_duration(s.last_ms - s.started_ms), kDim);
  sc.put(3 + x, 0, std::string(state_glyph(s.state)) + " " + std::string(to_string(s.state)), state_style(s.state));
  sc.put(1, 1, run, kLine);
  (void)now;

  int y = 3;
  for (size_t i = static_cast<size_t>(std::max(0, scroll)); i < events.size() && y < H - 1; ++i, ++y) {
    const Event& e = events[i];
    sc.put(1, y, timeutil::short_duration(e.ts_ms - s.started_ms), kDim);
    std::string g = "·", label = "passo", msg = e.msg;
    Style gs = kBlue, ls = kSoft, ms = kText;
    switch (e.kind) {
      case Kind::RunStarted: g = "▶"; label = "iniciou"; msg = e.extra_str("intent"); break;
      case Kind::Step: break;
      case Kind::Ask: g = "◐"; label = "pergunta"; gs = ls = ms = kAmber; break;
      case Kind::Blocked: g = "⊘"; label = "sem energia"; gs = kDim; msg = "(" + e.extra_str("cause") + ") " + msg; break;
      case Kind::Note: g = "›"; label = "nota"; gs = kDim; ms = kSoft; break;
      case Kind::Check: {
        const json::Value* ok = e.extra_find("ok");
        bool passed = ok && ok->is_bool() && ok->boolean();
        g = passed ? "✓" : "✕";
        gs = passed ? kSoft : kRed;
        label = "check";
        msg = e.extra_str("name");
        break;
      }
      case Kind::RunFinished: g = "■"; label = "terminou"; gs = kText; msg = e.extra_str("result") + (e.msg.empty() ? "" : " · " + e.msg); break;
    }
    sc.put(9, y, g, gs);
    sc.put(11, y, label, ls);
    sc.put(24, y, fit(msg, W - 25), ms);
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  sc.put(W - 22, H - 1, "↑↓ rolar · esc voltar", kDim);
}

void render_task(Screen& sc, const Project& project, const std::string& id) {
  sc.clear();
  auto file = TaskFile::load(project.tasks_file());
  const Task* t = file ? file->find(id) : nullptr;
  int W = sc.width(), H = sc.height();
  if (!t) {
    sc.put(1, 0, id + " não está no tasks.md", kDim);
  } else {
    int x = sc.put(1, 0, t->id, kBold);
    sc.put(2 + x, 0, fit(plain(t->title), W - 3 - x), kText);
    sc.put(1, 1, t->section, kLine);
    int y = 3;
    for (const auto& [k, val] : t->fields) {
      if (y >= H - 1) break;
      sc.put(1, y, k, kDim);
      sc.put(12, y, fit(val, W - 13), kText);
      ++y;
    }
    auto why = blockers_to_start(*file, *t);
    if (t->state == ' ') {
      ++y;
      if (why.empty()) sc.put(1, y, "○ pronta para pegar", kSoft);
      for (const auto& w : why)
        if (y < H - 1) sc.put(1, y++, "◌ " + w, kDim);
    }
  }
  sc.put(0, H - 1, " APRON ", kBadge);
  sc.put(W - 12, H - 1, "esc voltar", kDim);
}

}  // namespace apron::tui
