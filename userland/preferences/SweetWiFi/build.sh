#!/bin/sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
THEOS_SDK="/home/lopro/theos/sdks/iPhoneOS14.5.sdk"
CLANG="/home/lopro/theos/toolchain/linux/iphone/bin/clang"
LDID="/home/lopro/theos/toolchain/linux/iphone/bin/ldid"

echo "Building SweetWiFi preference bundle..."
mkdir -p "$DIR/build/SweetWiFi.bundle"

"$CLANG" -target arm64-apple-ios14.0 \
	-isysroot "$THEOS_SDK" \
	-shared \
	-fobjc-arc \
	-O2 \
	-I/home/lopro/theos/vendor/include \
	-F"$THEOS_SDK/System/Library/Frameworks" \
	-F"$THEOS_SDK/System/Library/PrivateFrameworks" \
	-framework Foundation \
	-framework UIKit \
	-framework CoreGraphics \
	-framework Preferences \
	-o "$DIR/build/SweetWiFi.bundle/SweetWiFi" \
	"$DIR/SweetWiFiRootListController.m"

"$LDID" -S "$DIR/build/SweetWiFi.bundle/SweetWiFi"
cp "$DIR/Resources/Info.plist" "$DIR/build/SweetWiFi.bundle/"

echo "SweetWiFi.bundle built successfully at $DIR/build/SweetWiFi.bundle"
