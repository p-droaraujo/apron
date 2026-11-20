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
      {"review", "revisão"},

      // Progress view.
      {"on another machine", "em outra máquina"},
      {"waiting for a decision", "espera uma decisão"},
      {"ready to pick up", "pronta para pegar"},
      {"{} is not in tasks.md", "{} não está no tasks.md"},
      {"could not put the terminal in raw mode", "não consegui colocar o terminal em modo cru"},

      // Tasks.
      {"missing the scope field", "falta o campo scope"},
      {"missing the done-when field", "falta o campo done-when"},
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
      {"no runs in this project yet", "nenhuma execução neste projeto ainda"},
      {"no runs yet", "nenhuma execução ainda"},
      {"warning: skipped line in {}", "aviso: linha ignorada em {}"},
      {"warning: {}", "aviso: {}"},
      {"nothing to review", "nada para revisar"},
      {"no activity yet", "nenhuma atividade ainda"},
      {"outside", "fora"},
      {"usage: apron review [<run> merge|return \"comment\"|discard]", "uso: apron review [<execução> merge|return \"comentário\"|discard]"},
      {"the run has no commit with its Apron-Run trailer yet", "a execução ainda não tem um commit com o trailer Apron-Run"},
      {"main has moved on since this run; rebase its branch onto main first", "o main andou desde esta execução; faça rebase do branch sobre o main antes"},
      {"main has uncommitted changes; commit or stash them first", "o main tem mudanças não commitadas; faça commit ou stash antes"},
      {"git could not fast-forward main", "o git não conseguiu avançar o main"},
      {"returning a run needs a comment for the agent", "devolver uma execução precisa de um comentário para o agente"},
      {"Progress", "Andamento"},
      {"Review", "Revisão"},
      {"Activity", "Atividade"},
      {"merged", "mesclada"},
      {"returned to the agent", "devolvida ao agente"},
      {"discarded", "descartada"},
      {"changes", "mudanças"},
      {"trajectory", "trajetória"},
      {"no commit yet: uncommitted changes since the run started", "ainda sem commit: mudanças não commitadas desde o início da execução"},
      {"aprond is not running; projects show up here once a run reports while it runs",
       "o aprond não está rodando; os projetos aparecem aqui quando uma execução reporta com ele no ar"},
      {"no projects yet", "nenhum projeto ainda"},
      {"could not start aprond", "não consegui iniciar o aprond"},
      {"aprond did not start; see {}", "o aprond não subiu; veja {}"},
      {"aprond is not running", "o aprond não está rodando"},
      {"lost the connection to aprond", "a conexão com o aprond caiu"},
      {"aprond did not answer", "o aprond não respondeu"},
      {"no session {}", "a sessão {} não existe"},
      {"[attached; Ctrl-] detaches]", "[anexado; Ctrl-] desanexa]"},
      {"[detached from session {}]", "[desanexado da sessão {}]"},
      {"no sessions", "nenhuma sessão"},
      {"exited", "encerrada"},
      {"usage: apron attach [<machine>:]<session>", "uso: apron attach [<máquina>:]<sessão>"},
      {"usage: apron kill [<machine>:]<session>", "uso: apron kill [<máquina>:]<sessão>"},
      {"no machines yet; add one with: apron machines add <ssh host>", "nenhuma máquina ainda; adicione com: apron machines add <host ssh>"},
      {"{} running · {} projects", "{} rodando · {} projetos"},
      {"usage: apron machines add <ssh host> [apron command there]", "uso: apron machines add <host ssh> [comando do apron lá]"},
      {"usage: apron machines rm <ssh host>", "uso: apron machines rm <host ssh>"},
      {"no machine {}", "nenhuma máquina {}"},
      {"scope meets {} ({}, {}@{}): {}", "o escopo encontra {} ({}, {}@{}): {}"},
      {"not a host: {}", "não é um host: {}"},
      {"not a run id: {}", "não é um id de execução: {}"},
      {"Add a machine: its ssh host", "Adicionar máquina: o host ssh"},
      {"Answer", "Responder"},
      {"Comment for the agent", "Comentário para o agente"},
      {"Details", "Detalhes"},
      {"Done", "Feito"},
      {"End session {} ({})?", "Encerrar a sessão {} ({})?"},
      {"End session {} ({}) on {}?", "Encerrar a sessão {} ({}) em {}?"},
      {"End the session", "Encerrar a sessão"},
      {"Gates", "Portões"},
      {"Keys", "Teclas"},
      {"Log", "Log"},
      {"Machines", "Máquinas"},
      {"New session in {}", "Nova sessão em {}"},
      {"No project in this folder. Pick one in the sidebar.", "Nenhum projeto nesta pasta. Escolha um na barra lateral."},
      {"Plan", "Plano"},
      {"Projects", "Projetos"},
      {"Running", "Executando"},
      {"Terminal", "Terminal"},
      {"back", "voltar"},
      {"board", "quadro"},
      {"cancel", "cancelar"},
      {"close", "fechar"},
      {"confirm", "confirmar"},
      {"could not start the session", "não foi possível iniciar a sessão"},
      {"discard", "descartar"},
      {"discarded; the task is open again", "descartada; a tarefa está aberta de novo"},
      {"end session", "encerrar sessão"},
      {"ended", "encerrada"},
      {"for now the board opens projects of this machine", "por enquanto o quadro abre projetos desta máquina"},
      {"keys", "teclas"},
      {"log", "log"},
      {"merge", "mesclar"},
      {"merged into main", "mesclada na main"},
      {"new session", "nova sessão"},
      {"next view", "próxima visão"},
      {"no project here", "nenhum projeto aqui"},
      {"none yet", "nenhum ainda"},
      {"open", "abrir"},
      {"quit", "sair"},
      {"return with a comment", "devolver com comentário"},
      {"scroll", "rolar"},
      {"seen {} ago", "visto há {}"},
      {"the screen needs a terminal; outside one, use apron log or apron tasks", "a tela precisa de um terminal; fora de um, use apron log ou apron tasks"},
      {"the session has ended", "a sessão terminou"},
      {"this folder has no .apron/ yet", "esta pasta ainda não tem .apron/"},
      {"this machine", "esta máquina"},
      {"unreachable", "sem contato"},
      {"{} added; connecting", "{} adicionada; conectando"},
      {"{} more", "mais {}"},
      {"{} running", "{} rodando"},
      {"previous view", "visão anterior"},
      {"move", "mover"},
      {"the agent's terminal", "o terminal do agente"},
      {"the run's log", "o log da execução"},
      {"add a machine", "adicionar máquina"},
      {"end a session", "encerrar uma sessão"},
      {"leave a terminal", "sair de um terminal"},
      {"could not write {}", "não foi possível gravar {}"},
      {"usage: apron machines [add <ssh host> [apron command there] | rm <ssh host>]", "uso: apron machines [add <host ssh> [comando do apron lá] | rm <host ssh>]"},
      {"aprond is running on {}", "o aprond está rodando em {}"},
      {"warning: {} of {} files left the scope:", "aviso: {} de {} arquivos saíram do escopo:"},
      {"no run in progress; use apron scope <run>", "nenhuma execução em curso; use apron scope <execução>"},
      {"{} files, {} outside the scope", "{} arquivos, {} fora do escopo"},
      {"scope: {} files, {} outside", "escopo: {} arquivos, {} fora"},
      {"checks.toml has no [[check]] with name and run", "o checks.toml não tem nenhum [[check]] com name e run"},
      {"warning: no check named {} in checks.toml", "aviso: nenhum check chamado {} no checks.toml"},
      {"timed out after {}s", "estourou o tempo de {}s"},
      {"no run in progress: results were not recorded", "nenhuma execução em curso: os resultados não foram gravados"},
      {"checks.toml line {}: expected key = value", "checks.toml linha {}: esperava chave = valor"},

      {R"(apron — operate AI agents and follow projects

  apron                         open the progress view in the terminal

Sessions (kept by aprond, started on demand):
  apron new [-d] [command...]   new session in this directory, attached unless -d
  apron ls                      sessions and the program in the foreground of each
  apron attach <session>        attach this terminal; Ctrl-] detaches
  apron kill <session>          end a session
  apron daemon [stop]           is aprond running? stop it
  apron projects                every project on this machine, with its runs

