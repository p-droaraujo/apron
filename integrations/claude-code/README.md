# Claude Code hooks

With these hooks, Claude Code tells Apron what state it is in by itself, even when the agent forgets to narrate. The agent still writes its own `apron report` sentences (see `AGENTS.md`); the hooks fill the gaps.

| Claude Code event | What Apron gets |
| --- | --- |
| `PreToolUse` | A `step` when the work changes phase: read, plan, edit, build or test. One per change, not one per tool call. The agent's own `apron` calls do not count. |
| `Notification` | An `ask` when Claude Code waits for permission (with its message) or for input. No second ask while one is still open. |
| `UserPromptSubmit` | A `step` when the person answers an open ask, so the task goes from `[?]` back to `[~]`. |
| `Stop` | With the run still open, Claude is told to close it (`apron check`, then `apron run finish`) or to `apron report ask`; that is how `run.finished` happens without anyone typing it. If Claude stops again with the run open, the hook writes the `ask` for it. |
| `SessionEnd` | `blocked` (cause `other`) when the session ends with the run still open. |

The hooks write only inside an Apron project (a folder with `.apron/` above the session's directory) and only while a run is open (`apron run current`). Everywhere else they do nothing. They never make Claude Code fail: any error exits 0 in silence.

## Install

Merge `settings.json` into your project's `.claude/settings.json` (shared) or `.claude/settings.local.json` (just you). The example points at `$CLAUDE_PROJECT_DIR/integrations/claude-code/apron-hook.sh`, the path in this repository; in another project, copy `apron-hook.sh` somewhere and change the path.

The script finds `apron` in this order: `$APRON_BIN`, `$CLAUDE_PROJECT_DIR/build/apron`, `build/apron` under the project, then `apron` on `PATH`.

Set `APRON_HOOK_STEPS=0` to turn off the phase steps if the agent already narrates its work well.

It needs nothing but POSIX `sh` and `sed`.

## Test

```sh
integrations/claude-code/test.sh build/apron
HOOK_SHELL=dash integrations/claude-code/test.sh build/apron
```

It plays a session against a throwaway project and checks every event the hooks leave. It uses `git` and `jq`.
