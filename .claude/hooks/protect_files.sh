#!/usr/bin/env bash
# PreToolUse hook: blocks Claude from editing human-owned files.
# Exit code 2 blocks the edit and shows the message to Claude.
# A human can bypass deliberately by starting Claude Code with FANCEM_ALLOW_PROTECTED=1.
set -euo pipefail

input="$(cat)"
file="$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty')"
[ -z "$file" ] && exit 0
[ "${FANCEM_ALLOW_PROTECTED:-0}" = "1" ] && exit 0

rel="${file#"${CLAUDE_PROJECT_DIR:-}"/}"

case "$rel" in
  docs/requirements.md | tests/hand_calcs/verified/* | .claude/settings.json | .claude/hooks/*)
    echo "BLOCKED: '$rel' is human-owned. Describe the change you propose in your reply; a human will apply it." >&2
    exit 2
    ;;
esac
exit 0
