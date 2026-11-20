#!/bin/sh
# Builder + verifier as an Apron check: a fresh agent, with no memory of how
# the work was done and no tools that write, reads the task and the diff and
# says whether the change does what the task asked.
#
# In .apron/checks.toml (see README.md next to this file):
#   [[check]]
#   name = "verify"
#   run  = "integrations/verify/apron-verify.sh"
#   timeout = 900
#
# A focus turns it into one reviewer of a team: ux, simplicity, edge-cases,
# security, or any word you like ("apron-verify.sh security").
#
# APRON_VERIFY_AGENT is the agent command; it reads the prompt on stdin and
# prints its answer. The default is Claude Code without its writing tools.
# APRON_VERIFY_MAX_LINES caps the diff in the prompt (default 4000).
set -u

focus=${1:-}
if [ -z "${APRON_BASE:-}" ]; then
  echo "apron-verify: run it with apron check, inside a run (APRON_BASE is not set)" >&2
  exit 2
fi
agent=${APRON_VERIFY_AGENT:-"claude -p --disallowedTools Edit Write NotebookEdit Bash"}
max=${APRON_VERIFY_MAX_LINES:-4000}

case "$focus" in
  "") ask="Does the change do what the task asks, as its done-when states, and nothing it should not?" ;;
  ux) ask="Review only the user experience: wording, defaults, error messages, what a person sees and types." ;;
  simplicity) ask="Review only simplicity: code that could be shorter, duplicated, dead, or more general than the task needs." ;;
  edge-cases) ask="Review only edge cases: empty, huge, malformed or concurrent input, failures, timeouts, other platforms." ;;
  security) ask="Review only security: injection, paths, permissions, secrets, data from files or the network trusted too much." ;;
  *) ask="Review only this aspect: $focus." ;;
esac

untracked=$(git ls-files --others --exclude-standard)
diff=$(git diff "$APRON_BASE" -- . | head -n "$max")
lines=$(git diff "$APRON_BASE" -- . | wc -l)

out=$(
  {
    echo "You are a verifier. Another agent did the task below; you did not see how."
    echo "You can read files but not change them. $ask"
    echo
    echo "## Task"
    echo "${APRON_TASK_TEXT:-${APRON_TASK:-unknown}}"
    echo
    echo "## Diff against the commit the run started from"
    [ "$lines" -gt "$max" ] && echo "(cut at $max of $lines lines; read the files for the rest)"
    echo "$diff"
    if [ -n "$untracked" ]; then
      echo
      echo "## New files not in the diff (read them)"
      echo "$untracked"
    fi
    echo
    echo "Answer with at most 10 short lines of findings, most important first."
    echo "Fail only for what matters: a wrong or missing behavior, a real risk."
    echo "The last line must be exactly VERDICT: PASS or VERDICT: FAIL."
  } | sh -c "$agent" 2>&1
)
status=$?
echo "$out"
if [ $status -ne 0 ]; then
  echo "apron-verify: the agent command failed ($status): $agent" >&2
  exit 2
fi
verdict=$(printf '%s\n' "$out" | grep -E '^VERDICT: (PASS|FAIL)[[:space:]]*$' | tail -n 1)
case "$verdict" in
  "VERDICT: PASS"*) exit 0 ;;
  "VERDICT: FAIL"*) exit 1 ;;
  *) echo "apron-verify: no VERDICT line in the answer" >&2; exit 2 ;;
esac
