# apron

A terminal control panel for AI coding agents across all your machines.

The apron is where aircraft park, refuel and get checked between flights. Apron does the same for AI agents: it shows every agent, project and machine on one screen, measures what each agent did, and takes you to the right terminal in one keystroke. You control agents in their terminals; Apron watches, measures and tells you when something needs you.

> Early days. Apron is being built by an AI agent, with Apron itself: every task in this repository is planned in `.apron/tasks.md` and reported through `apron report`.

## What works today

`apron` with no arguments opens the progress view: four lanes (plan, running, gates, done) built from `.apron/tasks.md` and the run events, refreshing as agents report. Arrows or hjkl move, Enter opens a run's timeline or a task's fields, `q` quits. It reads local files; the view across machines comes with T10.

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

## Builder, verifier and lanes

A check can call an agent. `integrations/verify/apron-verify.sh` gives a fresh agent, which cannot write, the task and the diff of the run, and passes or fails on its verdict. A focus makes it one reviewer of a team:

```toml
[[check]]
name = "verify"
run  = "integrations/verify/apron-verify.sh"            # or: ... security, ux, simplicity, edge-cases
timeout = 900
```

```sh
apron check verify                                                   # after the work, before finish
apron check review-ux review-simplicity review-edge-cases review-security   # the team, before a merge
```

The default agent is `claude -p` without its writing tools; set `APRON_VERIFY_AGENT` for another (it reads the prompt on stdin). Checks get `APRON_RUN`, `APRON_TASK`, `APRON_TASK_TEXT` and `APRON_BASE`, so your own checks can judge a run too.

Lanes: `apron run start` warns, and notes it in the run, when the task's scope meets the scope of a run still in progress. Parallel runs are for work that divides cleanly by folder.

## Review

Finished runs wait in Gates until you review them. `v` opens the Review tab: failing checks and scope drift first; Enter shows the trajectory, the checks, the scope and the changed files; `m` merges into `main`, `r` returns the run with a comment the next agent reads, `d` discards it. `apron review` does the same from the shell.

Enter on a running card marked ⌨ takes over the agent's terminal (the run was started inside an `apron new` session on this machine); Ctrl-] brings you back to the board. `o` opens the run's log instead.

`y` opens the Activity tab: one timeline of every project on the machine that `aprond` knows, newest first.

## Sessions that outlive your terminal

`aprond` keeps terminal sessions alive on each machine; `apron` starts it on demand.

```sh
./build/apron new -d -- claude      # a session running an agent, in the background
./build/apron ls                    # ● 1  claude   3m  ~/code/shop
./build/apron attach 1              # take over its terminal; Ctrl-] detaches, the agent keeps going
./build/apron kill 1
./build/apron projects              # every project on this machine, with its runs
```

## Other machines, over SSH

Every machine runs its own `aprond`. Another machine reaches it through `ssh <host> apron proxy`, which carries the same protocol over SSH. There is no hub, token or certificate: if `ssh nexus` works without a password prompt (keys, an agent, or Tailscale SSH), Apron works, and the access Apron has there is the access your SSH login has.

```sh
apron machines add nexus            # a host from ~/.ssh/config; Apron must be installed there
apron machines add buildbox ~/.local/bin/apron   # when apron is not on the PATH of a non-interactive ssh
apron machines                      # each machine, reachable or not, with what runs there
apron ls nexus                      # its sessions
apron attach nexus:3                # take over session 3 on nexus; Ctrl-] detaches
apron machines rm buildbox
```

The list lives in `~/.config/apron/machines`, one host per line. `APRON_SSH` replaces the `ssh` command (the tests use it).

## Claude Code reports by itself

With the hooks in [`integrations/claude-code/`](integrations/claude-code/) (already on in this repository), Claude Code tells Apron when it reads, edits, builds or tests, when it waits for you, and it closes its run before ending its turn.

## How it fits together

- `aprond` — a daemon on each machine that owns terminals and agents.
- `apron proxy` — what `ssh <host>` runs to reach that machine's `aprond`; machines talk over SSH only.
- `apron` — the TUI, in any modern terminal (Ghostty recommended).

Design docs are in Portuguese for now:

- [`docs/agent-protocol.md`](docs/agent-protocol.md) — how an agent works on a project end to end and what it reports.
- [`AGENTS.md`](AGENTS.md) — the short version, read by any agent that opens this repository.
- [`.apron/tasks.md`](.apron/tasks.md) — the plan.

## Requirements

C++20 (GCC 13+ or Clang 16+), CMake 3.25+, Ninja. Builds are optimized (Release) unless you pass `-DCMAKE_BUILD_TYPE=Debug`; an existing `build/` keeps the type it was configured with, so run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` once. Linux or macOS (on a Mac: `xcode-select --install` and `brew install cmake ninja`). No other dependencies yet.
