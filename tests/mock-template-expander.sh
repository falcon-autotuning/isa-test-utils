#!/bin/sh

in="$1"
out="$2"

if [ "$3" = "--fail" ]; then
  echo "expander fail stdout"
  echo "expander fail stderr" >&2
  exit 1
fi

# Simulate expansion: just copy
cat "$in" >"$out"

echo "expanded $in -> $out"
echo "debug: expander ok" >&2

exit 0
