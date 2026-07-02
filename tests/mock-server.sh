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

inst)
  if [ "$2" = "start" ]; then
    echo "Instrument started successfully"
    echo "debug: start ok" >&2
    exit 0
  elif [ "$2" = "stop" ]; then
    echo "Stopped instrument: $3"
    echo "debug: stop ok" >&2
    exit 0
  elif [ "$2" = "status" ]; then
    echo "instrument OK: $3"
    echo "debug: status ok" >&2
    exit 0
  fi
  ;;

measure)
  # Dynamic fallback lookup checking for structural --json requirements
  is_json=0
  for arg in "$@"; do
    if [ "$arg" = "--json" ]; then
      is_json=1
    fi
  done

  if [ "$is_json" = "1" ]; then
    echo "{"
    echo "  \"ok\": true,"
    echo "  \"message\": [\"Measurement complete\"],"
    echo "  \"output\": ["
    echo "    {"
    echo "      \"jobId\": 1"
    echo "    },"
    echo "    {"
    echo "      \"status\": \"JOB_STATUS_COMPLETED\","
    echo "      \"script\": \"iv_curve.lua\","
    echo "      \"results\": ["
    echo "        {"
    echo "          \"index\": 0,"
    echo "          \"instrumentName\": \"MockInstrument1:1\","
    echo "          \"verb\": \"SET\","
    echo "          \"executedAt\": \"2026-07-01T20:59:00.000Z\","
    echo "          \"param\": ["
    echo "            {"
    echo "              \"name\": \"return\","
    echo "              \"type\": \"LUA_TYPES_BOOL\","
    echo "              \"value\": {"
    echo "                \"b\": true"
    echo "              }"
    echo "            }"
    echo "          ]"
    echo "        },"
    echo "        {"
    echo "          \"index\": 4,"
    echo "          \"instrumentName\": \"Scope1\","
    echo "          \"verb\": \"CAPTURE\","
    echo "          \"executedAt\": \"2026-07-01T20:59:01.000Z\","
    echo "          \"param\": ["
    echo "            {"
    echo "              \"name\": \"return\","
    echo "              \"type\": \"LUA_TYPES_DATA_BUFFER\","
    echo "              \"value\": {"
    echo "                \"s\": \"buf_abc123\""
    echo "              },"
    echo "              \"dbmeta\": {"
    echo "                \"elementCount\": 10000,"
    echo "                \"dataType\": 1"
    echo "              }"
    echo "            }"
    echo "          ]"
    echo "        }"
    echo "      ]"
    echo "    }"
    echo "  ]"
    echo "}"
  else
    # Simple unformatted fallback legacy tracker output format
    echo "{\"result\": 42}"
  fi
  echo "debug: measurement ok" >&2
  exit 0
  ;;

buffer)
  if [ "$2" = "read" ]; then
    is_json=0
    for arg in "$@"; do
      if [ "$arg" = "--json" ]; then
        is_json=1
      fi
    done

    if [ "$is_json" = "1" ]; then
      echo "{"
      echo "  \"ok\": true,"
      echo "  \"output\": ["
      echo "    {"
      echo "      \"buffer_id\": \"$3\","
      echo "      \"element_count\": 3,"
      echo "      \"data_type\": \"float64\","
      echo "      \"data\": [1.0, 2.5, 3.14159]"
      echo "    }"
      echo "  ]"
      echo "}"
    else
      echo "Released buffer: $3"
    fi
    exit 0
  elif [ "$2" = "release" ]; then
    echo "Released buffer: $3" >&2
    exit 0
  fi
  ;;
esac

# --------------------------------
# Default failure
# --------------------------------

echo "error: unknown command (stdout)"
echo "error: unknown command (stderr)" >&2
exit 1
