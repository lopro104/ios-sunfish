#!/bin/bash
# Build Mesa (OSMesa with freedreno and softpipe) for the iOS root, on macOS with Xcode's iPhoneOS
# SDK (the CI's .github/workflows/mesa.yml), plus mesa/gputest.c, the program that finds out
# whether Mesa reaches the Adreno through the kernel's SweetGPU service (patch 0090).
#
# This follows HackiOS's tools/mk-mesa.sh (MIT, https://github.com/oosexgang/hackios-xnu-kernel,
# commit 5fbd7fd): the same pinned Mesa release, their patches in mesa/patches/ and their libdrm
# stand-in in mesa/hakios-drm/ (service name changed to SweetGPU). They cross-build on Linux with
# their own compiler wrapper; here Xcode's clang does it.
#
#   mesa/build.sh OUT_DIR
#
# OUT_DIR gets libOSMesa.8.dylib (install name /usr/lib/libOSMesa.8.dylib, a directory the root has), any other dylib
# Mesa builds next to it, gputest (/usr/bin/gputest) and the GL headers it was built with.
set -euo pipefail
VERSION=25.0.7
SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
WORK=${WORK:-$PWD/mesa-work}
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CLANG=$(xcrun --sdk iphoneos -f clang)
MIN=16.0
mkdir -p "$WORK"
cd "$WORK"

