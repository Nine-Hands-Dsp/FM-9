#!/bin/bash
# FM-9 - essaie de compiler la version web (emscripten) des firmwares qui n'en publient pas.
# Ligne : depot branche script
set +e
OUT="$PWD/web_out"; mkdir -p "$OUT"
# SDK JieLi AC79 (attendu par build.sh des derives de Felucca dans ~/fw-AC79_AIoT_SDK)
[ -d "$HOME/fw-AC79_AIoT_SDK" ] || git clone -q --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$HOME/fw-AC79_AIoT_SDK" || echo "SDK clone fail" >> "$OUT/log.txt"
while read -r repo br script; do
  [ -z "$repo" ] && continue
  n=$(echo "$repo" | tr '/' '_')
  d="/tmp/src_$n"; rm -rf "$d"
  git clone -q --depth 1 --recurse-submodules -b "$br" "https://github.com/$repo" "$d" || { echo "$repo: clone fail" >> "$OUT/log.txt"; continue; }
  (
    cd "$d"
    [ -f tools/get_toolchain.sh ] && timeout 900 bash tools/get_toolchain.sh   # chaine JieLi (Felucca et derives)
    # en-tetes generes d'abord (build.sh gen a la racine), puis la cible web
    if [ -f build.sh ]; then timeout 600 bash build.sh gen || timeout 900 bash build.sh; fi
    [ -f firmware/build.sh ] && (cd firmware && (timeout 600 bash build.sh gen || timeout 900 bash build.sh))
    cd "$d/$(dirname "$script")"
    if [ "$(basename "$script")" = Makefile ]; then timeout 900 make web || timeout 900 make wasm || timeout 900 make; else timeout 900 bash "$(basename "$script")"; fi
  ) > "$OUT/$n.build.log" 2>&1
  rc=$?
  for p in $(find "$d" -name "*.fwsc" -newer "$d/.git/HEAD" 2>/dev/null | head -2); do cp "$p" "$OUT/$n.$(basename "$p")"; done
  w=$(find "$d" -name "*.wasm" -newer "$d/.git/HEAD" -size +20k 2>/dev/null | head -1)
  [ -z "$w" ] && w=$(find "$d" -name "*.wasm" -size +20k 2>/dev/null | head -1)
  if [ -n "$w" ]; then cp "$w" "$OUT/$n.wasm"; echo "$repo: rc=$rc wasm=$w" >> "$OUT/log.txt"; else echo "$repo: rc=$rc no wasm" >> "$OUT/log.txt"; fi
done < <(grep -v -e trawler -e rocket -e deal -e rift tools/web_sources.txt)
