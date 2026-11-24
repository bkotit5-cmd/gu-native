#!/usr/bin/env bash
# Fetch the exact public source revisions used by this Android build.
# Sources stay under deps/src/, which is ignored by git.
set -euo pipefail

ROOT="$(cd "$(dirname "$BASH_SOURCE")/.." && pwd)"
SRC="$ROOT/deps/src"
mkdir -p "$SRC"

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

fetch_pinned() {
    local name="$1"
    local url="$2"
    local tag="$3"
    local expected="$4"
    local destination="$5"
    local submodules="$6"
    local actual origin
    [[ -n "$submodules" ]] || submodules="false"

    if [[ -e "$destination" ]]; then
        git -C "$destination" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
            || die "$name exists at $destination but is not a Git checkout; move it aside or remove it yourself."
        origin="$(git -C "$destination" remote get-url origin 2>/dev/null || true)"
        [[ "$origin" == "$url" ]] \
            || die "$name has unexpected origin '$origin' (expected '$url'); refusing to replace it."
        actual="$(git -C "$destination" rev-parse HEAD)"
        [[ "$actual" == "$expected" ]] \
            || die "$name is at $actual, but this build requires $expected. No existing files were changed."
        git -C "$destination" diff --quiet \
            && git -C "$destination" diff --cached --quiet \
            || die "$name has local tracked changes; commit or back them up before fetching."
    else
        git clone --depth 1 --branch "$tag" "$url" "$destination"
        actual="$(git -C "$destination" rev-parse HEAD)"
        [[ "$actual" == "$expected" ]] \
            || die "$name tag $tag resolved to $actual, not the pinned revision $expected."
    fi

    if [[ "$submodules" == "true" ]]; then
        git -C "$destination" submodule sync --recursive
        git -C "$destination" submodule update --init --recursive --depth 1
    fi

    printf '%s: %s (%s)\n' "$name" "$tag" "$actual"
}

fetch_pinned \
    "SDL3" \
    "https://github.com/libsdl-org/SDL.git" \
    "release-3.4.18" \
    "829a65d769d935c4852f8159e964312c0957260a" \
    "$SRC/SDL3-3.4.18" \
    ""

fetch_pinned \
    "FFmpeg" \
    "https://github.com/FFmpeg/FFmpeg.git" \
    "n9.0.2" \
    "946fcce07b6dcd0331c8cc609192aeff5e1924f8" \
    "$SRC/ffmpeg" \
    ""

fetch_exact() {
    local name="$1"
    local url="$2"
    local expected="$3"
    local destination="$4"
    local actual origin

    mkdir -p "$(dirname "$destination")"
    if [[ -e "$destination" ]]; then
        git -C "$destination" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
            || die "$name exists at $destination but is not a Git checkout; move it aside or remove it yourself."
        origin="$(git -C "$destination" remote get-url origin 2>/dev/null || true)"
        [[ "$origin" == "$url" ]] \
            || die "$name has unexpected origin '$origin' (expected '$url'); refusing to replace it."
        actual="$(git -C "$destination" rev-parse HEAD)"
        [[ "$actual" == "$expected" ]] \
            || die "$name is at $actual, but this build requires $expected. No existing files were changed."
        git -C "$destination" diff --quiet \
            && git -C "$destination" diff --cached --quiet \
            || die "$name has local tracked changes; commit or back them up before fetching."
    else
        git init "$destination"
        git -C "$destination" remote add origin "$url"
        git -C "$destination" fetch --depth 1 origin "$expected"
        git -C "$destination" checkout --detach FETCH_HEAD
        actual="$(git -C "$destination" rev-parse HEAD)"
        [[ "$actual" == "$expected" ]] \
            || die "$name resolved to $actual, not the pinned revision $expected."
    fi

    printf '%s: %s (%s)\n' "$name" "$expected" "$actual"
}

fetch_pinned \
    "shaderc" \
    "https://github.com/google/shaderc.git" \
    "v2026.4" \
    "b64fd42c4a2a9d501c47aca5d3571f7673a5e9cc" \
    "$SRC/shaderc" \
    ""

# shaderc tracks these dependencies in DEPS, not .gitmodules.
fetch_exact \
    "SPIRV-Headers" \
    "https://github.com/KhronosGroup/SPIRV-Headers.git" \
    "04fd3caa1e8267e4d95c806cad901181728e1006" \
    "$SRC/shaderc/third_party/spirv-headers"

fetch_exact \
    "SPIRV-Tools" \
    "https://github.com/KhronosGroup/SPIRV-Tools.git" \
    "ef96ed763b43b59b33b31b362f09a02b729fa1c9" \
    "$SRC/shaderc/third_party/spirv-tools"

fetch_exact \
    "glslang" \
    "https://github.com/KhronosGroup/glslang.git" \
    "e1b562a8bed273a02f30b59b66a5d499793cede5" \
    "$SRC/shaderc/third_party/glslang"

# Bloodframe: native Android Lossless Scaling FG (MIT), pinned rather than tracking its moving default branch.
fetch_exact \
    "Bloodframe LSFG Android" \
    "https://github.com/ducvd89/bloodframe.git" \
    "f4ac713aef58d9403a136e179b3b0d163d85eb66" \
    "$SRC/bloodframe"
git -C "$SRC/bloodframe" submodule sync --recursive
git -C "$SRC/bloodframe" submodule update --init --depth 1 -- thirdparty/pe-parse thirdparty/volk

printf '\nAll dependency sources are present at the pinned revisions.\n'
