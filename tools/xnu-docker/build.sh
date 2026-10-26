#!/bin/bash
# Build the SWEET XNU kernel locally in Docker (instead of a GitHub macOS runner).
#   tools/xnu-docker/build.sh              image (if needed) + kernel -> tools/xnu-docker/work/build/xnu.obj/
#   FRESH=0 tools/xnu-docker/build.sh      keep the patched source and objects (incremental)
#   MAKE_K=1 ...                           make -k: report every compile error in one run
# Needs tools/xnu-docker/work/bt: the CI's xnu-buildtree artifact, unpacked (the iig output and
# fakeroot headers Xcode made): gh run download RUN -n xnu-buildtree -D work/bt && tar -C work/bt -xzf work/bt/buildtree.tgz
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$HERE/work"
[ -d "$HERE/work/bt/build/xnu.obj" ] || { echo "no $HERE/work/bt (see the comment at the top)" >&2; exit 1; }
docker image inspect ios-sunfish-xnu >/dev/null 2>&1 || docker build -t ios-sunfish-xnu "$HERE"
exec docker run --rm -e FRESH="${FRESH:-1}" -e MAKE_K="${MAKE_K:-}" -e JOBS="${JOBS:-}" \
	-v "$HERE/work:/work:z" -v "$HERE/../../xnu/patches:/patches:ro,z" \
	-v "$HERE/bin:/opt/host/bin2:ro,z" -v "$HERE/hostinc:/opt/host/inc:ro,z" -v "$HERE/cc_kext:/opt/host/cc_kext:ro,z" -v "$HERE/build-in-container.sh:/build.sh:ro,z" \
	ios-sunfish-xnu bash -c 'cp /opt/host/bin2/* /opt/host/bin/ && bash /build.sh'
