#!/bin/sh
# Plays a Claude Code session against apron-hook.sh in a throwaway project and
# checks the events it leaves in the run.
#
#   integrations/claude-code/test.sh [path/to/apron]
set -u

here=$(cd "$(dirname "$0")" && pwd)
hook=$here/apron-hook.sh
APRON_BIN=$(cd "$(dirname "${1:-$here/../../build/apron}")" && pwd)/$(basename "${1:-apron}")
export APRON_BIN APRON_LANG=en APRON_AGENT=claude
unset CLAUDE_PROJECT_DIR APRON_SESSION

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
project=$tmp/project
mkdir -p "$project/.apron"
cat >"$project/.apron/tasks.md" <<'EOF'
# Tasks

- [ ] H1 A task for the hook
  scope: src/**
  done-when: the hook reports
EOF
(cd "$project" && git init -q && git add . && git -c user.name=t -c user.email=t@t commit -qm init)

failures=0
pass() { printf 'ok    %s\n' "$1"; }
failed() { printf 'FAIL  %s\n' "$1"; failures=$((failures + 1)); }

# Sends one hook event; its stdout is kept in $out.
send() {
  out=$(printf '%s' "$1" | ${HOOK_SHELL:-sh} "$hook")
}
kinds() {
  sed -n 's/.*"kind":"\([^"]*\)".*/\1/p' "$project"/.apron/runs/*.jsonl | tr '\n' ' '
}
expect_kinds() {
  if [ "$(kinds)" = "$2" ]; then pass "$1"; else failed "$1: got '$(kinds)', want '$2'"; fi
}
state() {
  sed -n 's/^- \[\(.\)\] H1.*/\1/p' "$project/.apron/tasks.md"
}
cwd="\"cwd\":\"$project\""

send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Read\"}"
if [ -z "$(ls "$project/.apron/runs" 2>/dev/null)" ]; then pass "no run, no events"; else failed "no run, no events"; fi

(cd "$project" && "$APRON_BIN" run start H1 --intent "test the hook" >/dev/null)

send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Read\",\"tool_input\":{\"file_path\":\"a\"}}"
send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Grep\"}"
expect_kinds "a phase change is one step" "run.started step "

send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"./build/apron report step \\\"x\\\"\"}}"
expect_kinds "the agent's own apron calls are not a phase" "run.started step "

send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Edit\"}"
send "{\"hook_event_name\":\"PreToolUse\",$cwd,\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"ctest --test-dir build\"}}"
expect_kinds "edit and test are steps" "run.started step step step "

send "{\"hook_event_name\":\"Notification\",$cwd,\"message\":\"Claude needs your permission to use \\\"Bash\\\"\",\"notification_type\":\"permission_prompt\"}"
expect_kinds "a permission prompt is an ask" "run.started step step step ask "
if grep -q 'waits for permission: Claude needs your permission to use \\"Bash\\"' "$project"/.apron/runs/*.jsonl; then
  pass "the ask carries Claude Code's message"
else
  failed "the ask carries Claude Code's message"
fi
if [ "$(state)" = "?" ]; then pass "the task waits: [?]"; else failed "the task waits: got [$(state)]"; fi

send "{\"hook_event_name\":\"Notification\",$cwd,\"message\":\"Claude is waiting for your input\",\"notification_type\":\"idle_prompt\"}"
expect_kinds "no second ask while one is open" "run.started step step step ask "

send "{\"hook_event_name\":\"Stop\",$cwd,\"stop_hook_active\":false}"
if [ -z "$out" ]; then pass "Stop lets Claude wait on an open ask"; else failed "Stop lets Claude wait on an open ask"; fi

send "{\"hook_event_name\":\"UserPromptSubmit\",$cwd,\"prompt\":\"yes, go on\"}"
expect_kinds "the person's answer is a step" "run.started step step step ask step "
if [ "$(state)" = "~" ]; then pass "the task is back in progress: [~]"; else failed "the task is back in progress: got [$(state)]"; fi

send "{\"hook_event_name\":\"UserPromptSubmit\",$cwd,\"prompt\":\"and this too\"}"
expect_kinds "a prompt with no ask open writes nothing" "run.started step step step ask step "

send "{\"hook_event_name\":\"Stop\",$cwd,\"stop_hook_active\":false}"
if printf '%s' "$out" | grep -q '"decision":"block"' && printf '%s' "$out" | jq -e .reason >/dev/null 2>&1; then
  pass "Stop with the run open asks Claude to close it"
else
  failed "Stop with the run open asks Claude to close it: $out"
fi

send "{\"hook_event_name\":\"Stop\",$cwd,\"stop_hook_active\":true}"
expect_kinds "a second Stop is an ask on Claude's behalf" "run.started step step step ask step ask "

(cd "$project" && "$APRON_BIN" report step "back to work" && "$APRON_BIN" run finish --result done "done" 2>/dev/null)
send "{\"hook_event_name\":\"Stop\",$cwd,\"stop_hook_active\":false}"
send "{\"hook_event_name\":\"SessionEnd\",$cwd,\"reason\":\"prompt_input_exit\"}"
expect_kinds "a finished run gets nothing more" "run.started step step step ask step ask step run.finished "

(cd "$project" && "$APRON_BIN" run start H1 --force --intent "again" >/dev/null)
send "{\"hook_event_name\":\"SessionEnd\",$cwd,\"reason\":\"other\"}"
if tail -n 1 "$(ls -t "$project"/.apron/runs/*.jsonl | head -n 1)" | grep -q '"kind":"blocked"'; then
  pass "a session that ends with the run open leaves it blocked"
else
  failed "a session that ends with the run open leaves it blocked"
fi

[ "$failures" -eq 0 ] && echo "all passed" || echo "$failures failed"
[ "$failures" -eq 0 ]
