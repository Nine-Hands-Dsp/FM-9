#!/bin/bash
# FM-9 - essaie de compiler la version web (emscripten) des firmwares qui n'en publient pas.
# Ligne : depot branche script
set +e
OUT="$PWD/web_out"; mkdir -p "$OUT"
while read -r repo br script; do
  [ -z "$repo" ] && continue
  n=$(echo "$repo" | tr '/' '_')
  d="/tmp/src_$n"; rm -rf "$d"
  git clone -q --depth 1 --recurse-submodules -b "$br" "https://github.com/$repo" "$d" || { echo "$repo: clone fail" >> "$OUT/log.txt"; continue; }
  (cd "$d/$(dirname "$script")" && timeout 900 bash "$(basename "$script")" > "$OUT/$n.build.log" 2>&1)
  rc=$?
  w=$(find "$d" -name "*.wasm" -newer "$d/.git/HEAD" -size +20k 2>/dev/null | head -1)
  [ -z "$w" ] && w=$(find "$d" -name "*.wasm" -size +20k 2>/dev/null | head -1)
  if [ -n "$w" ]; then cp "$w" "$OUT/$n.wasm"; echo "$repo: rc=$rc wasm=$w" >> "$OUT/log.txt"; else echo "$repo: rc=$rc no wasm" >> "$OUT/log.txt"; fi
done < tools/web_sources.txt
