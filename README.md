# apron

A terminal control panel for AI coding agents across all your machines.

The apron is where aircraft park, refuel and get checked between flights. Apron does the same for AI agents: it shows every agent, project and machine on one screen, measures what each agent did, and takes you to the right terminal in one keystroke. You control agents in their terminals; Apron watches, measures and tells you when something needs you.

> Early days. Apron is being built by an AI agent, with Apron itself: every task in this repository is planned in `.apron/tasks.md` and reported through `apron report`.

## What works today

The event layer (task T1): an agent starts a run, narrates it in short structured events, and finishes it. Anyone can read the timeline.

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

## How it fits together

- `aprond` — a daemon on each machine that owns terminals and agents (coming in T6).
- `aprond hub` — runs on an always-on machine, links all machines over Tailscale with mTLS (T8).
- `apron` — the TUI, in any modern terminal (Ghostty recommended).

Design docs are in Portuguese for now:

- [`docs/agent-protocol.md`](docs/agent-protocol.md) — how an agent works on a project end to end and what it reports.
- [`AGENTS.md`](AGENTS.md) — the short version, read by any agent that opens this repository.
- [`.apron/tasks.md`](.apron/tasks.md) — the plan.

## Requirements

C++20 (GCC 13+ or Clang 16+), CMake 3.25+, Ninja. Linux or macOS. No other dependencies yet.
