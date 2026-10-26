#!/bin/bash
# Runs inside the ios-sunfish-xnu container (tools/xnu-docker/build.sh starts it). /work is
# tools/xnu-docker/work, /patches is xnu/patches. Mirrors blacktop's darwin-xnu-build build.sh for
# macOS 13.5 (the CI's .github/workflows/xnu.yml) step by step, with Linux stand-ins for Xcode:
#   xnu source + blacktop's sed edits and patches + ours   (patches())
#   fakeroot headers: from the CI build tree (bt/), availability.pl from AvailabilityVersions
#   libfirehose_kernel.a: libdispatch's firehose_buffer.c compiled here (build_libdispatch())
#   the kernel: make install TARGET_CONFIGS="DEVELOPMENT ARM64 SWEET" SDKROOT=macosx (build_xnu())
# Not done (as HackiOS): CTF type data (dtrace's ctfconvert/ctfmerge are Xcode projects).
set -euo pipefail
export MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT ARCH_CONFIG=ARM64 MACHINE_CONFIG=SWEET   # as the CI env (the stage scripts check them)
JOBS=${JOBS:-$(nproc)}
W=/work
XNU_TAG=xnu-8796.141.3
DXB_SHA=663906528d1cd5ea8fde7de6f16de73db9511549
FAKEROOT=$W/fakeroot
BT=$W/bt                                  # CI build tree (xnu-buildtree artifact)
export IIG_TREE=$BT/build/xnu.obj
export XNU_SDK_PATH=/opt/sdk/MacOSX14.0.sdk XNU_SDK_VERSION=14.0
mkdir -p /opt/platform/MacOSX.platform $W/kdk $W/build

