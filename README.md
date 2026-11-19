# apron

A terminal control panel for AI coding agents across all your machines.

The apron is where aircraft park, refuel and get checked between flights. Apron does the same for AI agents: it shows every agent, project and machine on one screen, measures what each agent did, and takes you to the right terminal in one keystroke. You control agents in their terminals; Apron watches, measures and tells you when something needs you.

> Early days. Apron is being built by an AI agent, with Apron itself: every task in this repository is planned in `.apron/tasks.md` and reported through `apron report`.

## What works today

`apron` with no arguments opens the progress view: four lanes (plan, running, gates, done) built from `.apron/tasks.md` and the run events, refreshing as agents report. Arrows or hjkl move, Enter opens a run's timeline or a task's fields, `q` quits. It reads local files only; the view across machines comes with the daemon and the hub.

```
 shop · mac                                                                            20:52 UTC
 PLAN 2                        RUNNING 3                     GATES 1                     DONE 1
 ───────────────────────────   ───────────────────────────   ─────────────────────────   ──────────────────────
 ◌ S5 Load tests        ← S4   ▌ ● pi@nexus          9m20s   ▌ ◐ codex@mac        9m20s   ✓ S1 Price parser   9m
 ◌ S7 Rate limit per c… ← S6   ▌ S6 JSON logs                ▌ S3 Email confirmation   
                               ▌ JSON format defined; rep…   ▌ change the confirmation…
 APRON  ◐ 1  ● 2  ⊘ 1                                                                        ? help
```

The interface is in English, with Brazilian Portuguese as the second language: set `APRON_LANG=pt-BR`, or let Apron follow the system locale. `tasks.md` accepts its keys in either language (`scope` or `escopo`, `done-when` or `pronto`).

Under it, the event layer: an agent starts a run, narrates it in short structured events, and finishes it.

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/apron run start T2 --agent claude --intent "parse and update tasks.md"
./build/apron report step "parser reads ids, state and fields"
./build/apron report ask "should abandoned tasks stay in the file?"
./build/apron run finish --result done "parser and writer done; review the state markers first"
./build/apron log
```

```
T2 · claude@nexus · 12m04s · ✓ feita

  0s      ▶ iniciou      parse and update tasks.md
  41s     · passo        parser reads ids, state and fields
  3m10s   ◐ pergunta     should abandoned tasks stay in the file?
  12m04s  ■ terminou     done · parser and writer done; review the state markers first
```

## Sessions that outlive your terminal

`aprond` keeps terminal sessions alive on each machine; `apron` starts it on demand.

```sh
./build/apron new -d -- claude      # a session running an agent, in the background
./build/apron ls                    # ● 1  claude   3m  ~/code/shop
./build/apron attach 1              # take over its terminal; Ctrl-] detaches, the agent keeps going
./build/apron kill 1
./build/apron projects              # every project on this machine, with its runs
```

## How it fits together

- `aprond` — a daemon on each machine that owns terminals and agents.
- `aprond hub` — runs on an always-on machine, links all machines over Tailscale with mTLS (T8).
- `apron` — the TUI, in any modern terminal (Ghostty recommended).

Design docs are in Portuguese for now:

- [`docs/agent-protocol.md`](docs/agent-protocol.md) — how an agent works on a project end to end and what it reports.
- [`AGENTS.md`](AGENTS.md) — the short version, read by any agent that opens this repository.
- [`.apron/tasks.md`](.apron/tasks.md) — the plan.

## Requirements

C++20 (GCC 13+ or Clang 16+), CMake 3.25+, Ninja. Linux or macOS (on a Mac: `xcode-select --install` and `brew install cmake ninja`). No other dependencies yet.
