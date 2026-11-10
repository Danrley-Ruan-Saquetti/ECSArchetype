#!/usr/bin/env bash
# PostToolUse (Edit|Write): formata o .cpp/.hpp editado com o clang-format do container dev.
# Não há jq no host, então o file_path sai do JSON da entrada via sed.
set -euo pipefail

input=$(cat)
file=$(printf '%s' "$input" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
# O JSON escapa as barras invertidas dos caminhos Windows.
file=${file//\\\\/\\}

case "$file" in
  *.cpp | *.hpp) ;;
  *) exit 0 ;;
esac

project=$(cygpath -m "$CLAUDE_PROJECT_DIR" 2>/dev/null || printf '%s' "$CLAUDE_PROJECT_DIR")
absolute=$(cygpath -m "$file" 2>/dev/null || printf '%s' "$file")
relative=${absolute#"$project"/}
# Fora do projeto: o container só enxerga o que está montado em /app.
if [ "$relative" = "$absolute" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"
if ! output=$(docker compose run --rm -T dev clang-format -i "$relative" 2>&1); then
  printf '%s\n' "$output" | grep -v '^ Container ' >&2
  exit 1
fi
