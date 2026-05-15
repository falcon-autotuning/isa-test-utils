#!/bin/sh

cmd="$1"

# --------------------------------
# Failure injection mechanisms
# --------------------------------

# Fail everything if env var set
if [ "$MOCK_FAIL_ALL" = "1" ]; then
  echo "forced global failure (stdout)"
  echo "forced global failure (stderr)" >&2
  exit 1
fi

# Fail specific command if --fail passed
for arg in "$@"; do
  if [ "$arg" = "--fail" ]; then
    echo "command failed intentionally (stdout)"
    echo "command failed intentionally (stderr)" >&2
    exit 1
  fi
done

# --------------------------------
# Normal behavior
# --------------------------------

case "$cmd" in
daemon)
  if [ "$2" = "start" ]; then
    echo "server started"
    echo "debug: daemon start ok" >&2
    exit 0
  elif [ "$2" = "stop" ]; then
    echo "server stopped"
    echo "debug: daemon stop ok" >&2
    exit 0
  fi
  ;;

start)
  echo "instrument started: $2"
  echo "debug: start ok" >&2
  exit 0
  ;;

stop)
  echo "instrument stopped: $2"
  echo "debug: stop ok" >&2
  exit 0
  ;;

instrument)
  if [ "$2" = "status" ]; then
    echo "instrument OK: $3"
    echo "debug: status ok" >&2
    exit 0
  fi
  ;;

measure)
  echo "{\"result\": 42}"
  echo "debug: measurement ok" >&2
  exit 0
  ;;
esac

# --------------------------------
# Default failure
# --------------------------------

echo "error: unknown command (stdout)"
echo "error: unknown command (stderr)" >&2
exit 1