fetch() {    # fetch DIR URL REF
	[ -d "$1/.git" ] && return 0
	git init -q "$1"; git -C "$1" remote add origin "$2"
	git -C "$1" fetch -q --depth 1 origin "$3"; git -C "$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

echo "== sources"
fetch $W/dxb https://github.com/blacktop/darwin-xnu-build $DXB_SHA
fetch $W/AvailabilityVersions https://github.com/apple-oss-distributions/AvailabilityVersions.git refs/tags/AvailabilityVersions-121
fetch $W/libdispatch https://github.com/apple-oss-distributions/libdispatch.git refs/tags/libdispatch-1415.140.2
if [ ! -d $W/xnu/.git ] || [ "${FRESH:-1}" = 1 ]; then
	rm -rf $W/xnu $W/build/xnu.obj $W/build/xnu.sym
	fetch $W/xnu https://github.com/apple-oss-distributions/xnu.git refs/tags/$XNU_TAG
	echo "== patches (blacktop's sed edits, then blacktop's and ours in glob order, as the CI)"
	cd $W/xnu
	sed -i 's|^AVAILABILITY_PL="${SDKROOT}/${DRIVERKITROOT}|AVAILABILITY_PL="${FAKEROOT_DIR}|g' bsd/sys/make_symbol_aliasing.sh
	sed -i 's|^#include.*BSD.xcconfig.*||g' libsyscall/Libsyscall.xcconfig
	sed -i 's|^LDFLAGS_KERNEL_SDK	= -L$(SDKROOT).*|LDFLAGS_KERNEL_SDK	= -L$(FAKEROOT_DIR)/usr/local/lib/kernel -lfirehose_kernel|g' makedefs/MakeInc.def
	sed -i 's|^\(INCFLAGS_SDK[[:space:]]*=\) -I$(SDKROOT)/usr/local/include/\(kernel\)\{0,1\}|\1 -I$(FAKEROOT_DIR)/usr/local/include/kernel|' makedefs/MakeInc.def
	sed -i 's|export MIG := $(shell $(XCRUN) -sdk $(SDKROOT) -find mig)|export MIG := /opt/host/bin/mig|g' makedefs/MakeInc.cmd
	sed -i 's|export MIGCOM := $(shell $(XCRUN) -sdk $(SDKROOT) -find migcom)|export MIGCOM := /opt/host/bin/migcom|g' makedefs/MakeInc.cmd
	rm -rf $W/allpatches; mkdir -p $W/allpatches
	cp $W/dxb/patches/*.patch $W/allpatches/ 2>/dev/null || true
	cp /patches/*.patch /patches/*.sh $W/allpatches/
	for s in $W/allpatches/*.sh; do [ -e "$s" ] && KDKROOT=$W/kdk bash "$s"; done
	n=0; skipped=0
	for p in $W/allpatches/*.patch; do
		if git apply --check "$p" 2>/dev/null; then git apply "$p"; n=$((n+1)); else echo "skipped: $(basename $p)"; skipped=$((skipped+1)); fi
	done
	echo "applied $n patches, skipped $skipped"
	# Linux-build only (stock clang lacks Apple's kalloc_type builtins, see /opt/host/inc/xnu_clang_compat.h):
	# the compile-time pointer-type check takes *(void *), which only Apple's clang lets C++ do.
	python3 - <<'PY'
p = 'osfmk/kern/kalloc.h'
s = open(p).read()
a = '#define KALLOC_TYPE_IS_COMPATIBLE_PTR(ptr, type)                   \\\n'
assert s.count(a) == 1, 'kalloc.h changed'
s = s.replace(a, '#if XNU_NO_TYPE_BUILTINS\n#define KALLOC_TYPE_IS_COMPATIBLE_PTR(ptr, type) 1\n#else\n' + a, 1)
i = s.index('_Pragma("clang diagnostic pop")', s.index('#define KALLOC_TYPE_IS_COMPATIBLE_PTR(ptr, type)                   '))
j = s.index('\n', i)
s = s[:j + 1] + '#endif\n' + s[j + 1:]
open(p, 'w').write(s)
print('kalloc.h: KALLOC_TYPE_IS_COMPATIBLE_PTR off without the builtins')
PY
	# corecrypto's assembly macros: the syntax Apple's clang gets (v-registers), which LLVM takes too;
	# the other branch (q-registers) it rejects. Same instructions.
	sed -i 's|^#if defined(__clang__) \&\& ((defined(__apple_build_version__) \&\& __apple_build_version__ > 5010000))$|#if defined(__clang__)|' osfmk/arm64/corecrypto/arm64_isa_compatibility.h
	grep -q '^#if defined(__clang__)$' osfmk/arm64/corecrypto/arm64_isa_compatibility.h
	# OSData's "only plain data" check needs Apple's type summary of a struct, which stock clang
	# cannot give: compile-time only, so off without the builtins (the other OSData checks stay).
	python3 - <<'PY'
p = 'libkern/libkern/c++/OSData.h'
s = open(p).read()
a = '\t\tstatic_assert(KALLOC_TYPE_IS_DATA_ONLY(T) || (acquisition == kValueNoCopy),\n'
assert s.count(a) == 1, 'OSData.h changed'
s = s.replace(a, '\t\tstatic_assert(XNU_NO_TYPE_BUILTINS || KALLOC_TYPE_IS_DATA_ONLY(T) || (acquisition == kValueNoCopy),\n')
open(p, 'w').write(s)
print('OSData.h: data-only check off without the builtins')
PY
	python3 /opt/host/kalloc_tolerate.py
fi

echo "== fakeroot"
mkdir -p $FAKEROOT/usr/local/lib/kernel $FAKEROOT/usr/local/include
rsync -a $BT/fakeroot/usr/local/include/ $FAKEROOT/usr/local/include/
if [ ! -f $FAKEROOT/usr/local/libexec/availability.pl ]; then
	make -s -C $W/AvailabilityVersions install DSTROOT=$FAKEROOT >/dev/null
fi
ls $FAKEROOT/usr/local/libexec/availability.pl

if [ ! -f $FAKEROOT/usr/local/lib/kernel/libfirehose_kernel.a ]; then
	echo "== libfirehose_kernel.a"
	H=$BT/build/xnu.obj/EXPORT_HDRS
	mkdir -p $W/build/firehose
	xnu-clang -arch arm64 -c -Os -mkernel -nostdinc -fno-builtin -fno-stack-protector \
		-DKERNEL=1 -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=0 -DPRIVATE=1 -D__DARWIN_64_BIT_INO_T=1 \
		-DDISPATCH_USE_DTRACE=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
		-DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 -DOS_FIREHOSE_SPI=1 -DARM64_BOARD_CONFIG_SWEET \
		-I$W/libdispatch -I$W/libdispatch/src -I$W/libdispatch/src/firehose \
		-I$H/osfmk -I$H/bsd -I$H/libkern -I$H/pexpert -I$H/iokit -I$H/security \
		-I$W/xnu/libkern/firehose -I$H/libkern/os -I$W/xnu/EXTERNAL_HEADERS \
		-I$FAKEROOT/usr/local/include/kernel -I$FAKEROOT/usr/local/include \
		-o $W/build/firehose/firehose_buffer.o $W/libdispatch/src/firehose/firehose_buffer.c
	/opt/toolchain/linux/iphone/bin/libtool -static -o $FAKEROOT/usr/local/lib/kernel/libfirehose_kernel.a $W/build/firehose/firehose_buffer.o
fi

if [ ! -f $FAKEROOT/usr/local/lib/kernel/libcc_kext.a ]; then
	echo "== libcc_kext.a (stand-in for Xcode's libclang_rt.cc_kext.a: the profile runtime's two buffer helpers)"
	xnu-clang -arch arm64 -c -Os -mkernel -nostdinc -fno-builtin -I/opt/sdk/MacOSX14.0.sdk/usr/include \
		-o $W/build/cc_kext_stubs.o /opt/host/cc_kext/cc_kext_stubs.c
	/opt/toolchain/linux/iphone/bin/libtool -static -o $FAKEROOT/usr/local/lib/kernel/libcc_kext.a $W/build/cc_kext_stubs.o
fi

echo "== kernel"
# XNU's makefiles call some tools by absolute path
for t in xcrun codesign plutil sw_vers; do ln -sf /opt/host/bin/$t /usr/bin/$t; done
# the build tools of SETUP/ (compiled for this machine) need three Mach-O headers (as HackiOS's stage 2)
mkdir -p /opt/hackios/hostinc/mach-o
for f in loader.h nlist.h fat.h; do cp $W/xnu/EXTERNAL_HEADERS/mach-o/$f /opt/hackios/hostinc/mach-o/; done
HOSTCC="clang -include /opt/hackios/hostinc/hostcompat.h -I/opt/hackios/hostinc -Wno-unused-command-line-argument -Wno-error=implicit-function-declaration"
cd $W/xnu
# bsd/sys's sysproto.h/syscall.h rules read $(TARGET)/bsd.syscalls.master with TARGET empty there,
# i.e. /bsd.syscalls.master ("created from /bsd.syscalls.master" in the CI's files too); in a
# parallel build here they ran before anything made it, and came out empty. Made first, as the
# rule of makedefs/MakeInc.rule does (unifdef exits 1 when it removed something).
G=$W/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd
mkdir -p $G
unifdef $W/xnu/bsd/kern/syscalls.master > $G/bsd.syscalls.master || [ $? -eq 1 ]
cp $G/bsd.syscalls.master /bsd.syscalls.master
cmp $G/bsd.syscalls.master $BT/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/bsd.syscalls.master && echo "bsd.syscalls.master = the CI's"
export KERNEL_BUILD_DATE="$(TZ=UTC git log -1 --format=%cd --date=format-local:'%a %b %e %T %Z %Y')"
export KERNEL_BUILD_OBJROOT=$XNU_TAG
make install -j"$JOBS" ${MAKE_K:+-k} SDKROOT=macosx TARGET_CONFIGS="DEVELOPMENT ARM64 SWEET" CONCISE=0 LOGCOLORS=n \
	BUILD_WERROR=0 BUILD_LTO=0 DO_CTFMERGE=0 \
	SRCROOT=$W/xnu OBJROOT=$W/build/xnu.obj SYMROOT=$W/build/xnu.sym DSTROOT=$FAKEROOT FAKEROOT_DIR=$FAKEROOT \
	KDKROOT=$W/kdk TIGHTBEAMC=tightbeamc-not-supported RC_DARWIN_KERNEL_VERSION=22.6.0 \
	MEMORY_SIZE=$((16<<30)) SYSCTL_HW_PHYSICALCPU=$JOBS SYSCTL_HW_LOGICALCPU=$JOBS KERNEL_BUILDS_IN_PARALLEL=1 \
	CC=xnu-clang CXX=xnu-clang++ HOST_CC="$HOSTCC" HOST_FLEX=flex HOST_BISON=bison HOST_GM4=m4
ls -la $W/build/xnu.obj/kernel.development.sweet
