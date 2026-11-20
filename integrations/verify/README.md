# Verifier and reviewer team

`apron-verify.sh` is an Apron check that asks a fresh agent whether a run did what its task asked. The agent never saw the work being done and has no tools that write; it reads the task, the diff since the run's base commit and any new files, and ends with `VERDICT: PASS` or `VERDICT: FAIL`. The check passes or fails on that line. No `VERDICT` line, or an agent that fails to run, is exit code 2.

```toml
[[check]]
name = "verify"
run  = "integrations/verify/apron-verify.sh"
timeout = 900

[[check]]
name = "review-security"
run  = "integrations/verify/apron-verify.sh security"
timeout = 900
```

Foci: `ux`, `simplicity`, `edge-cases`, `security`, or any word, which becomes "review only this aspect". Several focused checks make a reviewer team; each one is a separate agent that sees nothing the others said.

| Variable | Default | |
|---|---|---|
| `APRON_VERIFY_AGENT` | `claude -p --disallowedTools Edit Write NotebookEdit Bash` | The agent command. It reads the prompt on stdin and prints its answer. |
| `APRON_VERIFY_MAX_LINES` | `4000` | Lines of diff in the prompt; the agent reads the files for the rest. |

The script needs to run under `apron check`, inside a run: it reads `APRON_BASE`, `APRON_TASK_TEXT` and `APRON_TASK` from the environment.

Each verify costs one agent call. Tasks pick their checks in their `checks:` field, so `verify` runs only where you ask for it.
