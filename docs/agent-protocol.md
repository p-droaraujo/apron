# Agent protocol

How an AI agent works on an Apron project from start to finish, and how it tells what it is doing.

The principle: **the agent narrates its work in short, structured events; Apron turns those events into progress, cost, scope and review.** The agent never explains what Apron can measure on its own, such as changed files, time or tokens.

## 1. The project

An Apron project is a git repository with an `.apron/` folder:

| File | Versioned | What for |
| --- | --- | --- |
| `.apron/tasks.md` | yes | The plan: tasks, scope, dependencies, done-when |
| `.apron/checks.toml` | yes | Commands that tell whether the work is good |
| `.apron/runs/` | no | Events of each run, one file per run |

That is all the configuration there is. `apron` finds the project by walking up from the current folder to the first `.apron/`.

Everything that belongs to the project lives in the repository and changes by commit. Run events belong to the machine and stay out of git.

## 2. The `tasks.md` format

Markdown that both people and machines read. A task is a list item with a stable id, followed by indented `key: value` fields.

```markdown
## v0a · Observe

- [ ] T6 Minimal daemon keeps sessions and answers sessions.list
  scope: src/daemon/**, tests/daemon/**
  depends: T1
  done-when: `aprond` starts and `apron new` creates a PTY that `sessions.list` reports
  checks: build, test
```

The mark between brackets is the state:

| Mark | State | Who changes it |
| --- | --- | --- |
| `[ ]` | Open | A person or the Planner |
| `[~]` | In progress, with `by: <agent>@<machine>` | `apron run start` |
| `[?]` | Waiting for a decision from a person | `apron report ask` |
| `[x]` | Done | `apron run finish --result done` |
| `[-]` | Abandoned | A person |

Fields:

| Field | Required | Meaning |
| --- | --- | --- |
| `scope` | yes | Globs of the files the task may touch; anything outside is scope drift |
| `done-when` | yes | The verifiable criterion for done, in one sentence |
| `depends` | no | Ids that must be done first |
| `checks` | no | Check names; without the field, all checks run |
| `decision` | no | Why this task needs a person |
| `by` | no | Written by Apron when someone picks the task up |
| `review` | no | Written by Apron when a person returns a run: what the next agent must change |

Keys are English, like the keys of any file format; titles and values can be in any language.

A task without `scope` or `done-when` cannot be picked up: Apron refuses `run start` and says what is missing. That is the minimum contract for work to be reviewable.

## 3. The life of a run

```
apron run start T6 --intent "..."      →  [~] in tasks.md, run.started event
apron report step "..."                 →  trajectory
apron report ask "..."                  →  ◐ waiting for you; [?] in tasks.md
apron report blocked "..."              →  ⊘ no power, with the reason
apron report note "..."                 →  a lesson the next run should know
apron check                             →  runs the checks, one check event per command
apron run finish --result done "..."    →  [x], summary, goes to Review
git commit (with Apron-Run: <id>)       →  tasks.md and code change together
```

Then a person reviews the run (Enter on its card in Gates): **merge** fast-forwards `main` to the run's commit and never merges over a `main` that moved on; **return** reopens the task with the person's comment in its `review` field; **discard** reopens the task and deletes nothing.

The commit comes after `finish`, because `finish` changes the state in `tasks.md`. The `Apron-Run` trailer links the commit to the run, and the commit body carries the summary: events stay out of git, but the story of the work survives the push.

Every command writes one event and returns in under 10 ms. None of them blocks the agent.

The file in `.apron/runs/` is always the source. If the machine's `aprond` is running, the CLI also tells it about the event, without waiting for an answer; that is how the daemon knows every project on the machine. With the daemon down, nothing is lost.

### Parallel runs: lanes

Runs can work at the same time when their scopes do not meet: `web/**`, `api/**` and `docs/**` are clean lanes. When a run starts and its scope meets the scope of a run still in progress, `apron run start` warns and records the overlap as a note in the new run. The comparison is by the globs' literal prefixes, so it may warn for globs that never touch (`src/*.cpp` and `src/*.hpp`), but it never misses two that do. On a warning, an agent asks before going on. Step-by-step work and small fixes stay in one run.

