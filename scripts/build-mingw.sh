#!/usr/bin/env bash
# Cross-compiles KayzeLauncher.exe with mingw-w64 (used by the release workflow).
set -euo pipefail
# Resolve the output dir against the caller's cwd before changing directory.
OUT_DIR="${1:-$(dirname "$0")/../build}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
cd "$(dirname "$0")/../KayzeLauncher"

# Resource.rc is saved by Visual Studio as UTF-16, windres wants UTF-8.
iconv -f UTF-16LE -t UTF-8 Resource.rc | sed '1s/^\xEF\xBB\xBF//' > "$OUT_DIR/Resource.utf8.rc"
x86_64-w64-mingw32-windres -I . "$OUT_DIR/Resource.utf8.rc" -O coff -o "$OUT_DIR/resource.o"

# qrcodegen.c is C; compile it separately so g++ doesn't treat it as C++.
x86_64-w64-mingw32-gcc -std=c99 -O2 -c qrcodegen.c -o "$OUT_DIR/qrcodegen.o"

x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -static -mwindows \
    main.cpp remote_auth.cpp imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp \
    imgui_impl_win32.cpp imgui_impl_dx11.cpp "$OUT_DIR/qrcodegen.o" "$OUT_DIR/resource.o" \
    -o "$OUT_DIR/KayzeLauncher.exe" \
    -ld3d11 -ld3dcompiler_47 -ldwmapi -lurlmon -lwininet -lcrypt32 -lwinhttp -lbcrypt -lgdi32 -limm32 -ldxgi

echo "Built $OUT_DIR/KayzeLauncher.exe"
