#!/usr/bin/env bash
# Extracts the body of a disassembled function (demangled name matching the regex) to a text file.
# Usage: tools/extract_codegen.sh <binary> <function-regex> <output>   (run inside the container)
# .wasm uses the emsdk llvm-objdump; anything else uses objdump (Intel syntax). The regex is awk ERE.
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <binary> <function-regex> <output>" >&2
  exit 1
fi

binary=$1
pattern=$2
output=$3

if [ ! -f "$binary" ]; then
  echo "binary not found: $binary" >&2
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
  echo "function not found: $pattern in $binary" >&2
  exit 1
fi

if [ "$(printf '%s\n' "$matches" | wc -l)" -gt 1 ]; then
  echo "ambiguous regex: $pattern matches:" >&2
  printf '%s\n' "$matches" >&2
  exit 1
fi

TARGET=$matches awk "$print_body" "$disassembly" > "$body"

if [ ! -s "$body" ]; then
  echo "empty body for: $matches" >&2
  exit 1
fi

mkdir -p "$(dirname "$output")"
cp "$body" "$output"
