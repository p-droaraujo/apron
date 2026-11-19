# Claude Code hooks

With these hooks, Claude Code tells Apron what state it is in by itself, even when the agent forgets to narrate. The agent still writes its own `apron report` sentences (see `AGENTS.md`); the hooks fill the gaps.

| Claude Code event | What Apron gets |
| --- | --- |
| `PreToolUse` | A `step` when the work changes phase: read, plan, edit, build or test. One per change, not one per tool call. The agent's own `apron` calls do not count. |
| `PostToolUse` | A `step` when a tool runs while an ask is open: the person approved a permission prompt, so the card leaves gates. |
| `Notification` | An `ask` when Claude Code waits for permission (with its message) or for input. No second ask while one is open. |
| `UserPromptSubmit` | A `step` when the person answers an open ask, so the task goes from `[?]` back to `[~]`. |
| `Stop` | With the run still open, Claude is told to close it (`apron check`, then `apron run finish`) or to `apron report ask`; that is how `run.finished` happens without anyone typing it. If Claude stops again with the run open, the hook writes the `ask` for it. |
| `SessionEnd` | `blocked` (cause `other`) when the session ends with the run still open. |

The work is done by `apron hook`, in C++, with the same JSON parser as the rest of Apron; `apron-hook.sh` only finds the binary. The hooks write only inside an Apron project with a run open (`apron run current`), and never make Claude Code fail: any error exits 0 in silence. Hook sentences follow the project's `language` in `.apron/project.toml`.

## Install

Merge `settings.json` into your project's `.claude/settings.json` (shared) or `.claude/settings.local.json` (just you). This repository already has it on. The example points at `$CLAUDE_PROJECT_DIR/integrations/claude-code/apron-hook.sh`; in another project, copy the script somewhere and change the path.

The script finds `apron` in this order: `$APRON_BIN`, `$CLAUDE_PROJECT_DIR/build/apron`, then `apron` on `PATH`.

| Variable | Effect |
| --- | --- |
| `APRON_HOOK_STEPS=0` | No phase steps, for an agent that already narrates well |
| `APRON_HOOK_STOP=0` | Claude may end its turn with the run open; nothing nudges it to close the run |

## Test

The scenarios run in `ctest` (`tests/test_hooks.cpp`), through the real `apron` binary.
