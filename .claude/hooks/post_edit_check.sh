#!/usr/bin/env bash
# PostToolUse hook: formats edited files and reports lint errors back to Claude.
# Exit code 2 sends stderr to Claude so it fixes the problem.
set -uo pipefail

input="$(cat)"
file="$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty')"
if [ -z "$file" ] || [ ! -f "$file" ]; then exit 0; fi

case "$file" in
  *.cpp | *.hpp | *.h | *.cc)
    if command -v clang-format >/dev/null 2>&1; then
      clang-format -i "$file"
    fi
    ;;
  *.py)
    if command -v uv >/dev/null 2>&1; then
      uv run --quiet ruff format "$file" >/dev/null 2>&1 || true
      if ! out="$(uv run --quiet ruff check "$file" 2>&1)"; then
        echo "ruff reported problems in $file:" >&2
        echo "$out" >&2
        exit 2
      fi
    fi
    ;;
esac
exit 0
