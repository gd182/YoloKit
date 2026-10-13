#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AAR="${1:-$ROOT/yolokit/build/outputs/aar/yolokit-release.aar}"
NDK="${2:-${ANDROID_NDK_HOME:-}}"

if [[ ! -f "$AAR" || -z "$NDK" ]]; then
    echo "Usage: bash scripts/check_release_aar.sh [release.aar] /path/to/android-ndk" >&2
    exit 1
fi

LLVM_BIN=""
for candidate in "$NDK"/toolchains/llvm/prebuilt/*/bin; do
    if [[ -x "$candidate/llvm-readelf" && -x "$candidate/llvm-strings" ]]; then
        LLVM_BIN="$candidate"
        break
    fi
done
if [[ -z "$LLVM_BIN" ]]; then
    echo "NDK LLVM tools not found: $NDK" >&2
    exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/yolokit-aar-check.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
unzip -q "$AAR" 'jni/*/libyolokit.so' classes.jar -d "$TMP"

EXPECTED_EXPORTS="$(printf 'JNI_OnLoad\nJNI_OnUnload\n')"
for abi in arm64-v8a armeabi-v7a x86_64; do
    library="$TMP/jni/$abi/libyolokit.so"
    [[ -f "$library" ]] || { echo "Missing ABI: $abi" >&2; exit 1; }
    exports="$("$LLVM_BIN/llvm-readelf" --dyn-syms --wide "$library" |
        awk '($5 == "GLOBAL" || $5 == "WEAK") && $6 == "DEFAULT" && $7 != "UND" { print $8 }' |
        LC_ALL=C sort)"
    if [[ "$exports" != "$EXPECTED_EXPORTS" ]]; then
        echo "Unexpected exports for $abi:" >&2
        printf '%s\n' "$exports" >&2
        exit 1
    fi
    "$LLVM_BIN/llvm-readelf" --sections --wide "$library" > "$TMP/sections"
    if grep -Eq '[.]debug_|[.]symtab|[.]gnu_debugdata' "$TMP/sections"; then
        echo "Debug symbols remain in $abi" >&2
        exit 1
    fi
    "$LLVM_BIN/llvm-strings" "$library" > "$TMP/strings"
    if grep -Eq 'first input RGB:|first output:|output channel %d:|best anchor %d|avg over 90 frames:|model loaded, gpu=' "$TMP/strings"; then
        echo "Diagnostic strings remain in $abi" >&2
        exit 1
    fi
    "$LLVM_BIN/llvm-readelf" --program-headers --wide "$library" > "$TMP/segments"
    grep -q GNU_RELRO "$TMP/segments" || { echo "Missing RELRO: $abi" >&2; exit 1; }
    "$LLVM_BIN/llvm-readelf" --dynamic --wide "$library" > "$TMP/dynamic"
    grep -q BIND_NOW "$TMP/dynamic" || { echo "Missing BIND_NOW: $abi" >&2; exit 1; }
    echo "$abi: exports, stripped symbols, diagnostics, and linker flags OK"
done

unzip -Z1 "$TMP/classes.jar" > "$TMP/classes"
for type in Backend Detection ModelConfig 'ModelConfig$Builder' YoloKit 'YoloKit$Companion'; do
    grep -Fxq "io/github/gd182/yolokit/$type.class" "$TMP/classes" ||
        { echo "Missing public API class: $type" >&2; exit 1; }
done

check_jni_class() {
    local type="$1"
    local expected="$2"
    javap -s -p -classpath "$TMP/classes.jar" "io.github.gd182.yolokit.internal.$type" |
        awk '/ native / {
            name = $0
            sub(/[(].*/, "", name)
            sub(/.* /, "", name)
            next
        }
        /descriptor:/ && name != "" { print name " " $2; name = "" }' |
        LC_ALL=C sort > "$TMP/signatures"
    printf '%s\n' "$expected" | LC_ALL=C sort > "$TMP/expected"
    if ! diff -u "$TMP/expected" "$TMP/signatures"; then
        echo "JNI contract changed: $type" >&2
        exit 1
    fi
}

check_jni_class NcnnNative 'nativeHasGpu ()Z
nativeInitAsset (Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZZZ)Z
nativeInitBytes ([B[BLjava/lang/String;Ljava/lang/String;IIZZZ)Z
nativeDetect (Landroid/graphics/Bitmap;FF)[F
nativeRelease ()V
nativeReleaseGpu ()V
nativePrecision ()Ljava/lang/String;'

check_jni_class QnnNative 'nativeIsBuilt ()Z
nativeInitAsset (Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/lang/String;IZ)Ljava/lang/String;
nativeInitBytes ([BLjava/lang/String;IZ)Ljava/lang/String;
nativeDescribe ()Ljava/lang/String;
nativeRelease ()V
nativeDetect (Landroid/graphics/Bitmap;FF)[F'

echo "Public API classes and all 13 JNI method signatures OK"
