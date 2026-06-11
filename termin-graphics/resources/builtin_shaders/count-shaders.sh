#!/usr/bin/env bash
set -euo pipefail

DIR="${1:-.}"

glsl=$(find "$DIR" -type f -name '*.glsl' | wc -l)
slang=$(find "$DIR" -type f -name '*.slang' | wc -l)
total=$((glsl + slang))

echo "GLSL : $glsl"
echo "Slang: $slang"
echo "Total: $total"
echo "Remaining (GLSL / total): $(awk "BEGIN{printf \"%.0f\", ($glsl/$total)*100}")%"
