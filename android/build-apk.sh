#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f assets/default.xbe ]]; then
    echo "Missing assets/default.xbe. Add the legally owned Sims 2 Xbox USA XBE before building." >&2
    exit 1
fi

for tool in cmake ninja java gradle; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing build tool: $tool" >&2
        exit 1
    }
done

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to your Android NDK installation}"
: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK installation}"

cmake --preset host-tools
cmake --build --preset host-tools --target xbe_recomp
# Gradle configures CMake with the SDL AAR's Prefab package paths and builds
# the Android shared library and APK together.
gradle -p android --no-daemon assembleRelease

echo "APK: android/app/build/outputs/apk/release/app-release.apk"
