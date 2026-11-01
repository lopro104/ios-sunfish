#!/bin/sh
# Build userland/tools for iOS (arm64) using the Theos toolchain.
set -e
cd "$(dirname "$0")"

T=${THEOS:-$HOME/theos}
CC="$T/toolchain/linux/iphone/bin/clang -target arm64-apple-ios16.0 -isysroot $T/sdks/iPhoneOS16.5.sdk -O2 -Wall"
LDID="$T/toolchain/linux/iphone/bin/ldid"

echo "Building sweet-bootctl..."
$CC -o sweet-bootctl sweet-bootctl.c
$LDID -S sweet-bootctl

echo "Building sweet-growfs..."
$CC -o sweet-growfs sweet-growfs.c
$LDID -S sweet-growfs

echo "Done."
