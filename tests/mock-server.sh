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

read-buffer)
  # Check if the caller requested a structured JSON representation
  is_json=0
  for arg in "$@"; do
    if [ "$arg" = "--json" ]; then
      is_json=1
    fi
  done

  if [ "$is_json" = "1" ]; then
    # Return a fully valid JSON block simulating a float64 buffer of 3 items
    echo "{"
    echo "  \"ok\": true,"
    echo "  \"buffer_id\": \"$2\","
    echo "  \"element_count\": 3,"
    echo "  \"data_type\": \"float64\","
    echo "  \"data\": [1.0, 2.5, 3.14159]"
    echo "}"
  else
    # Fallback to standard human-readable format if evaluated raw
    echo "Released buffer: $2"
  fi
  echo "ERROR: unexpected no json" >&2
  exit 0
  ;;

release-buffer)
  echo "Released buffer: $2" >&2
  exit 0
  ;;
esac

# --------------------------------
# Default failure
# --------------------------------

echo "error: unknown command (stdout)"
echo "error: unknown command (stderr)" >&2
exit 1