[ -f mesa-$VERSION.tar.xz ] || curl -sL --fail -o mesa-$VERSION.tar.xz https://archive.mesa3d.org/mesa-$VERSION.tar.xz
echo "$SHA256  mesa-$VERSION.tar.xz" | shasum -a 256 -c --quiet
rm -rf mesa-$VERSION build
tar xJf mesa-$VERSION.tar.xz
for p in "$HERE"/patches/*.patch; do
	echo "patch: $(basename "$p")"
	patch -s --no-backup-if-mismatch -d mesa-$VERSION -p1 < "$p"
done
cp -r "$HERE/hakios-drm" mesa-$VERSION/src/hakios-drm
# HackiOS's AIR frontend (mesa/air, their tools/mesa/air): reads Apple's Metal shader bitcode (AIR,
# in .metallib files) and makes NIR, which ir3 compiles for the Adreno. patches/air-frontend.patch
# and patches/air-ir3.patch hook it into the build; air_run draws with one Metal shader.
cp -r "$HERE/air" mesa-$VERSION/src/compiler/air

python3 -m venv venv
venv/bin/pip -q install mako pyyaml packaging meson
export PATH=$WORK/venv/bin:$PATH

cat > cross.ini <<INI
[binaries]
c = ['$CLANG', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=$MIN']
cpp = ['$CLANG++', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=$MIN']
objc = ['$CLANG', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=$MIN']
ar = '$(xcrun -f ar)'
ranlib = '$(xcrun -f ranlib)'
strip = '$(xcrun -f strip)'
pkg-config = 'false'

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
INI

# HackiOS's options, with the prefix of this port.
meson setup build mesa-$VERSION --cross-file cross.ini --wrap-mode=nofallback \
	--prefix=/usr --libdir=lib --buildtype=release -Db_ndebug=true \
	-Dplatforms= -Dgallium-drivers=softpipe,freedreno -Dfreedreno-kmds=msm -Dvulkan-drivers= -Dglx=disabled \
	-Degl=disabled -Dgbm=disabled -Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled \
	-Dgles2=enabled -Dopengl=true -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled \
	-Dgallium-va=disabled -Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false
ninja -C build
# air_run (not built by default): a Metal shader of the system's, translated, drawn on the Adreno
ninja -C build src/gallium/drivers/freedreno/air_run
cp build/src/gallium/drivers/freedreno/air_run "$OUT/air_run"

# Every dylib the build made, with /usr/lib install names (libOSMesa links the others by those).
for lib in $(find build -name '*.dylib' -type f); do
	cp "$lib" "$OUT/"
done
# Their references to each other (meson's @rpath/... or build paths) by /usr/lib too.
fixup() {
	for dep in $(otool -L "$1" | awk 'NR > 1 { print $1 }'); do
		name=$(basename "$dep")
		if [ -f "$OUT/$name" ] && [ "$dep" != "/usr/lib/$name" ]; then
			install_name_tool -change "$dep" "/usr/lib/$name" "$1"
		fi
	done
}
for lib in "$OUT"/*.dylib; do
	install_name_tool -id /usr/lib/$(basename "$lib") "$lib"
	fixup "$lib"
done
mkdir -p "$OUT/include"
cp -r mesa-$VERSION/include/GL mesa-$VERSION/include/KHR "$OUT/include/"

"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror \
	-I"$OUT/include" -o "$OUT/gputest" "$HERE/gputest.c" "$OUT/libOSMesa.8.dylib" -framework IOKit
fixup "$OUT/gputest"
# The OpenGL ES layer behind EAGLContext for backboardd (userland/gpu: ios-topaz's, after HackiOS)
# and the library that loads it into backboardd (DYLD_INSERT_LIBRARIES in its launchd job).
G=$HERE/../userland/gpu
nm -gU "$OUT/libOSMesa.8.dylib" | awk '$3 ~ /^_gl/ { print substr($3, 2) }' > "$WORK/exported-gl.txt"
python3 "$G/mkeagl.py" "$G/eagl_slots.txt" mesa-$VERSION/src/mapi/glapi/registry/gl.xml "$WORK/exported-gl.txt" \
	"$G/eagl.c" > "$WORK/eagl_gen.h"
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -dynamiclib \
	-install_name /usr/lib/libsunfish-gl.dylib -I"$WORK" -I"$G" -framework CoreFoundation -framework IOKit \
	-framework IOSurface -lobjc "$OUT/libOSMesa.8.dylib" -o "$OUT/libsunfish-gl.dylib" "$G/eagl.c" "$G/gl_osmesa.c"
fixup "$OUT/libsunfish-gl.dylib"
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -dynamiclib \
	-install_name /usr/lib/libsunfish-inject.dylib -framework CoreFoundation -framework IOKit -lobjc \
	-o "$OUT/libsunfish-inject.dylib" "$G/sunfish_inject.c"
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror -o "$OUT/bbd_wrap" "$G/bbd_wrap.c"
# The USB network as a configd network service (userland/net).
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror -framework CoreFoundation \
	-framework SystemConfiguration -o "$OUT/sweet-net" "$HERE/../userland/net/sweet-net.c"
cat > "$WORK/sweet-net.entitlements" <<'ENT'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
	<key>com.apple.SystemConfiguration.SCDynamicStore-write-access</key><true/>
	<key>platform-application</key><true/>
</dict></plist>
ENT
# What LaunchServices knows about application extensions (userland/ls).
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror -fobjc-arc -framework Foundation \
	-o "$OUT/sweet-lsq" "$HERE/../userland/ls/sweet-lsq.m"
# Plane layout probe for SweetSurface (userland/tools).
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror -framework CoreFoundation -framework IOSurface \
	-o "$OUT/sweet-surfplane" "$HERE/../userland/tools/sweet-surfplane.c"
"$CLANG" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=$MIN -O2 -Wall -Werror -framework CoreFoundation -framework IOSurface \
	-o "$OUT/sweet-shot" "$HERE/../userland/tools/sweet-shot.c"
fixup "$OUT/air_run"
codesign -f -s - "$OUT"/*.dylib "$OUT/gputest" "$OUT/bbd_wrap" "$OUT/sweet-lsq" "$OUT/sweet-surfplane" "$OUT/sweet-shot" "$OUT/air_run"
codesign -f -s - --entitlements "$WORK/sweet-net.entitlements" "$OUT/sweet-net"
otool -L "$OUT/gputest" "$OUT"/*.dylib
ls -l "$OUT"
