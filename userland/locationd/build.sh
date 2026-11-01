#!/bin/sh
# Build SweetLocationd.dylib (ElleKit tweak for locationd) with the Theos toolchain into out/.
set -e
cd "$(dirname "$0")"
T=${THEOS:-$HOME/theos}
mkdir -p out
"$T/toolchain/linux/iphone/bin/clang" -target arm64-apple-ios16.0 -isysroot "$T/sdks/iPhoneOS16.5.sdk" -O2 -Wall \
	-dynamiclib -o out/SweetLocationd.dylib SweetLocationd.c -lobjc
"$T/toolchain/linux/iphone/bin/ldid" -S out/SweetLocationd.dylib
