# XNU (xnu-8796.141.3, the SWEET kernel of ios-sunfish) built on Linux instead of a GitHub macOS runner.
# The approach is HackiOS's (github.com/oosexgang/hackios-xnu-kernel, MIT for its scripts; the files
# in hackios/ are theirs): Apple's Mach-O linker and tools from the theos Linux toolchain, a port of
# MIG, wrappers for xcrun/clang/iig. Our build is the macOS-config kernel of blacktop's
# darwin-xnu-build (SDKROOT=macosx), so the SDK is the macOS 14.0 one (Xcode 15.0, as on macos-14).
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
	ca-certificates curl git xz-utils make bison flex m4 perl python3 autoconf automake libtool pkg-config \
	gawk rsync file cpio uuid-dev libbsd-dev zlib1g-dev clang lld llvm build-essential tcsh ruby python-is-python3 \
	&& rm -rf /var/lib/apt/lists/*
# Apple's cctools + ld64 for Linux (what theos installs)
RUN mkdir -p /opt/toolchain && curl -sL --fail --retry 3 \
	https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-x86_64.tar.xz \
	| tar -xJ -C /opt/toolchain && ls /opt/toolchain && find /opt/toolchain -maxdepth 3 -name ld | head
# The macOS 14.0 SDK (Xcode 15.0)
RUN mkdir -p /opt/sdk && curl -sL --fail --retry 3 \
	https://github.com/joseluisq/macosx-sdks/releases/download/14.0/MacOSX14.0.sdk.tar.xz \
	| tar -xJ -C /opt/sdk && ls /opt/sdk
# MIG: Apple's bootstrap_cmds does not build on glibc; this port does (as in HackiOS's setup.sh)
RUN git init -q /opt/src/bootstrap_cmds && cd /opt/src/bootstrap_cmds \
	&& git remote add origin https://github.com/markmentovai/bootstrap_cmds \
	&& git fetch -q --depth 1 origin f337e600741638896fb6056ce87c6479f74b89a3 && git checkout -q FETCH_HEAD \
	&& autoreconf --install >/dev/null 2>&1 && sh configure --prefix=/opt/host >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null \
	&& ls /opt/host/bin
# unifdef
RUN git init -q /opt/src/unifdef && cd /opt/src/unifdef \
	&& git remote add origin https://github.com/fanf2/unifdef.git \
	&& git fetch -q --depth 1 origin 08d12cd3059fe072929ea001ad9833f5487fd355 && git checkout -q FETCH_HEAD \
	&& printf '"@(#) $Version: unifdef-2.12 $\\n"\n"@(#) $Date: 2020-11-06 11:22:51 +0000 $\\n"\n' > version.h \
	&& cc -std=gnu11 -O2 -w -o /opt/host/bin/unifdef unifdef.c
COPY hackios /opt/hackios
COPY bin /opt/host/bin
COPY hostinc /opt/host/inc
COPY cc_kext /opt/host/cc_kext
ENV PATH=/opt/host/bin:$PATH
WORKDIR /work
