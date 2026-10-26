# Building the kernel locally (Docker)

`tools/xnu-docker/build.sh` builds `kernel.development.sweet` on Linux, instead of the GitHub
macOS runner (which often cannot get a machine: "jobs targeting macOS arm64 runners may experience
longer queue times"). Output: `tools/xnu-docker/work/build/xnu.obj/kernel.development.sweet`.

    gh run download RUN -R lopro104/ios-sunfish -n xnu-buildtree -D tools/xnu-docker/work/bt
    tar -C tools/xnu-docker/work/bt -xzf tools/xnu-docker/work/bt/buildtree.tgz
    tools/xnu-docker/build.sh                  # fresh: source, patches, all objects (~15 min, 12 cores)
    FRESH=0 tools/xnu-docker/build.sh          # incremental: keeps the patched source and objects
    MAKE_K=1 ...                               # make -k (every error in one run)

`work/bt` is the CI's `xnu-buildtree` artifact of any recent run: the files Xcode's closed `iig`
generated (DriverKit glue, used as they are) and the fakeroot headers (libplatform, firehose).

## How
Mirrors blacktop's darwin-xnu-build `build.sh` for macOS 13.5 (`.github/workflows/xnu.yml`):
xnu-8796.141.3 + blacktop's sed edits + blacktop's and our `xnu/patches` in glob order (the same 4
skipped as in CI), `MACOS_VERSION=13.5 MACHINE_CONFIG=SWEET` for our stage scripts, then
`make install TARGET_CONFIGS="DEVELOPMENT ARM64 SWEET" SDKROOT=macosx`. The image: Ubuntu 24.04,
clang 18, Apple's ld64/cctools (theos Linux toolchain), the macOS 14.0 SDK (Xcode 15.0's), MIG
(the bootstrap_cmds port), unifdef. The approach and `hackios/` are HackiOS's
(github.com/oosexgang/hackios-xnu-kernel, MIT for their scripts).

## Where it differs from the CI kernel
- No CTF type data (dtrace's ctfconvert/ctfmerge are Xcode projects): DTrace/lldb type info only.
- Stock clang lacks Apple's kalloc_type builtins: types are described "pointers and data" except
  C++ arithmetic/enum (arrays) as data (`hostinc/xnu_clang_compat.h`); two compile-time checks
  (`KALLOC_TYPE_IS_COMPATIBLE_PTR`, OSData's data-only assert) are off. Heap zone choice differs.
- corecrypto's assembly macros take the branch Apple's clang takes (same instructions).
- `cc_kext/cc_kext_stubs.c` stands in for Xcode's libclang_rt.cc_kext.a: the profile runtime's two
  buffer helpers (no PGO here) and `__cxa_atexit` (clang 18 registers C++ global destructors with
  it under -fapple-kext; the kernel never exits).
- /bsd.syscalls.master is made before make (a parallel-build race on Linux left sysproto.h empty).
First comparison (0121 local vs 0120 CI): same segments within 3% (__TEXT_EXEC 8.51 vs 8.78 MB),
24174 vs 24170 global symbols, 180 sweet_ symbols in both; fewer local labels.
