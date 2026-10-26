# Mesa for the iOS root

Mesa's OpenGL (OSMesa) with the freedreno driver for the Adreno 618, built for iOS, and `gputest`,
which checks that it reaches the GPU. The kernel side is patch 0090 (`SweetGPU`, the GPU kept on with
`adreno=21 adreno_keep=1`, see [`docs/GPU.md`](../docs/GPU.md)).

Built by [`build.sh`](build.sh) in CI ([`.github/workflows/mesa.yml`](../.github/workflows/mesa.yml),
artifact `mesa-ios`). Status: **builds in CI; not yet run on the phone.** The files go to `/usr/lib/libOSMesa.8.dylib` and `/usr/bin/gputest`, put onto the root by the kernel from the boot image (patch 0103, `mkadt.py --drop`).

## Credits

The approach, the pinned Mesa release (25.0.7), the patches in `patches/` and the libdrm stand-in in
`hakios-drm/` are HackiOS's ([oosexgang/hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel),
`tools/mk-mesa.sh` and `tools/mesa/`, commit `5fbd7fd`), under the MIT License. The AIR frontend in
`air/` (their `tools/mesa/air`: Metal shader bitcode to NIR, and `air_run`) and `patches/air-*.patch`
are also HackiOS's, from commit `8851317`, under the same license:

```
Copyright (c) 2025-2026 the HakiOS authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Changed here: the service name in `hakios-drm/hakios.drm.c` (`SweetGPU` instead of `HakiosGPU`), and
the build runs on macOS with Xcode's iPhoneOS SDK instead of their Linux cross compiler. Mesa itself is
MIT-licensed and is downloaded by the build, not kept here.
