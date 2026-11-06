#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/ios}"
SDK="${SDK:-iphoneos}" # iphoneos (physical device / IPA) or iphonesimulator
CONFIGURATION="${CONFIGURATION:-Release}"

echo "=== Building PharaohPad for iOS ($SDK - $CONFIGURATION) ==="

for tool in cmake xcodebuild; do
    if ! command -v "$tool" >/dev/null; then
        echo "Error: Required tool '$tool' not found." >&2
        exit 1
    fi
done

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_OSX_SYSROOT="$SDK"
    -DTARGET_PLATFORM=ios
    -G Xcode
)

if [[ "$SDK" == "iphoneos" ]]; then
    CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES=arm64)
fi

cmake "${CMAKE_ARGS[@]}"

XCODE_ARGS=(
    -project "$BUILD_DIR/akhenaten.xcodeproj"
    -scheme akhenaten
    -configuration "$CONFIGURATION"
    -sdk "$SDK"
    COMPILER_INDEX_STORE_ENABLE=NO
)

if [[ "$SDK" == "iphoneos" ]]; then
    XCODE_ARGS+=(
        -destination "generic/platform=iOS"
    )
    if [[ -n "${DEVELOPMENT_TEAM:-}" ]]; then
        XCODE_ARGS+=(
            DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM"
            CODE_SIGN_STYLE=Automatic
        )
        echo "Code signing enabled with Development Team: $DEVELOPMENT_TEAM"
    else
        XCODE_ARGS+=(
            CODE_SIGN_IDENTITY=""
            CODE_SIGNING_REQUIRED=NO
            CODE_SIGNING_ALLOWED=NO
        )
        echo "Building unsigned binary for sideloading / packaging"
    fi
else
    XCODE_ARGS+=(
        -destination "platform=iOS Simulator,name=iPad Pro 11-inch (M4)"
        CODE_SIGN_IDENTITY=""
        CODE_SIGNING_REQUIRED=NO
        CODE_SIGNING_ALLOWED=NO
    )
fi

xcodebuild "${XCODE_ARGS[@]}" build

APP_PATH="$BUILD_DIR/$CONFIGURATION-$SDK/PharaohPad.app"
if [[ ! -d "$APP_PATH" ]]; then
    APP_PATH="$BUILD_DIR/$CONFIGURATION-$SDK/akhenaten.app"
fi

if [[ ! -d "$APP_PATH" ]]; then
    echo "Error: App bundle not found at $APP_PATH" >&2
    exit 1
fi

echo "Successfully built: $APP_PATH"
