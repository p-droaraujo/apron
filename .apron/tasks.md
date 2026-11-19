# Apron tasks

Apron's own plan, in the format described in `docs/agent-protocol.md`.

## v0a · Observe

- [x] T1 Local events: `apron run start`, `apron report`, `apron run finish` and `apron log`
  scope: CMakeLists.txt, src/**, tests/**
  done-when: a run started, narrated and finished through the CLI shows up whole in `apron log`
  checks: build, test

- [x] T2 The CLI reads and updates `tasks.md` (`apron tasks`, states `[~]`, `[?]`, `[x]`)
  scope: src/**, tests/**
  depends: T1
  done-when: `apron run start T2` refuses a task without scope or done-when and marks `[~]` with `by:`
  checks: build, test

- [x] T11 Local, read-only progress view: `apron` with no arguments opens the screen
  scope: CMakeLists.txt, src/tui/**, src/cli/**, tests/**, README.md
  depends: T2
  done-when: `apron` shows the plan, running, gates and done lanes from tasks.md and the events, refreshes on its own and opens a run's log
  checks: build, test

- [x] T12 English as the default language and Portuguese as the second: interface, CLI, tasks.md format, code and docs
  scope: **
  depends: T11
  done-when: with APRON_LANG=pt-BR everything shows in Portuguese, without it in English; tasks.md accepts keys in both languages
  checks: build, test

- [x] T3 `apron check` runs `checks.toml` and writes one event per check
  scope: src/**, tests/**
  depends: T1
  done-when: `apron check` shows each check with ok and duration and records them in the current run
  checks: build, test

- [ ] T4 Scope meter: files changed in git compared with the task's globs
  scope: src/**, tests/**
  depends: T2
  done-when: `apron run finish` records the touched files and the ones that left the scope
  checks: build, test

- [ ] T5 Claude Code hooks call `apron report` for the agent's state
  scope: integrations/claude-code/**, docs/**
  depends: T1
  done-when: an example `.claude/settings.json` makes Claude Code emit step, ask and run.finished by itself
  checks: build, test

- [ ] T6 Minimal `aprond`: unix socket, protocol frames, PTYs and `sessions.list`
  scope: src/daemon/**, src/proto/**, tests/**
  depends: T1
  done-when: `aprond` starts, `apron new` creates a session and `apron ls` lists it with the foreground agent
  checks: build, test

- [ ] T7 Events go to the daemon when it exists, with the local file as fallback
  scope: src/**, tests/**
  depends: T6
  done-when: with `aprond` running, `apron report` delivers over the socket; without it, it writes to `.apron/runs/`
  checks: build, test

- [ ] T8 Hub: token pairing, its own CA and outbound mTLS connections
  scope: src/hub/**, src/proto/**, tests/**
  depends: T6
  done-when: two paired machines show up in `apron machines` and the hub routes `sessions.list` to each
  checks: build, test
  decision: the token and certificate format affect the security of every machine

- [ ] T9 TUI renderer: cell grid, diff and synchronized output
  scope: src/tui/**, tests/**
  depends: T6
  done-when: a test frame draws and redraws only the changed cells
  checks: build, test

- [ ] T10 Progress view across machines, from the daemon's events
  scope: src/tui/**, tests/**
  depends: T7, T9, T11
  done-when: the progress view shows the runs of every project and machine with the agent states
  checks: build, test
