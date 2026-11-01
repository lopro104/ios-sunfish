#!/bin/bash
# The compiler half of hakios-metal for the phone: Mesa 25.0.7 cross-built for arm64 iOS with hakios's
# AIR front end (AIR -> NIR -> ir3), laid out where hakios-metal's tools/compiler.mk looks for it
# (HAKIOS/xnu-deps/build/mesa/{mesa-25.0.7,build-softpipe-freedreno}), so that
#   make ios MESA=1 CHIP=618 HAKIOS=<workspace> ...
# links it in. This is hakios's tools/mk-mesa.sh with our cross compiler (host clang + the Theos ld64) in
# place of their xnu-clang wrapper, and only the static libraries the driver links are built (the
# OSMesa library is ours from .github/workflows/mesa.yml already).
#
#   userland/gpu/mk-hakios-mesa-ios.sh [WORKSPACE]
#     WORKSPACE  default research/hakios-metal-src/workspace (hakios-metal's ../workspace)
#     TOOLS      hakios-xnu-kernel's tools/ (default research/hakios-xnu-tools/tools)
set -e
HERE=$(cd "$(dirname "$0")/../.." && pwd)
W=${1:-$HERE/research/hakios-metal-src/workspace}
TOOLS=${TOOLS:-$HERE/research/hakios-xnu-tools/tools}
VERSION=25.0.7
SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
T=${THEOS:-$HOME/theos}
SDK=$T/sdks/iPhoneOS16.5.sdk
LD=$T/toolchain/linux/iphone/bin/ld
B=$W/xnu-deps/build/mesa
mkdir -p "$B"
cd "$B"

PATCHES=$(cat "$TOOLS"/mesa/*.patch | sha256sum | cut -d' ' -f1)
if [ ! -d mesa-$VERSION ] || [ "$(cat mesa-$VERSION/.hakios-patches 2>/dev/null)" != "$PATCHES" ]; then
	[ -f mesa-$VERSION.tar.xz ] || curl -sL --fail -o mesa-$VERSION.tar.xz https://archive.mesa3d.org/mesa-$VERSION.tar.xz
	echo "$SHA256  mesa-$VERSION.tar.xz" | sha256sum -c --quiet
	rm -rf mesa-$VERSION build-*
	tar xJf mesa-$VERSION.tar.xz
	for p in "$TOOLS"/mesa/*.patch; do patch -s --no-backup-if-mismatch -d mesa-$VERSION -p1 < "$p"; done
	echo "$PATCHES" > mesa-$VERSION/.hakios-patches
fi
rm -rf mesa-$VERSION/src/hakios-drm mesa-$VERSION/src/compiler/air
cp -r "$TOOLS/mesa/hakios-drm" mesa-$VERSION/src/hakios-drm
cp -r "$TOOLS/mesa/air" mesa-$VERSION/src/compiler/air

cat > cross.ini <<INI
[binaries]
c = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
cpp = ['clang++', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK', '-fuse-cxa-atexit']
objc = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', '$SDK']
ar = 'llvm-ar'
ranlib = 'llvm-ranlib'
strip = 'llvm-strip'
pkg-config = 'false'

[built-in options]
c_link_args = ['--ld-path=$LD']
cpp_link_args = ['--ld-path=$LD']
objc_link_args = ['--ld-path=$LD']

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
INI
BUILD=build-softpipe-freedreno
# (a cross.ini that changed needs a new setup: rm -rf $B/$BUILD)
[ -f $BUILD/build.ninja ] || meson setup $BUILD mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
	--prefix=/redwood --libdir=lib --buildtype=release -Db_ndebug=true \
	-Dplatforms= -Dgallium-drivers=softpipe,freedreno -Dfreedreno-kmds=msm -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
	-Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
	-Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
	-Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
	-Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false > meson-setup.log
# the archives hakios-metal links (tools/compiler.mk MESA_ARCHIVE_PATHS) and the front end's
ninja -C $BUILD src/compiler/air/libair.a src/freedreno/ir3/libfreedreno_ir3.a src/freedreno/isa/libir3encode.a \
	src/freedreno/isa/libir3decode.a src/compiler/isaspec/libisaspec.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/freedreno/fdl/libfreedreno_layout.a \
	src/freedreno/common/libfreedreno_common.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a \
	src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a > ninja.log 2>&1 || { tail -30 ninja.log; exit 1; }
echo "mk-hakios-mesa-ios.sh: $B/$BUILD"
ls -l $BUILD/src/compiler/air/libair.a $BUILD/src/freedreno/ir3/libfreedreno_ir3.a $BUILD/src/compiler/nir/libnir.a
