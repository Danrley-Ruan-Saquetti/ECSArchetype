#!/usr/bin/env bash
# Extrai o corpo de uma função desmontada (nome demanglado casando com a regex) para um arquivo de texto.
# Uso: tools/extract_codegen.sh <binário> <regex-da-função> <saída>   (rodar dentro do container)
# .wasm usa llvm-objdump do emsdk; qualquer outro usa objdump (sintaxe Intel). A regex é ERE do awk.
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "uso: $0 <binário> <regex-da-função> <saída>" >&2
  exit 1
fi

binary=$1
pattern=$2
output=$3

if [ ! -f "$binary" ]; then
  echo "binário não encontrado: $binary" >&2
  exit 1
fi

disassembly=$(mktemp)
body=$(mktemp)
trap 'rm -f "$disassembly" "$body"' EXIT

case $binary in
  *.wasm) /emsdk/upstream/bin/llvm-objdump -d --demangle --no-show-raw-insn "$binary" > "$disassembly" ;;
  *) objdump -d --no-show-raw-insn -M intel -C "$binary" > "$disassembly" ;;
esac

list_functions='
  /^[0-9a-f]+ <.*>:$/ {
    name = $0
    sub(/^[0-9a-f]+ </, "", name)
    sub(/>:$/, "", name)
    if (name ~ ENVIRON["PATTERN"] && !(name in seen)) {
      seen[name] = 1
      print name
    }
  }'

print_body='
  /^[0-9a-f]+ <.*>:$/ {
    name = $0
    sub(/^[0-9a-f]+ </, "", name)
    sub(/>:$/, "", name)
    printing = (name == ENVIRON["TARGET"])
  }
  printing { print }'

matches=$(PATTERN=$pattern awk "$list_functions" "$disassembly")

if [ -z "$matches" ]; then
  echo "função não encontrada: $pattern em $binary" >&2
  exit 1
fi

if [ "$(printf '%s\n' "$matches" | wc -l)" -gt 1 ]; then
  echo "regex ambígua: $pattern casa com:" >&2
  printf '%s\n' "$matches" >&2
  exit 1
fi

TARGET=$matches awk "$print_body" "$disassembly" > "$body"

if [ ! -s "$body" ]; then
  echo "corpo vazio para: $matches" >&2
  exit 1
fi

mkdir -p "$(dirname "$output")"
cp "$body" "$output"
