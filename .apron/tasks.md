# Apron tasks

Apron's own plan, in the format described in `docs/agent-protocol.md`.

## v0a · Observe

See every agent on one screen and reach the right terminal in one keystroke.

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

- [x] T4 Scope meter: files changed in git compared with the task's globs
  scope: src/**, tests/**
  depends: T2
  done-when: `apron run finish` records the touched files and the ones that left the scope
  checks: build, test

- [x] T5 Claude Code hooks call `apron report` for the agent's state
  scope: integrations/claude-code/**, docs/**
  depends: T1
  done-when: an example `.claude/settings.json` makes Claude Code emit step, ask and run.finished by itself
  checks: build, test

- [x] T6 Minimal `aprond`: unix socket, protocol frames, PTYs and `sessions.list`
  scope: CMakeLists.txt, src/daemon/**, src/proto/**, src/cli/**, tests/**
  depends: T1
  done-when: `aprond` starts, `apron new` creates a session and `apron ls` lists it with the foreground agent
  checks: build, test

- [x] T7 The daemon learns about every project and run on its machine; the local file stays the source
  scope: CMakeLists.txt, src/**, tests/**, docs/agent-protocol.md, README.md
  depends: T6
  done-when: every event is still written to `.apron/runs/`; if `aprond` is running it is told too, `apron projects` lists the machine's projects with their runs, and a run started inside an `apron` session records which session it is
  checks: build, test

- [x] T8 Machines over SSH: each machine runs its own aprond; `apron proxy` carries the protocol over `ssh <host>`, with no hub, token or certificate
  scope: src/cli/**, src/daemon/**, src/proto/**, src/core/**, tests/**, CMakeLists.txt, README.md, docs/**
  depends: T6
  done-when: `apron machines add <host>` uses a host from ~/.ssh/config; `apron machines` shows each one reachable or not; `apron ls <host>` and `apron attach <host>:<id>` work through ssh; the daemon answers `events.recent`
  checks: build, test
  decision: SSH instead of a hub with its own CA (decided by Pedro on 2026-11-20): it reuses the keys and access developers already have; the access to a machine is the SSH access, with no extra permission levels in Apron

- [x] T9 TUI renderer: cell grid, diff and synchronized output
  scope: src/tui/**, tests/**
  depends: T6
  done-when: a test frame draws and redraws only the changed cells
  checks: build, test

- [x] T13 Claude Code hooks, second pass: a C++ `apron hook` instead of sed, the stuck ◐ after a permission prompt, and the hooks on in this repository
  scope: CMakeLists.txt, src/cli/**, src/core/i18n.cpp, tests/**, integrations/claude-code/**, .claude/settings.json, docs/agent-protocol.md, README.md
  depends: T5
  done-when: `apron hook <event>` reads Claude Code's JSON with the real parser; approving a permission prompt takes the card out of gates; the Stop nudge can be turned off; the hook tests run in ctest; this repository's own Claude Code sessions report through the hooks
  checks: build, test

- [x] T16 From a card to the agent's terminal: Enter on a running card attaches to its session
  scope: CMakeLists.txt, src/tui/**, src/cli/**, tests/**
  depends: T7, T11
  done-when: Enter on a running card whose run recorded a session leaves the TUI, attaches to that session, and Ctrl-] comes back to the TUI
  checks: build, test

- [x] T20 A product screen: a sidebar with projects, sessions and machines; cards as boxes with clickable buttons on the selected one; Tab cycles views, Esc goes back one level; Enter opens the agent's terminal on any card with a live session; n starts a session; a footer with the keys of the current screen; interface copy and docs following unslop-ui
  scope: src/tui/**, src/cli/**, src/daemon/server.cpp, src/core/i18n.cpp, tests/**, README.md, docs/**, AGENTS.md, CMakeLists.txt
  depends: T19
  done-when: the sidebar lists every machine's sessions by project and Enter attaches to one, local or over ssh; the board draws boxed cards with buttons that work by key and by mouse; Tab, Shift-Tab and Esc behave the same on every screen; the Review tab is gone and reviewing happens from a Gates card; no uppercase eyebrow labels, duplicated status or em dashes in the interface and docs
  checks: build, test

- [ ] T10 Progress view across machines, from the daemon's events
  scope: src/tui/**, tests/**
  depends: T7, T9, T11
  done-when: the progress view shows the runs of every project and machine with the agent states
  checks: build, test

- [ ] T22 Cut the commands: a person needs `apron` and `apron new`; agents use `run`, `report` and `check`; `hook`, `proxy` and `daemon` leave the help; the rest goes once the screen covers it
  scope: src/cli/**, src/core/i18n.cpp, tests/**, README.md, docs/**, AGENTS.md, integrations/**
  depends: T20
  done-when: `apron --help` lists two commands for people and three for agents; nothing the removed commands did is lost from the screen; tests and docs follow
  checks: build, test

## v0b · Review

Merge with confidence without opening the editor.

- [x] T14 Review tab: finished runs with trajectory, checks, scope and the diff, and the actions merge, return to the agent, discard
  scope: CMakeLists.txt, src/tui/**, src/cli/**, src/core/**, tests/**, README.md
  depends: T11, T4
  done-when: `v` in the TUI opens a queue of finished runs, failing checks and scope drift first; Enter shows the instruments and the changed files; m merges the run's branch into main, r writes the person's comment as the run's next prompt note, d discards
  checks: build, test

- [x] T15 Activity tab: one timeline of the events of every project on the machine
  scope: CMakeLists.txt, src/tui/**, src/cli/**, src/core/**, tests/**, README.md
  depends: T7, T11
  done-when: `y` in the TUI shows the latest events of all projects aprond knows, newest first, and updates live
  checks: build, test

- [x] T17 Code review pass: Release by default, a daemon that never blocks on a slow client or agent, git without a shell, cached run summaries
  scope: src/**, tests/**, CMakeLists.txt, README.md
  done-when: sanitizers (ASan, UBSan, leaks) stay clean on every test; a stalled client cannot freeze aprond; no value read from .apron/ reaches a shell; the board reloads 300 runs without re-reading unchanged files
  checks: build, test

- [x] T18 Builder + verifier and lanes: a `verify` check where a fresh read-only agent judges the diff against the task, the same with a focus for a reviewer team, and a warning when running tasks' scopes overlap
  scope: src/**, tests/**, integrations/**, docs/**, AGENTS.md, README.md, .apron/checks.toml
  depends: T3, T4
  done-when: checks get APRON_RUN, APRON_TASK, APRON_TASK_TEXT and APRON_BASE; `integrations/verify/apron-verify.sh [focus]` passes or fails on the agent's verdict, with any agent command; `apron run start` warns and records a note when its scope overlaps a run still in progress
  checks: build, test

- [x] T19 Cut the fat: two files in .apron/, English task keys, free-text blocked, one `machines` command for every project, scope inside `log`, no Review tab, the hook called directly, one source for the protocol
  scope: src/**, tests/**, integrations/**, docs/**, AGENTS.md, README.md, .apron/**, .claude/**, CMakeLists.txt
  depends: T18
  done-when: project.toml and policy.toml are gone and nothing reads them; tasks.md keys are English only; `apron projects`, `apron scope` and the Review tab are gone with what they showed still reachable; settings.json calls `apron hook`; AGENTS.md points to docs/agent-protocol.md; every test passes
  checks: build, test

- [x] T21 Ready to be public: Apache-2.0 license, trademark policy, contributing and security guides, repository description and topics
  scope: LICENSE, NOTICE, TRADEMARKS.md, CONTRIBUTING.md, SECURITY.md, README.md, .github/**
  depends: T19
  done-when: the files are in the repository, README links them, and the GitHub repository has a product description and topics; visibility stays a person's choice
  checks: build, test

- [ ] T23 `apron run finish` refuses files outside the task's scope unless the agent passes `--outside "reason"`, which the run records
  scope: src/cli/**, src/core/**, tests/**, docs/**, AGENTS.md, integrations/**
  depends: T19
  done-when: finishing with files outside the scope fails with the list; with --outside it passes and the reason shows in the log and the review
  checks: build, test

- [ ] T24 Agents survive an upgrade of aprond: the new daemon takes over the running sessions' terminals instead of closing them
  scope: src/daemon/**, src/proto/**, src/cli/**, tests/**, docs/**
  depends: T6
  done-when: replacing the aprond binary and restarting it keeps every session alive and attachable, with its replay
  checks: build, test

- [ ] T25 `apron brief`: the project's memory for the agent that starts, from notes, review comments and decisions, within a line budget
  scope: src/cli/**, src/core/**, tests/**, docs/**, AGENTS.md
  depends: T19
  done-when: `apron brief [task]` prints the notes of every run, the review comments and the task's decision within a budget of lines, and AGENTS.md tells agents to read it first
  checks: build, test

- [ ] T26 Worktrees and best of N: a run can live in its own git worktree, and several attempts at one task can run side by side with the checks picking the winner
  scope: src/cli/**, src/core/**, src/tui/**, tests/**, docs/**
  depends: T23
  done-when: `apron run start --worktree` and `--attempts N` work, and the review shows the attempts side by side with their checks
  checks: build, test

- [ ] T27 Tokens and cost per run, from the agents' telemetry
  scope: src/**, tests/**, docs/**, integrations/**
  depends: T20
  done-when: a Claude Code run shows its tokens and cost on its card, in the log and in the review
  checks: build, test

## v0c · Auto

The agent settles as many decisions as it can on its own.

- [ ] T28 Auto mode: a project in auto merges a finished run when every declared check passes and nothing left the scope; only decisions reach the person
  scope: src/**, tests/**, docs/**
  depends: T23, T26
  done-when: a project switched to auto by the person merges passing runs on its own, records why in Activity, and still stops at every ask
  checks: build, test

- [ ] T29 Push with a time window: in auto, merged work is pushed after a delay the person can cancel
  scope: src/**, tests/**, docs/**
  depends: T28
  done-when: an auto merge is pushed after the window unless the person cancels it from the screen
  checks: build, test
