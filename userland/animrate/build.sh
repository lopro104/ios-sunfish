#!/bin/sh
# Build SweetAnimRate.dylib into out/.
set -e
cd "$(dirname "$0")"
T=${THEOS:-$HOME/theos}
mkdir -p out
clang -target arm64-apple-ios16.0 -isysroot "$T/sdks/iPhoneOS16.5.sdk" --ld-path="$T/toolchain/linux/iphone/bin/ld" -Wno-unused-command-line-argument \
	-O2 -Wall -fobjc-arc -dynamiclib -o out/SweetAnimRate.dylib SweetAnimRate.m -framework Foundation -framework QuartzCore -lobjc
"$T/toolchain/linux/iphone/bin/ldid" -S out/SweetAnimRate.dylib
