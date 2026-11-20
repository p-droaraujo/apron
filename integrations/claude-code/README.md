# Claude Code hooks

With these hooks, Claude Code tells Apron what state it is in by itself, even when the agent forgets to narrate. The agent still writes its own `apron report` sentences (see `docs/agent-protocol.md`); the hooks fill the gaps.

| Claude Code event | What Apron gets |
| --- | --- |
| `PreToolUse` | A `step` when the work changes phase: read, plan, edit, build or test. One per change, not one per tool call. The agent's own `apron` calls do not count. |
| `PostToolUse` | A `step` when a tool runs while an ask is open: the person approved a permission prompt, so the card leaves gates. |
| `Notification` | An `ask` when Claude Code waits for permission (with its message) or for input. No second ask while one is open. |
| `UserPromptSubmit` | A `step` when the person answers an open ask, so the task goes from `[?]` back to `[~]`. |
| `Stop` | With the run still open, Claude is told to close it (`apron check`, then `apron run finish`) or to `apron report ask`. If Claude stops again with the run open, the hook writes the `ask` for it. |
| `SessionEnd` | `blocked` when the session ends with the run still open. |

The hooks write only inside an Apron project with a run open, and never make Claude Code fail. Their sentences follow the machine's language, like the interface.

## Install

Merge `settings.json` into your project's `.claude/settings.json` (shared) or `.claude/settings.local.json` (just you). It runs `apron hook`, so `apron` must be on your `PATH`.

| Variable | Effect |
| --- | --- |
| `APRON_HOOK_STEPS=0` | No phase steps, for an agent that already narrates well |
| `APRON_HOOK_STOP=0` | Claude may end its turn with the run open; nothing nudges it to close the run |
