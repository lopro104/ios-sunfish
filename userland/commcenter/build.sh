#!/bin/sh
# Build SweetNoBaseband.dylib (ElleKit tweak for CommCenter) with the Theos toolchain into out/.
set -e
cd "$(dirname "$0")"
T=${THEOS:-$HOME/theos}
mkdir -p out
"$T/toolchain/linux/iphone/bin/clang" -target arm64-apple-ios16.0 -isysroot "$T/sdks/iPhoneOS16.5.sdk" -O2 -Wall \
	-dynamiclib -o out/SweetNoBaseband.dylib SweetNoBaseband.c
"$T/toolchain/linux/iphone/bin/ldid" -S out/SweetNoBaseband.dylib
