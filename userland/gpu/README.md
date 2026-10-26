# OpenGL ES for CoreAnimation (backboardd)

CoreAnimation's render server draws with Metal, else OpenGL ES, else its own software rasterizer.
With no Apple GPU it ends up in software. These files give it an OpenGL ES that is Mesa freedreno on
the Adreno 618 (`mesa/`, kernel `SweetGPU`):

- `eagl.c`, `gl_osmesa.c`, `redwood_gl.h`: the OpenGL ES behind `EAGLContext` (built as
  `/usr/lib/libsunfish-gl.dylib`); `mkeagl.py` + `eagl_slots.txt` generate its function table;
- `sunfish_inject.c`: inserted into backboardd (`DYLD_INSERT_LIBRARIES` in its launchd job, built as
  `/usr/lib/libsunfish-inject.dylib`); waits for `SweetGPU`, loads the library and patches two virtual
  functions of CoreAnimation's `CA::OGL::GLESContext` (addresses of iOS 16.7.16 / 20H392, the build
  this port runs).

Built by `mesa/build.sh` (CI: `.github/workflows/mesa.yml`). Status: being ported, not yet run.

## Credits

`eagl.c`, `gl_osmesa.c`, `redwood_gl.h` and `sunfish_inject.c` (their `topaz_inject.c`) come from
[ios-topaz](https://github.com/MakrSas/ios-topaz) `userland/gpu/`, which based them on HackiOS's user
space; `mkeagl.py` and `eagl_slots.txt` from [HackiOS](https://github.com/oosexgang/hackios-xnu-kernel)
`userland/`. HackiOS user space is MIT-licensed, Copyright (c) 2026 OOSEX GANG (the full text is in
`mesa/README.md`). Changed here: paths, the GPU service name (`SweetGPU`) and the wait for it.
