# Tarefas do Apron

O plano do próprio Apron, no formato descrito em `docs/agent-protocol.md`.

## v0a · Observar

- [x] T1 Eventos locais: `apron run start`, `apron report`, `apron run finish` e `apron log`
  escopo: CMakeLists.txt, src/**, tests/**
  pronto: uma execução iniciada, narrada e terminada pela CLI aparece inteira em `apron log`
  checks: build, test

- [x] T2 O `tasks.md` é lido e atualizado pela CLI (`apron tasks`, estados `[~]`, `[?]`, `[x]`)
  escopo: src/**, tests/**
  depende: T1
  pronto: `apron run start T2` recusa tarefa sem escopo ou pronto e marca `[~]` com `por:`
  checks: build, test

- [x] T11 TUI do Andamento local e somente leitura: `apron` sem argumentos abre a tela
  escopo: CMakeLists.txt, src/tui/**, src/cli/**, tests/**, README.md
  depende: T2
  pronto: `apron` mostra as faixas plano, executando, portões e feito a partir do tasks.md e dos eventos, atualiza sozinho e abre o log de uma execução
  checks: build, test

- [ ] T3 `apron check` roda o `checks.toml` e grava um evento por check
  escopo: src/**, tests/**
  depende: T1
  pronto: `apron check` mostra cada check com ok e duração e os grava na execução atual
  checks: build, test

- [ ] T4 Medidor de escopo: arquivos alterados no git comparados aos globs da tarefa
  escopo: src/**, tests/**
  depende: T2
  pronto: `apron run finish` registra os arquivos tocados e os que saíram do escopo
  checks: build, test

- [ ] T5 Hooks do Claude Code chamam `apron report` para o estado do agente
  escopo: integrations/claude-code/**, docs/**
  depende: T1
  pronto: um `.claude/settings.json` de exemplo faz o Claude Code gerar step, ask e run.finished sozinho
  checks: build, test

- [ ] T6 `aprond` mínimo: socket unix, quadros do protocolo, PTYs e `sessions.list`
  escopo: src/daemon/**, src/proto/**, tests/**
  depende: T1
  pronto: `aprond` sobe, `apron new` cria uma sessão e `apron ls` a lista com o agente em primeiro plano
  checks: build, test

- [ ] T7 Eventos vão para o daemon quando ele existe, com o arquivo local como reserva
  escopo: src/**, tests/**
  depende: T6
  pronto: com o `aprond` rodando, `apron report` entrega pelo socket; sem ele, grava em `.apron/runs/`
  checks: build, test

- [ ] T8 Hub: pareamento por token, CA própria e conexão de saída com mTLS
  escopo: src/hub/**, src/proto/**, tests/**
  depende: T6
  pronto: duas máquinas pareadas aparecem em `apron machines` e o hub roteia `sessions.list` para cada uma
  checks: build, test
  decisão: formato do token e do certificado afetam a segurança de todas as máquinas

- [ ] T9 Renderizador da TUI: grade de células, diff e saída sincronizada
  escopo: src/tui/**, tests/**
  depende: T6
  pronto: um quadro de teste desenha e redesenha só as células alteradas
  checks: build, test

- [ ] T10 Andamento na TUI a partir dos eventos
  escopo: src/tui/**, tests/**
  depende: T7, T9, T11
  pronto: o Andamento mostra as execuções de todos os projetos com os estados de agente
  checks: build, test
