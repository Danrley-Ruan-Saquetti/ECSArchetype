#!/usr/bin/env bash
# PostToolUse (Edit|Write): formats the edited .cpp/.hpp with the dev container's clang-format.
# There is no jq on the host, so file_path is extracted from the input JSON with sed.
set -euo pipefail

input=$(cat)
file=$(printf '%s' "$input" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
# The JSON escapes the backslashes in Windows paths.
file=${file//\\\\/\\}

case "$file" in
  *.cpp | *.hpp) ;;
  *) exit 0 ;;
esac

project=$(cygpath -m "$CLAUDE_PROJECT_DIR" 2>/dev/null || printf '%s' "$CLAUDE_PROJECT_DIR")
absolute=$(cygpath -m "$file" 2>/dev/null || printf '%s' "$file")
relative=${absolute#"$project"/}
# Outside the project: the container only sees what is mounted at /app.
if [ "$relative" = "$absolute" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"
if ! output=$(docker compose run --rm -T dev clang-format -i "$relative" 2>&1); then
  printf '%s\n' "$output" | grep -v '^ Container ' >&2
  exit 1
fi
