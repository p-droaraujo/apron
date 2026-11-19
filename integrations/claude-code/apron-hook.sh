#!/bin/sh
# Claude Code hook: turns Claude Code's own events into `apron report`.
#
# Claude Code runs this on UserPromptSubmit, PreToolUse, Notification, Stop and
# SessionEnd, with the hook's JSON on stdin (see settings.json next to it).
# It only writes when the session is inside an Apron project with a run open:
#
#   PreToolUse        a step when the work changes phase (read, plan, edit, build, test)
#   Notification      an ask when Claude Code waits for permission or for input
#   UserPromptSubmit  a step when the person answers an ask, so the task leaves [?]
#   Stop              asks Claude to close the run (check + run finish) or to report ask;
#                     if it stops again with the run open, an ask on its behalf
#   SessionEnd        blocked, when the session ends with the run still open
#
# A hook must never get in Claude Code's way: every failure exits 0 in silence.
# POSIX sh and sed only.
#
# Environment:
#   APRON_BIN           the apron binary (default: $CLAUDE_PROJECT_DIR/build/apron, then apron on PATH)
#   APRON_HOOK_STEPS=0  no phase steps from PreToolUse; the agent narrates on its own

input=$(tr '\n\r' '  ')

# The value of a string field in the hook's JSON, unescaped and on one line.
field() {
  printf '%s' "$input" |
    sed -nE "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"(([^\"\\\\]|\\\\.)*)\".*/\\1/p" |
    sed -e 's/\\n/ /g' -e 's/\\t/ /g' -e 's/\\"/"/g' -e 's/\\\\/\\/g'
}

# True when a boolean field is true.
flag() {
  printf '%s' "$input" | grep -Eq "\"$1\"[[:space:]]*:[[:space:]]*true"
}

# One sentence for an event: at most 160 characters, no trailing spaces.
sentence() {
  printf '%s' "$1" | sed -e 's/[[:space:]][[:space:]]*/ /g' -e 's/^ //' -e 's/ $//' | cut -c1-160
}

event=$(field hook_event_name)
dir=$(field cwd)
[ -n "$dir" ] || dir=${CLAUDE_PROJECT_DIR:-$PWD}
cd "$dir" 2>/dev/null || exit 0

# The Apron project around this directory.
root=$PWD
while [ ! -d "$root/.apron" ]; do
  [ "$root" = / ] && exit 0
  root=$(dirname "$root")
done

apron=${APRON_BIN:-}
if [ -z "$apron" ]; then
  if [ -n "${CLAUDE_PROJECT_DIR:-}" ] && [ -x "$CLAUDE_PROJECT_DIR/build/apron" ]; then
    apron=$CLAUDE_PROJECT_DIR/build/apron
  elif [ -x "$root/build/apron" ]; then
    apron=$root/build/apron
  else
    apron=$(command -v apron) || exit 0
  fi
fi

run=$("$apron" run current 2>/dev/null) || exit 0
[ -n "$run" ] || exit 0
runfile=$root/.apron/runs/$run.jsonl
[ -f "$runfile" ] || exit 0
last=$(tail -n 1 "$runfile" | sed -n 's/.*"kind":"\([^"]*\)".*/\1/p')
phasefile=$root/.apron/runs/$run.claude-code-phase

report() {
  "$apron" report "$@" >/dev/null 2>&1
}

case $event in
  PreToolUse)
    [ "${APRON_HOOK_STEPS:-1}" = 0 ] && exit 0
    tool=$(field tool_name)
    phase=
    case $tool in
      Read|Grep|Glob|LS|WebFetch|WebSearch) phase=read ;;
      TodoWrite|ExitPlanMode) phase=plan ;;
      Edit|MultiEdit|Write|NotebookEdit) phase=edit ;;
      Bash)
        cmd=$(field command)
        case $cmd in
          # The agent talking to Apron itself is not a phase.
          *"apron report"*|*"apron run"*|*"apron check"*) exit 0 ;;
          *ctest*|*" test"*|*pytest*|*vitest*|*jest*) phase=test ;;
          *"cmake --build"*|*ninja*|*make*|*"cargo build"*|*"go build"*|*tsc*) phase=build ;;
        esac
        ;;
    esac
    [ -n "$phase" ] || exit 0
    [ "$(cat "$phasefile" 2>/dev/null)" = "$phase" ] && exit 0
    printf '%s\n' "$phase" >"$phasefile" 2>/dev/null
    case $phase in
      read) msg="Claude Code reads the code." ;;
      plan) msg="Claude Code plans the work." ;;
      edit) msg="Claude Code edits files." ;;
      build) msg="Claude Code builds the project." ;;
      test) msg="Claude Code runs the tests." ;;
    esac
    report step "$msg"
    ;;

  Notification)
    [ "$last" = ask ] && exit 0
    kind=$(field notification_type)
    message=$(field message)
    if [ -z "$kind" ]; then
      case $message in
        *permission*) kind=permission_prompt ;;
        *waiting*) kind=idle_prompt ;;
      esac
    fi
    case $kind in
      permission_prompt) report ask "$(sentence "Claude Code waits for permission: $message")" ;;
      idle_prompt) report ask "Claude Code waits for the person's input." ;;
    esac
    ;;

  UserPromptSubmit)
    # The person answers: back to work, and the task goes from [?] to [~].
    [ "$last" = ask ] && report step "The person answers and Claude Code resumes the work."
    ;;

  Stop)
    case $last in ask|blocked|run.finished) exit 0 ;; esac
    if flag stop_hook_active; then
      # Claude stopped again without closing the run: it is waiting for the person.
      report ask "Claude Code stops with the run open and waits for the person."
      exit 0
    fi
    reason="The Apron run $run is still open. If the task is done, run \`$apron check\` and then \`$apron run finish --result done \\\"<what was done; what was left out; what to look at first>\\\"\`. If you need a decision from the person, run \`$apron report ask \\\"<the question>\\\"\`. If something outside stops you, run \`$apron report blocked --cause <gpu|budget|lock|machine|other> \\\"<the reason>\\\"\`."
    printf '{"decision":"block","reason":"%s"}\n' "$reason"
    ;;

  SessionEnd)
    case $last in run.finished|blocked) exit 0 ;; esac
    report blocked --cause other "The Claude Code session ends with the run still open."
    ;;
esac
exit 0
