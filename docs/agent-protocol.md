# Protocolo do agente

Como um agente de IA trabalha num projeto do Apron, do começo ao fim, e como ele conta o que está fazendo.

O princípio: **o agente narra o trabalho em eventos curtos e estruturados; o Apron transforma esses eventos em andamento, custo, escopo e revisão.** O agente nunca precisa explicar o que o Apron já consegue medir sozinho, como arquivos alterados, tempo ou tokens.

## 1. O projeto

Um projeto do Apron é um repositório git com uma pasta `.apron/`:

| Arquivo | Versionado | Para quê |
| --- | --- | --- |
| `.apron/project.toml` | sim | Nome, modo (manual ou auto), caminhos de decisão |
| `.apron/tasks.md` | sim | O plano: tarefas, escopo, dependências, critério de pronto |
| `.apron/checks.toml` | sim | Comandos que dizem se o trabalho está bom |
| `.apron/policy.toml` | sim | O que o agente pode fazer sem perguntar |
| `.apron/runs/` | não | Eventos de cada execução, um arquivo por execução |

Tudo que é do projeto vive no repositório e muda por commit. Os eventos das execuções são da máquina e não entram no git.

## 2. O formato do `tasks.md`

Markdown legível por gente e por máquina. Uma tarefa é um item de lista com um id estável, seguido de campos indentados `chave: valor`.

```markdown
## v0a · Observar

- [ ] T2 Daemon mínimo guarda sessões e responde a sessions.list
  escopo: src/daemon/**, tests/daemon/**
  depende: T1
  pronto: `aprond` sobe, cria um PTY com `apron new` e `apron ls` o mostra
  checks: build, test
```

O que vai entre colchetes é o estado:

| Marca | Estado | Quem muda |
| --- | --- | --- |
| `[ ]` | Aberta | Pessoa ou Planner |
| `[~]` | Em andamento, com `por: <agente>@<máquina>` | `apron run start` |
| `[?]` | Espera uma decisão da pessoa | `apron report ask` |
| `[x]` | Feita | `apron run finish --result done` |
| `[-]` | Abandonada | Pessoa |

Campos:

| Campo | Obrigatório | Significado |
| --- | --- | --- |
| `escopo` | sim | Globs dos arquivos que a tarefa pode tocar; o que sair disso vira desvio de escopo |
| `pronto` | sim | O critério verificável de pronto, em uma frase |
| `depende` | não | Ids que precisam estar feitos antes |
| `checks` | não | Nomes de checks; sem o campo, rodam todos |
| `decisão` | não | Por que esta tarefa precisa de uma pessoa (vira ◐ ao terminar) |
| `por` | não | Escrito pelo Apron quando alguém pega a tarefa |

Uma tarefa sem `escopo` ou sem `pronto` não pode ser pega: o Apron recusa o `run start` e diz o que falta. Esse é o contrato mínimo para o trabalho ser revisável.

## 3. O ciclo de uma execução

```
apron run start T2 --intent "..."      →  [~] no tasks.md, worktree, evento run.started
apron report step "..."                 →  trajetória
apron report ask "..."                  →  ◐ espera por você; [?] no tasks.md
apron report blocked "..."              →  ⊘ sem energia, com a causa
apron report note "..."                 →  aprendizado que vale para a próxima execução
apron check                             →  roda os checks, um evento check por comando
apron run finish --result done "..."    →  [x], resumo, entra na Revisão
```

Cada comando grava um evento e devolve em menos de 10 ms. Nenhum deles bloqueia o agente.

## 4. Os eventos

Uma linha JSON por evento em `.apron/runs/<run>.jsonl`:

```json
{"ts":"2026-11-19T20:31:04Z","run":"r-T1-0001","task":"T1","agent":"claude","machine":"nexus","kind":"step","msg":"parser do tasks.md com testes"}
```

| `kind` | Quando | Campos extras | Vira na interface |
| --- | --- | --- | --- |
| `run.started` | Ao pegar a tarefa | `intent` | Faixa em Executando, ● |
| `step` | Ao mudar de fase do trabalho | — | Um ponto na trajetória |
| `ask` | Precisa de uma decisão | — | ◐ e cartão nos portões |
| `blocked` | Algo de fora impede o avanço | `cause` (gpu, budget, lock, machine, other) | ⊘ com a causa |
| `note` | Algo que a próxima execução deve saber | — | Linha no log da sessão |
| `check` | Um check rodou | `name`, `ok`, `ms` | Instrumento de checks |
| `run.finished` | Ao terminar | `result` (done, failed, abandoned) | Faixa sai para Portões ou Revisão |

Regras de escrita para o agente:

- Uma frase por evento, no presente, dizendo o que mudou: "parser do tasks.md passa nos testes", não "estou trabalhando no parser".
- `step` a cada mudança de fase (ler, planejar, editar, testar, corrigir), não a cada comando. Três a dez por execução.
- `ask` só para o que é decisão da pessoa: regra de negócio, voltado ao cliente, arquitetura, escopo. Dúvida técnica o agente resolve.
- O resumo do `run.finished` responde a três perguntas: o que foi feito, o que ficou de fora e o que a pessoa deve olhar primeiro.

## 5. Como cada agente se conecta

| Agente | Como o estado chega | Como os eventos chegam |
| --- | --- | --- |
| Claude Code | Hooks (UserPromptSubmit, Notification, Stop, PreToolUse) chamam `apron report` | O agente chama `apron report` pelo shell, guiado pelo `AGENTS.md`; no v0b também por MCP |
| Codex | `notify` chama `apron report` | Shell, guiado pelo `AGENTS.md` |
| Pi | Extensão própria | Extensão e shell |
| Outros | Processo em primeiro plano | Shell, se o agente souber ler o `AGENTS.md` |

O `AGENTS.md` na raiz do repositório é o ponto de entrada: qualquer agente que leia instruções do projeto aprende o protocolo por ele.

## 6. O que o Apron mede sozinho

O agente não reporta nada disto; o Apron tira do git, da telemetria e do relógio:

- arquivos e linhas alterados, e o que saiu do `escopo`;
- duração da execução e de cada passo;
- tokens e custo (OpenTelemetry) ou tempo de GPU (modelo local);
- resultado dos checks, quando rodam por `apron check`.
