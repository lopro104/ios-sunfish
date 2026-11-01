#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THEOS="${THEOS:-$HOME/theos}"
SDK="$THEOS/sdks/iPhoneOS16.5.sdk"
LD="$THEOS/toolchain/linux/iphone/bin/ld"
MESA_BUILD="$ROOT/research/hakios-metal-src/workspace/xnu-deps/build/mesa/build-softpipe-freedreno"
OUT="$ROOT/userland/gpu"

echo "Building HakiosMetalCore.dylib..."
clang++ -target arm64-apple-ios16.0 -isysroot "$SDK" --ld-path="$LD" \
    -Wno-unused-command-line-argument -O2 -dynamiclib \
    -install_name @loader_path/HakiosMetalCore.dylib \
    -Wall -Wextra -Wno-protocol -Wno-objc-protocol-property-synthesis -Wno-property-attribute-mismatch \
    -DHAKIOS_METAL_GPU -DHAKIOS_METAL_A618 \
    -I"$ROOT/research/hakios-metal-src/hakios-metal" \
    -I"$ROOT/research/hakios-metal-src/hakios-metal/include" \
    -I"$ROOT/research/hakios-metal-src/hakios-metal/plugin" \
    "$ROOT/research/hakios-metal-src/hakios-metal/plugin/HakiosMetal.m" \
    "$ROOT/research/hakios-metal-src/hakios-metal/plugin/hakios_metallib.c" \
    "$ROOT/research/hakios-metal-src/hakios-metal/out/ios-a618/libhakios-metal.a" \
    "$MESA_BUILD/src/compiler/air/libair.a" \
    "$MESA_BUILD/src/freedreno/ir3/libfreedreno_ir3.a" \
    "$MESA_BUILD/src/freedreno/isa/libir3encode.a" \
    "$MESA_BUILD/src/freedreno/isa/libir3decode.a" \
    "$MESA_BUILD/src/compiler/isaspec/libisaspec.a" \
    "$MESA_BUILD/src/compiler/nir/libnir.a" \
    "$MESA_BUILD/src/compiler/libcompiler.a" \
    "$MESA_BUILD/src/freedreno/fdl/libfreedreno_layout.a" \
    "$MESA_BUILD/src/freedreno/common/libfreedreno_common.a" \
    "$MESA_BUILD/src/util/libmesa_util.a" \
    "$MESA_BUILD/src/util/libmesa_util_sse41.a" \
    "$MESA_BUILD/src/util/blake3/libblake3.a" \
    "$MESA_BUILD/src/c11/impl/libmesa_util_c11.a" \
    -lz -lm -framework Foundation -framework Metal -framework IOKit -framework CoreFoundation -framework IOSurface -lc++ \
    -o "$OUT/HakiosMetalCore.dylib"

echo "Building SweetMetal bundle executable..."
clang++ -target arm64-apple-ios16.0 -isysroot "$SDK" --ld-path="$LD" \
    -Wno-unused-command-line-argument -O2 -bundle \
    "$OUT/SweetMetal.mm" \
    -framework Foundation -framework Metal -framework IOKit -framework CoreFoundation \
    -o "$OUT/SweetMetal"
"$THEOS/toolchain/linux/iphone/bin/ldid" -S "$OUT/SweetMetal"

echo "Building hakios-metal-client..."
clang++ -target arm64-apple-ios16.0 -isysroot "$SDK" --ld-path="$LD" \
    -Wno-unused-command-line-argument -O2 \
    "$OUT/hakios-metal-client.mm" \
    -framework Foundation -framework Metal -framework IOSurface \
    -o "$OUT/hakios-metal-client"
"$THEOS/toolchain/linux/iphone/bin/ldid" -S "$OUT/hakios-metal-client"

echo "Hakios Metal build complete!"
