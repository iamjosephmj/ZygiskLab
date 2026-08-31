#!/usr/bin/env bash
# Builds the module and packages a flashable zip into out/.
set -euo pipefail
cd "$(dirname "$0")"

NAME="$(basename "$PWD")"
NDK_BUILD="${ANDROID_NDK_HOME:+$ANDROID_NDK_HOME/ndk-build}"
NDK_BUILD="${NDK_BUILD:-$(command -v ndk-build || true)}"
[ -x "$NDK_BUILD" ] || { echo "ndk-build not found; set ANDROID_NDK_HOME" >&2; exit 1; }

rm -rf out obj libs
"$NDK_BUILD" -j"$(nproc)"

mkdir -p "out/pkg/zygisk"
cp module.prop "out/pkg/module.prop"
cp libs/arm64-v8a/libzygisklab_companion.so "out/pkg/zygisk/arm64-v8a.so"

(cd out/pkg && zip -qr "../$NAME.zip" .)
echo "built out/$NAME.zip"
