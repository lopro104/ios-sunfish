#!/bin/sh
# SweetTorch.dylib (Control Center flashlight) and sweet-objc-methods, with the Theos toolchain.
# Install: SweetTorch.dylib + SweetTorch.plist into /var/jb/Library/MobileSubstrate/DynamicLibraries/
set -e
cd "$(dirname "$0")"
T=${THEOS:-$HOME/theos}
CC="$T/toolchain/linux/iphone/bin/clang -target arm64-apple-ios16.0 -isysroot $T/sdks/iPhoneOS16.5.sdk -O2 -Wall"
mkdir -p out
$CC -dynamiclib -o out/SweetTorch.dylib SweetTorch.c -lobjc -framework CoreFoundation
$CC -o out/sweet-objc-methods sweet-objc-methods.c -lobjc
"$T/toolchain/linux/iphone/bin/ldid" -S out/SweetTorch.dylib
"$T/toolchain/linux/iphone/bin/ldid" -S out/sweet-objc-methods
