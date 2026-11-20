#!/bin/sh
# Stands in for ssh in tests (APRON_SSH): host "x" is the aprond whose socket
# is $APRON_FAKE_SSH_DIR/x.sock; host "down" fails the way ssh does.
host="" cmd=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) shift ;;
    -*) ;;
    *) if [ -z "$host" ]; then host=$1; else cmd=$1; fi ;;
  esac
  shift
done
if [ "$host" = down ]; then
  echo "ssh: connect to host down port 22: Connection refused" >&2
  exit 255
fi
APRON_SOCKET="$APRON_FAKE_SSH_DIR/$host.sock" XDG_STATE_HOME="$APRON_FAKE_SSH_DIR/$host-state" exec sh -c "$cmd"
