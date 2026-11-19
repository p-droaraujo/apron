#!/bin/sh
# Claude Code hook: hands the event to `apron hook`, which does the work in C++
# with the real JSON parser (see src/cli/cmd_hook.cpp for what each event does).
# Finds the apron binary and never gets in Claude Code's way.
apron=${APRON_BIN:-}
if [ -z "$apron" ]; then
  if [ -n "${CLAUDE_PROJECT_DIR:-}" ] && [ -x "$CLAUDE_PROJECT_DIR/build/apron" ]; then
    apron=$CLAUDE_PROJECT_DIR/build/apron
  else
    apron=$(command -v apron) || exit 0
  fi
fi
exec "$apron" hook 2>/dev/null
