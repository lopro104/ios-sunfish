#!/bin/sh
# Build SweetSensors.dylib and sweet-sensor-ctl
set -e
cd "$(dirname "$0")"
T=${THEOS:-$HOME/theos}
mkdir -p out

echo "==> Building SweetSensors.dylib (SpringBoard tweak)..."
clang -target arm64-apple-ios16.0 -isysroot "$T/sdks/iPhoneOS16.5.sdk" --ld-path="$T/toolchain/linux/iphone/bin/ld" -Wno-unused-command-line-argument \
	-O2 -Wall -fobjc-arc -dynamiclib -o out/SweetSensors.dylib SweetSensors.m \
	-framework Foundation -framework QuartzCore -lobjc
"$T/toolchain/linux/iphone/bin/ldid" -S out/SweetSensors.dylib

echo "==> Building sweet-sensor-ctl (CLI tool)..."
clang -target arm64-apple-ios16.0 -isysroot "$T/sdks/iPhoneOS16.5.sdk" --ld-path="$T/toolchain/linux/iphone/bin/ld" -Wno-unused-command-line-argument \
	-O2 -Wall -fobjc-arc -o out/sweet-sensor-ctl sweet-sensor-ctl.m \
	-framework Foundation -lobjc -F "$T/sdks/iPhoneOS16.5.sdk/System/Library/PrivateFrameworks" -framework BackBoardServices
"$T/toolchain/linux/iphone/bin/ldid" -S out/sweet-sensor-ctl

echo "Done! Artifacts in out/."
