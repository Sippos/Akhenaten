#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/ios}"
RELEASE_DIR="${RELEASE_DIR:-$ROOT_DIR/build/release}"
VERSION="${VERSION:-0.3.0}"
BUILD_NUMBER="${BUILD_NUMBER:-1}"
OUTPUT_NAME="PharaohPad-${VERSION}-b${BUILD_NUMBER}-unsigned.ipa"
OUTPUT_PATH="$RELEASE_DIR/$OUTPUT_NAME"

APP_PATH="$BUILD_DIR/Release-iphoneos/PharaohPad.app"
if [[ ! -d "$APP_PATH" ]]; then
    APP_PATH="$BUILD_DIR/Release-iphoneos/akhenaten.app"
fi

if [[ ! -d "$APP_PATH" ]]; then
    echo "Error: Device app not found at $APP_PATH. Run scripts/build-ios.sh first." >&2
    exit 1
fi

echo "=== Packaging PharaohPad IPA ==="
mkdir -p "$RELEASE_DIR"

STAGING_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pharaohpad-ipa.XXXXXX")"
trap 'rm -rf "$STAGING_DIR"' EXIT

mkdir -p "$STAGING_DIR/Payload"
cp -R "$APP_PATH" "$STAGING_DIR/Payload/PharaohPad.app"
STAGED_APP="$STAGING_DIR/Payload/PharaohPad.app"

# Strip existing local signatures and provisioning profiles
rm -rf "$STAGED_APP/_CodeSignature"
rm -f "$STAGED_APP/embedded.mobileprovision"

# Ensure engine data files (fonts, custom packs) are bundled
if [[ ! -d "$STAGED_APP/data" && ! -d "$STAGED_APP/Data" ]]; then
    echo "Copying engine data files to app bundle..."
    mkdir -p "$STAGED_APP/data"
    cp -R "$ROOT_DIR/data/"* "$STAGED_APP/data/"
fi

# Copyright safety audit: ensure original game assets are NEVER packaged into public binary
PROHIBITED_FILES="$(find "$STAGED_APP" -type f \( \
    -iname 'pharaoh.eng' -o \
    -iname 'pharaoh.sg2' -o \
    -iname 'cleopatra.eng' -o \
    -iname 'cleopatra.sg2' -o \
    -iname '*.sav' -o \
    -iname '*.bik' \
\) -print)"

if [[ -n "$PROHIBITED_FILES" ]]; then
    echo "ERROR: Copyrighted game data or save files detected in app bundle:" >&2
    echo "$PROHIBITED_FILES" >&2
    exit 1
fi

# Package into .ipa
(
    cd "$STAGING_DIR"
    zip -qry "$OUTPUT_PATH" Payload
)

# Compute sha256 checksum
if command -v sha256sum >/dev/null; then
    sha256sum "$OUTPUT_PATH" > "$OUTPUT_PATH.sha256"
elif command -v shasum >/dev/null; then
    shasum -a 256 "$OUTPUT_PATH" > "$OUTPUT_PATH.sha256"
fi

echo "Successfully generated: $OUTPUT_PATH"
if [[ -f "$OUTPUT_PATH.sha256" ]]; then
    cat "$OUTPUT_PATH.sha256"
fi