### Builder and verifier

A check runs with the run's context in its environment: `APRON_RUN`, `APRON_TASK`, `APRON_TASK_TEXT` (the task line and its fields) and `APRON_BASE` (the commit the run started from). That is enough for a check that judges the run itself.

`integrations/verify/apron-verify.sh` is such a check: a fresh agent, with no memory of how the work was done and no tools that write, reads the task and the diff and ends with `VERDICT: PASS` or `VERDICT: FAIL`. With a focus (`ux`, `simplicity`, `edge-cases`, `security`) it is one reviewer of a team; run several to have the team. Any agent works: `APRON_VERIFY_AGENT` is the command, and it reads the prompt on stdin.

A run started inside an `apron` session records the session (`session` in `run.started`), so the run and the agent's terminal can be found from each other.

## 4. Events

One JSON line per event in `.apron/runs/<run>.jsonl`:

```json
{"ts":"2026-11-19T20:31:04Z","run":"r-T1-0001","task":"T1","agent":"claude","machine":"nexus","kind":"step","msg":"tasks.md parser passes its tests"}
```

| `kind` | When | Extra fields | Shows up as |
| --- | --- | --- | --- |
| `run.started` | On picking up a task | `intent`, `base` (commit), `session` (if inside one) | A card in Running, ● |
| `step` | On changing work phase |  | A point on the trajectory |
| `ask` | A decision is needed |  | ◐ and a card in Gates |
| `blocked` | Something outside stops progress |  | ⊘ with the reason |
| `note` | Something the next run should know |  | A line in the session log |
| `check` | A check ran | `name`, `ok`, `ms` | The checks instrument |
| `run.finished` | On finishing | `result` (done, failed, abandoned) | The card waits in Gates for review |
| `review` | A person reviewed the run | `action` (merged, returned, discarded) | The card leaves Gates |

Writing rules for the agent:

- One sentence per event, present tense, saying what changed: "tasks.md parser passes its tests", not "working on the parser".
- Write in the language the project's people read; this repository uses English.
- A `step` on each change of phase (read, plan, edit, test, fix), not on each command. Three to ten per run.
- `ask` only for what is a person's decision: business rules, anything customer-facing, architecture, scope. Technical doubts the agent solves itself.
- If staying inside the `scope` forces a worse design, that is an `ask` too: say which better design needs which extra paths, and let the person choose.
- The `run.finished` summary answers three questions: what was done, what was left out, and what the person should look at first.

## 5. How each agent connects

| Agent | How state arrives | How events arrive |
| --- | --- | --- |
| Claude Code | Hooks call `apron hook` (PreToolUse, PostToolUse, Notification, UserPromptSubmit, Stop, SessionEnd); see `integrations/claude-code/` | The agent calls `apron report` from the shell, guided by `AGENTS.md`; the hooks fill the gaps |
| Codex | `notify` calls `apron report` | Shell, guided by `AGENTS.md` |
| Pi | Its own extension | Extension and shell |
| Others | Foreground process | Shell, if the agent reads `AGENTS.md` |

`AGENTS.md` at the repository root is the entry point: any agent that reads project instructions learns the protocol from it.

## 6. What Apron measures on its own

The agent reports none of this; Apron takes it from git, telemetry and the clock:

- files changed, and what left the `scope` (`.apron/tasks.md` never counts: Apron itself writes the task state there);
- duration of the run and of each step;
- check results, when they run through `apron check`.

Tokens and cost (OpenTelemetry) are planned, not built yet.

## 7. Languages

The interface is in English by default, with Brazilian Portuguese as the second language. `APRON_LANG` (`en` or `pt-BR`) chooses; without it, Apron follows the system locale and falls back to English. Code, comments, commits and these docs are in English. Sentences written by the Claude Code hooks follow the machine's language, like the interface.
