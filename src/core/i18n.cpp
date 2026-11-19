#include "core/i18n.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>

namespace apron::i18n {

namespace {

// Portuguese (Brazil). Keys are the English source strings, word for word.
// A missing entry falls back to English, so a new string never breaks the UI.
const std::unordered_map<std::string_view, std::string_view>& pt_br() {
  static const std::unordered_map<std::string_view, std::string_view> m{
      // Agent states and event labels.
      {"working", "trabalhando"},
      {"waiting for you", "espera por você"},
      {"no power", "sem energia"},
      {"done", "feita"},
      {"failed", "falhou"},
      {"abandoned", "abandonada"},
      {"started", "iniciou"},
      {"step", "passo"},
      {"question", "pergunta"},
      {"note", "nota"},
      {"check", "check"},
      {"finished", "terminou"},

      // Progress view.
      {"PLAN", "PLANO"},
      {"RUNNING", "EXECUTANDO"},
      {"GATES", "PORTÕES"},
      {"DONE", "FEITO"},
      {"? help", "? ajuda"},
      {"←→↑↓ move · enter open · esc back · q quit", "←→↑↓ navegar · enter abrir · esc voltar · q sair"},
      {"↑↓ scroll · esc back", "↑↓ rolar · esc voltar"},
      {"esc back", "esc voltar"},
      {"on another machine", "em outra máquina"},
      {"waiting for a decision", "espera uma decisão"},
      {"ready to pick up", "pronta para pegar"},
      {"{} is not in tasks.md", "{} não está no tasks.md"},
      {"the progress view needs a terminal; outside one, use apron runs or apron tasks",
       "o Andamento precisa de um terminal; fora dele, use apron runs ou apron tasks"},
      {"could not put the terminal in raw mode", "não consegui colocar o terminal em modo cru"},

      // Tasks.
      {"missing the scope field", "falta o campo escopo"},
      {"missing the done-when field", "falta o campo pronto"},
      {"already in progress ({})", "já está em andamento ({})"},
      {"already done", "já está feita"},
      {"was abandoned", "foi abandonada"},
      {"waiting for a decision from a person", "espera uma decisão da pessoa"},
      {"depends on {}, which does not exist", "depende de {}, que não existe"},
      {"depends on {}, which is not done yet", "depende de {}, que ainda não está feita"},
      {"cannot read {}", "não consegui ler {}"},
      {"cannot write {}", "não consegui escrever {}"},
      {"cannot replace {}: {}", "não consegui substituir {}: {}"},

      // CLI.
      {"no .apron/ found from here to the root; create one with mkdir .apron and a tasks.md",
       "nenhum .apron/ encontrado daqui até a raiz; crie um com mkdir .apron e um tasks.md"},
      {"unknown command: {} (see apron --help)", "comando desconhecido: {} (veja apron --help)"},
      {"usage: apron run start <task> [--agent name] [--intent \"sentence\"]",
       "uso: apron run start <tarefa> [--agent nome] [--intent \"frase\"]"},
      {"usage: apron run start|finish|current", "uso: apron run start|finish|current"},
      {"usage: apron report step|ask|blocked|note \"sentence\"",
       "uso: apron report step|ask|blocked|note \"frase\""},
      {"a run is already in progress in this worktree ({}); close it with apron run finish first",
       "já existe uma execução em curso neste worktree ({}); feche com apron run finish antes"},
      {"task {} cannot be picked up:", "a tarefa {} não pode ser pega:"},
      {"(use --force to start anyway)", "(use --force para começar mesmo assim)"},
      {"warning: {} is not in tasks.md; the run stands alone",
       "aviso: {} não está no tasks.md; a execução fica avulsa"},
      {"no run in progress; use --run <id>", "nenhuma execução em curso; use --run <id>"},
      {"run not found: {}", "execução não encontrada: {}"},
      {"--result must be done, failed or abandoned", "--result precisa ser done, failed ou abandoned"},
      {"invalid report kind: {} (use step, ask, blocked or note)",
       "tipo de relato inválido: {} (use step, ask, blocked ou note)"},
      {"a report needs a sentence", "o relato precisa de uma frase"},
      {"no run in progress; start one with apron run start <task>",
       "nenhuma execução em curso; comece com apron run start <tarefa>"},
      {"--cause must be gpu, budget, lock, machine or other",
       "--cause precisa ser gpu, budget, lock, machine ou other"},
      {"no runs in this project yet", "nenhuma execução neste projeto ainda"},
      {"no runs yet", "nenhuma execução ainda"},
      {"warning: skipped line in {}", "aviso: linha ignorada em {}"},
      {"warning: {}", "aviso: {}"},

      {R"(apron — operate AI agents and follow projects

  apron                         open the progress view in the terminal

Runs (docs/agent-protocol.md):
  apron run start <task> [--agent name] [--intent "sentence"] [--force]
  apron run finish [--result done|failed|abandoned] "summary"
  apron run current
  apron report step|ask|note "sentence"
  apron report blocked --cause gpu|budget|lock|machine|other "sentence"
  apron log [run]               timeline of a run (default: the current or the latest)
  apron runs                    every run in the project
  apron tasks [--ready]         the plan in tasks.md, with each task's state

Environment: APRON_AGENT (agent name), APRON_MACHINE (machine name),
APRON_LANG (en or pt-BR; default: system locale, then English), NO_COLOR.
)",
       R"(apron — operar agentes e acompanhar projetos

  apron                         abre o Andamento no terminal

Execuções (docs/agent-protocol.md):
  apron run start <tarefa> [--agent nome] [--intent "frase"] [--force]
  apron run finish [--result done|failed|abandoned] "resumo"
  apron run current
  apron report step|ask|note "frase"
  apron report blocked --cause gpu|budget|lock|machine|other "frase"
  apron log [execução]          linha do tempo de uma execução (padrão: a atual ou a última)
  apron runs                    todas as execuções do projeto
  apron tasks [--ready]         o plano do tasks.md, com o estado de cada tarefa

Variáveis: APRON_AGENT (nome do agente), APRON_MACHINE (nome da máquina),
APRON_LANG (en ou pt-BR; padrão: idioma do sistema, senão inglês), NO_COLOR.
)"},
  };
  return m;
}

bool is_portuguese(const char* value) {
  if (!value || !*value) return false;
  std::string v(value);
  return v.rfind("pt", 0) == 0;  // pt, pt-BR, pt_BR.UTF-8
}

Lang g_lang = detect();

}  // namespace

Lang detect() {
  if (const char* explicit_lang = std::getenv("APRON_LANG"); explicit_lang && *explicit_lang)
    return is_portuguese(explicit_lang) ? Lang::PtBr : Lang::En;
  for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* value = std::getenv(var);
    if (value && *value) return is_portuguese(value) ? Lang::PtBr : Lang::En;
  }
  return Lang::En;
}

bool has_portuguese(std::string_view english) { return pt_br().count(english) > 0; }

Lang current() { return g_lang; }
void set(Lang lang) { g_lang = lang; }

std::string tr(std::string_view english) {
  if (g_lang == Lang::PtBr) {
    const auto& m = pt_br();
    if (auto it = m.find(english); it != m.end()) return std::string(it->second);
  }
  return std::string(english);
}

std::string trf(std::string_view english, std::initializer_list<std::string> args) {
  std::string pattern = tr(english);
  std::string out;
  auto arg = args.begin();
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}' && arg != args.end()) {
      out += *arg++;
      ++i;
    } else {
      out += pattern[i];
    }
  }
  return out;
}

}  // namespace apron::i18n