Runs (docs/agent-protocol.md):
  apron run start <task> [--agent name] [--intent "sentence"] [--force]
  apron run finish [--result done|failed|abandoned] "summary"
  apron run current
  apron report step|ask|note "sentence"
  apron report blocked "sentence"
  apron log [run]               timeline of a run (default: the current or the latest)
  apron check [names...]        run checks.toml and record the results in the current run
  apron scope [run]             files the run touched, and which left the task's scope
  apron review                  finished runs waiting for a person
  apron review <run> merge|return "comment"|discard
  apron runs                    every run in the project
  apron tasks [--ready]         the plan in tasks.md, with each task's state

Environment: APRON_AGENT (agent name), APRON_MACHINE (machine name),
APRON_LANG (en or pt-BR; default: system locale, then English), NO_COLOR.
)",
       R"(apron — operar agentes e acompanhar projetos

  apron                         abre o Andamento no terminal

Sessões (mantidas pelo aprond, que sobe sozinho):
  apron new [-d] [comando...]   nova sessão neste diretório, anexada a menos que -d
  apron ls                      as sessões e o programa em primeiro plano de cada uma
  apron attach <sessão>         anexa este terminal; Ctrl-] desanexa
  apron kill <sessão>           encerra uma sessão
  apron daemon [stop]           o aprond está rodando? pare-o
  apron projects                todos os projetos desta máquina, com as execuções

Execuções (docs/agent-protocol.md):
  apron run start <tarefa> [--agent nome] [--intent "frase"] [--force]
  apron run finish [--result done|failed|abandoned] "resumo"
  apron run current
  apron report step|ask|note "frase"
  apron report blocked "frase"
  apron log [execução]          linha do tempo de uma execução (padrão: a atual ou a última)
  apron check [nomes...]        roda o checks.toml e grava os resultados na execução atual
  apron scope [execução]        arquivos que a execução tocou, e os que saíram do escopo
  apron review                  execuções terminadas esperando uma pessoa
  apron review <execução> merge|return "comentário"|discard
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
