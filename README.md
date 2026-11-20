# Apron

**One screen for every AI coding agent on every machine you own.**

Run Claude Code, Codex or Pi on your laptop, your home server and your build box. Apron shows what each one is doing, which ones are waiting for you, and what they changed, and takes you to the right agent's terminal in one keystroke. The agents keep working when you close the screen, put the laptop to sleep or lose the connection.

```
 shop · mac                                                                            20:52
 PLAN 2                        RUNNING 3                     GATES 1                     DONE 1
 ───────────────────────────   ───────────────────────────   ─────────────────────────   ──────────────────────
 ◌ S5 Load tests        ← S4   ▌ ● pi@nexus     ⌨ 9m20s     ▌ ◐ codex@mac        9m20s   ✓ S1 Price parser   9m
 ◌ S7 Rate limit per c… ← S6   ▌ S6 JSON logs                ▌ S3 Email confirmation   
                               ▌ JSON format defined; rep…   ▌ change the confirmation…
 APRON  ◐ 1  ● 2  ⊘ 1                                                                        ? help
```

> **Early release.** Apron works, and Apron is built with Apron, but it is young: expect rough edges and breaking changes. Linux and macOS.

## Why Apron

- **Agents in terminals, not in a new IDE.** You keep the agents and the terminal you already use. Apron watches and measures; the control stays in the terminal.
- **Many machines, no server.** Every machine runs a small daemon; machines reach each other over plain SSH. No account, no cloud, no hub to keep running.
- **Work you can review.** Each task declares the files it may touch and what "done" means. Apron measures what an agent actually changed, runs your checks, and puts finished work in front of you to merge, return with a comment, or discard.
- **Small and fast.** Two binaries under 2 MB, written in C++20 with no dependencies. The screen uses under 1% CPU with hundreds of runs.

## What works today

| | |
| --- | --- |
| **Progress board** | Plan, Running, Gates, Done, built from your tasks and the agents' reports, refreshed every second |
| **Sessions that outlive your terminal** | Start an agent in a session kept by `aprond`; detach, close the laptop, attach again later |
| **One keystroke to the agent** | Enter on a running card opens that agent's terminal; Ctrl-] brings you back |
| **Machines over SSH** | Sessions and activity of every machine you can `ssh` into |
| **Review** | Failing checks and scope drift first; merge, return or discard from the board |
| **Activity** | One timeline of everything every agent did, newest first |
| **Scope meter** | Which changed files are outside what the task allowed |
| **Checks, verifier and reviewer team** | Your build and tests, plus a fresh read-only agent that judges the diff against the task |
| **Claude Code hooks** | Claude Code reports reading, editing, testing and waiting for you by itself |
| **English and Portuguese** | Follows the system language, or `APRON_LANG` |

## Install

Apron builds from source in about a minute. You need a C++20 compiler (GCC 13+ or Clang 16+), CMake 3.25+ and Ninja; on a Mac, `xcode-select --install` and `brew install cmake ninja`.

```sh
git clone https://github.com/p-droaraujo/apron && cd apron
cmake -S . -B build -G Ninja && cmake --build build
cmake --install build --prefix ~/.local --strip      # apron and aprond in ~/.local/bin
```

Do the same on every machine where agents will run. A Homebrew tap is planned.

## Quick start

```sh
cd ~/code/my-project
apron new -- claude          # an agent in a session that survives your terminal
```

Detach with Ctrl-], then open the board:

```sh
apron                        # arrows move, Enter opens, ? shows the keys, q quits
```

To follow other machines, add them by the name you use with `ssh`:

```sh
apron machines add nexus     # Apron must be installed there; ssh must work without a password prompt
```

## Plan the work, and let agents report it

Agents work on tasks from `.apron/tasks.md`, a plain Markdown file in your repository:

```markdown
- [ ] T6 Minimal daemon keeps sessions and answers sessions.list
  scope: src/daemon/**, tests/daemon/**
  depends: T1
  done-when: `aprond` starts, `apron new` creates a PTY and `apron ls` lists it
  checks: build, test
```

An agent picks a task, narrates it in one-line events (`apron report step "..."`, `apron report ask "..."`), runs the checks and closes the run. Apron turns that into the board, the timeline, the scope meter and the review. Put [`AGENTS.md`](AGENTS.md) in your repository and any agent that reads project instructions learns how; the full protocol is in [`docs/agent-protocol.md`](docs/agent-protocol.md).

Checks live in `.apron/checks.toml`:

```toml
[[check]]
name = "test"
run  = "ctest --test-dir build"

[[check]]
name = "verify"                                  # a fresh agent that cannot write judges the diff
run  = "integrations/verify/apron-verify.sh"     # add a focus for a reviewer: security, ux, simplicity, edge-cases
timeout = 900
```

With the hooks in [`integrations/claude-code/`](integrations/claude-code/), Claude Code reports its own state, and closes its run before it ends its turn.

## Many machines

Every machine runs its own `aprond`, which keeps the sessions and the events. When you open Apron, it reaches the others through `ssh <host> apron proxy`. There is no hub, token or certificate: if `ssh nexus` works, Apron works, and Apron can do on that machine exactly what your SSH login can. Keys, an SSH agent, ProxyJump and Tailscale SSH all work as they do today.

```sh
apron machines                    # every machine, reachable or not, with what runs there
apron attach nexus:3              # take over session 3 on nexus; Ctrl-] detaches
```

## Status and roadmap

Apron is planned in the open, in [`.apron/tasks.md`](.apron/tasks.md), and built by AI agents through Apron itself: every commit carries the timeline of the run that produced it.

Next: sessions and machines inside the screen (fewer commands to remember), agents that survive an upgrade of `aprond`, a project memory every agent reads at the start, git worktrees for parallel attempts, and token and cost tracking.

## Contributing

Issues and ideas are welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before a pull request: work here goes through tasks and runs, like the agents' work.

## License

Apache License 2.0; see [`LICENSE`](LICENSE). The Apron name, logo and look are not covered by the license; see [`TRADEMARKS.md`](TRADEMARKS.md).
