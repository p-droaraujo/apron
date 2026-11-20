# Apron

Apron is a terminal screen for the coding agents you run on your own machines. It shows which agents are working, which ones are waiting for you and what each one changed, and it takes you into an agent's terminal with one key. The agents keep running when you close the screen, put the laptop to sleep or lose the connection.

```
╭─ apron ──────────────────╮╭─ shop ─────────────────────────────────────────────────── Progress   Agents   Activity ──╮
│                          ││                                                                                          │
│ Projects                 ││ Plan 2            Running 1                 Gates 2                   Done 2             │
│                          ││                                                                                          │
│ shop                     ││ ○ S7 Rate limit…  ╭──────────────────────╮  ┏━━━━━━━━━━━━━━━━━━━━━━┓  ✓ S1 Price pars…   │
│   ● claude         6m53s ││ ◌ S5 Load tests   │ ● claude@mac   6m53s │  ┃ ◐ codex@mac    5m33s ┃  ✓ S4 Cache for …   │
│   ◐ codex          5m33s ││                   │ S6 JSON logs         │  ┃ S3 Order             ┃                     │
│                          ││                   │ format defined, rep… │  ┃ confirmation email   ┃                     │
│ Machines                 ││                   ╰──────────────────────╯  ┃ Can I send the emai… ┃                     │
│                          ││                   ╭┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄╮  ┃                      ┃                     │
│ ● mac       this machine ││                   ┆     + New agent      ┆  ┃ [ Answer ] [ Log ]   ┃                     │
│ + Add machine            ││                   ╰┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄╯  ┗━━━━━━━━━━━━━━━━━━━━━━┛                     │
│                          ││                                             1 more                                       │
│                          ││                                                                                          │
╰──────────────────────────╯╰──────────────────────────────────────────────────────────────────────────────────────────╯
 enter answer   tab next view   q quit
```

It runs inside the terminal you already use. Each machine runs a small daemon, `aprond`, that keeps the agents' terminals and their reports. The screen talks to the daemon on your machine directly and to the others over ssh. There is no account, no cloud service and no server in between.

Apron is early. It works, and it is built with itself, but expect rough edges and changes. It runs on Linux and macOS.

## Install

You need a C++20 compiler (GCC 13 or Clang 16 and later), CMake 3.25 or later, Ninja and Zig 0.17. On a Mac: `xcode-select --install` and `brew install cmake ninja zig`.

The agents' terminals are drawn with [libghostty-vt](https://github.com/ghostty-org/ghostty), the terminal core of Ghostty. The build fetches its release vt-1.0.1 and compiles it with Zig; the first build takes a few minutes.

```sh
git clone https://github.com/p-droaraujo/apron && cd apron
cmake -S . -B build -G Ninja && cmake --build build
cmake --install build --prefix ~/.local --strip
```

That puts `apron` and `aprond` in `~/.local/bin`. Install it the same way on every machine where agents run.

## Use

```sh
cd ~/code/shop
apron new -- claude     # an agent in a session that outlives this terminal; Ctrl-] leaves it running
apron                   # the screen
```

On the screen, the left side lists your projects with their sessions, and your machines. The right side shows the selected project: its board (the plan, what is running, what waits for you, what is done), its agents live side by side, and its activity.

Everything you can do is something on the screen: a card, a button, a session, a `+ New agent` place. Move to it with the arrows and press Enter, or click it. The footer says what Enter does where you are. Besides that there are only a few keys:

| Key | |
| --- | --- |
| ← → ↑ ↓ | move; on a card, → goes to its buttons |
| Enter | do what is selected |
| Esc | back one level |
| Tab, Shift-Tab | the next or previous view (Progress, Agents, Activity) |
| Ctrl-] | leave an agent's terminal; the agent keeps running |
| q | quit |

A finished run waits in Gates. Open it to see its steps, checks, scope and changed files, then merge it into `main`, return it to the agent with a comment, or discard it.

## Other machines

Choose `+ Add machine` and type a host you reach with `ssh`, such as a name from `~/.ssh/config`. Apron must be installed there, and ssh must log in without a password prompt (keys, an agent, or Tailscale SSH). Apron can do on that machine exactly what your ssh login can do. Its projects and sessions show in the sidebar, and its boards open like local ones.

## For agents

Agents work on tasks from `.apron/tasks.md`, a Markdown file in your repository, and report in one-line events:

```sh
apron run start S6 --agent claude --intent "JSON logs"
apron report step "format defined, replacing the old logger"
apron report ask "Can I send the email in English for now?"
apron check
apron run finish --result done "what was done; what was left out; what to look at first"
```

Put [`AGENTS.md`](AGENTS.md) in your repository and any agent that reads project instructions follows this. The full protocol is in [`docs/agent-protocol.md`](docs/agent-protocol.md). With the hooks in [`integrations/claude-code/`](integrations/claude-code/), Claude Code reports its own state. A check can also ask a second agent, one that cannot write, to judge the change against the task: see [`integrations/verify/`](integrations/verify/).

## Plan

The plan lives in [`.apron/tasks.md`](.apron/tasks.md), in three phases: v0a (see every agent and reach its terminal), v0b (merge with confidence), v0c (agents settle what they can on their own).

## Contributing

Issues and ideas are welcome. Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before a pull request.

## License

Apache License 2.0; see [`LICENSE`](LICENSE). The name, logo and look of Apron are not covered by the license; see [`TRADEMARKS.md`](TRADEMARKS.md).
