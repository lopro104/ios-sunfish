#!/usr/bin/env bash
# HakiOS's window-server Metal plug-in (hakios-xnu-kernel userland/src/HakiosMetalCA.m) for the Pixel 4a's Adreno 618, as
# the executable of /System/Library/Extensions/SweetMetal.bundle: over hakios-metal's sunfish-a618 branch with its
# compiler (make ios MESA=1 CHIP=618), the class renamed to what the kernel advertises (SweetMetalDevice), and /redwood's
# QuartzCore-default.metallib replaced by this image's own, which 16.7's QuartzCore has. Output: userland/gpu/out-ca/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${THEOS:-$HOME/theos}"
SDK="$T/sdks/iPhoneOS16.5.sdk"
LD="$T/toolchain/linux/iphone/bin/ld"
HM="$ROOT/research/hakios-metal-src/hakios-metal"
M="$ROOT/research/hakios-metal-src/workspace/xnu-deps/build/mesa/build-softpipe-freedreno"
SRC="$ROOT/research/hakios-xnu-tools/userland/src/HakiosMetalCA.m"
OUT="$ROOT/userland/gpu/out-ca"
mkdir -p "$OUT"
sed 's|/redwood/share/ca-share/QuartzCore-default.metallib|/System/Library/Frameworks/QuartzCore.framework/default.metallib|g' "$SRC" > "$OUT/HakiosMetalCA.m"
make -C "$HM" -j8 ios MESA=1 CHIP=618 SDK="$SDK" \
	IOS_CXX="clang++ -target arm64-apple-ios16.0 -isysroot $SDK --ld-path=$LD -Wno-unused-command-line-argument" \
	IOS_AR="llvm-ar --format=darwin rcs" 2>&1 | grep -E "error:|\*\*\*" || true
ARCHIVES=""
for a in src/compiler/air/libair.a src/freedreno/ir3/libfreedreno_ir3.a src/freedreno/isa/libir3encode.a \
    src/freedreno/isa/libir3decode.a src/compiler/isaspec/libisaspec.a src/compiler/nir/libnir.a src/compiler/libcompiler.a \
    src/freedreno/fdl/libfreedreno_layout.a src/freedreno/common/libfreedreno_common.a src/util/libmesa_util.a \
    src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a; do
	ARCHIVES="$ARCHIVES $M/$a"
done
clang -target arm64-apple-ios16.0 -isysroot "$SDK" --ld-path="$LD" -Wno-unused-command-line-argument -O2 -g0 -bundle \
	-fno-objc-arc -w -DHakiosMetalDevice=SweetMetalDevice -DHAKIOS_METAL_A618 -I"$HM/include" \
	"$OUT/HakiosMetalCA.m" "$HM/out/ios-a618/libhakios-metal.a" $ARCHIVES -lz -lm -lc++ -lobjc \
	-framework Foundation -framework IOKit -framework IOSurface -framework Metal -framework CoreFoundation \
	-o "$OUT/SweetMetal"
"$T/toolchain/linux/iphone/bin/ldid" -S "$OUT/SweetMetal"
ls -l "$OUT/SweetMetal"
