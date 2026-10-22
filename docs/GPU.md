# GPU bring-up (Adreno 618)

Status (2026-10-22): **probe steps written and built, not run on a phone yet.** The goal is
CoreAnimation drawing on the GPU instead of the CPU. This first part only finds out, step by step,
how much of the GPU block this kernel may touch under Qualcomm's hypervisor.

The approach and the first ten steps are ported from
[HackiOS](https://github.com/oosexgang/hackios-xnu-kernel) (OOSEX GANG, APSL 2.0). HackiOS takes an
Adreno 642L on the Snapdragon 778G all the way to drawing frames for iOS through Mesa's freedreno.
Their file is `xnu/files/bsd/dev/redwood_adreno.c`; ours is `bsd/dev/sweet_adreno.c` (patch `0083`).
Every address was re-derived for SM7150, and the source of each one is noted in the file. The code
is the same for sweet and sunfish (same SoC).

## Running it

Add `adreno=N` to the boot-args. The kernel then runs steps 1 to N in a thread, 40 s after the root
is mounted (`adreno_delay=S` changes that), so the GUI is already up. With `sweetusb=2` the lines
stream live over USB (`tools/sweet-usbnet.sh`); otherwise pull the log from recovery. Every line
starts with `SWEET GPU:`. When the steps are over, what they switched on is switched off again
(`adreno_keep=1` leaves it on).

Go one step at a time: `adreno=1`, then `adreno=2`, and so on. A step announces each access before
making it and pauses 1.5 s after each announcement (`adreno_pause=MS`; `adreno_fine=1` pauses
before every single access). If the phone resets, the last line in the log names the access that
did it.

| Step | Kind | What | Success looks like |
| --- | --- | --- | --- |
| 1 | reads | GCC GPU clocks, RPMh names `gfx.lvl` / `cx.lvl` / `mx.lvl` from cmd-db (`0x85f20000`) | `RPMh has "gfx.lvl" at ...; levels: ...` |
| 2 | reads | GPU speed-bin fuse (`0x7841a2`) | `speed bin N` (0-4) |
| 3 | reads | GPU clock controller `0x5090000` as ABL left it | values, no reset |
| 4 | **writes** | CX power switch on (`gpu_cx_gdsc`, `0x509106c`) | `CX is on` |
| 5 | **writes** | GMU clock 200 MHz, GPU bus/memory/AHB clocks on; measures them with the SoC's frequency counter | `cxo runs at ~19200000 Hz`, `THE REGISTER BUS OF THE GPU BLOCK HAS ITS CLOCK` |
| 6 | reads | GMU registers (`0x506a000`), always-on counter, GBIF, cx_mem | `THE GPU BLOCK IS ALIVE` (counter at 19.2 MHz) |
| 7 | reads | GMU RSCC (`0x508d000`) | values |
| 8 | reads | GPU SMMU (`0x5040000`) ID registers, stream table | `SMMU: N stream match registers, M contexts ...` |
| 9 | reads | SMMU context 0 | values |
| 10 | **writes** | two words into GMU data memory, read back, restored | `TAKES WHAT IS WRITTEN: its firmware can be loaded` |

## What comes after (from HackiOS, steps 11-22)

TrustZone calls and loading the zap shader (`a615_zap.mdt` on sunfish), GMU page tables and
firmware (`a630_gmu.bin`), HFI message queues, powering the 3D core, the SQE microcode
(`a630_sqe.fw`), the first command ring, and a first frame. Then a `RedwoodGPU`-style service that
speaks Linux's msm DRM interface, so Mesa's freedreno (a6xx; the A618 is supported upstream) can
draw for CoreAnimation. Each of these needs the results of steps 1-10 from a real phone before it
can be written with any confidence.
