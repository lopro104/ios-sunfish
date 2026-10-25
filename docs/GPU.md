# GPU bring-up (Adreno 618)

Status (2026-10-24): **steps 1-21 written and built, not run on a phone yet.** The goal is
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

## Steps 11-21 (patch 0089)

Status (2026-10-24): **written and compiled, not run on a phone.** They take the GPU from "the GMU
takes what is written" to "the 3D core writes a word into our memory on command". The A618 has the
older ("legacy") GMU, so these steps follow Google's own driver for this exact GPU and firmware
(msm-4.14 `drivers/gpu/msm`, which runs with `qcom,gpu-quirk-hfi-use-reg` on this SoC), checked
against Linux's legacy path in `a6xx_gmu.c` / `a6xx_hfi.c`. HackiOS's step layout, SMMU page
tables, TrustZone calls and command ring are reused; their GMU code is for a newer GMU and is not.

### Firmware

Steps 12 on need the Pixel's own GPU firmware. Since patch 0101 the boot image can carry it: give
`tools/mkadt.py` the directory with Android's firmware files (an Android tree's
`vendor/google/sunfish/proprietary/vendor/firmware`, or `adb pull`ed as below):

```bash
python3 tools/mkadt.py shim/out/sunfish.adt --highmem --device sunfish --firmware DIR
```

The kernel looks in the device tree's `/sweet-fw` first and only then in `/fw/adreno/` on the iOS
root. To put the files on the root instead, copy them once, with Android running:

```bash
mkdir -p fw-adreno
for f in a618_gmu.bin a630_sqe.fw a615_zap.mdt a615_zap.b00 a615_zap.b01 a615_zap.b02; do
  adb pull /vendor/firmware/$f fw-adreno/
done
sudo mount -o loop,force ios16-root.img /mnt
sudo mkdir -p /mnt/fw/adreno && sudo cp fw-adreno/* /mnt/fw/adreno/
sudo umount /mnt
```

If `adb pull` says a file does not exist, run `adb shell ls /vendor/firmware | grep -E 'a6|a618'`
and send the list: the zap shader may have a different number of `.bNN` segments (step 19 reads the
segment list from the `.mdt` and asks for exactly the files it names).

### What each step does

| Step | Kind | What | Success looks like |
| --- | --- | --- | --- |
| 11 | asks TrustZone | Is "is this call available" available; are the image-loading calls there; which images it "supports" | `"supported" ... GPU x/y` (any answer is information) |
| 12 | asks TrustZone | Check the signature of `a615_zap.mdt` (image 13) | `TRUSTZONE ACCEPTS THE ZAP SHADER'S SIGNATURE` |
| 13 | **writes** | Page tables for the GMU's memory (caches at 0x4000/0x44000, HFI, dump, log at 0x6000_0000+); SMMU context 1, stream 5 | no `SMMU did not take what was written` |
| 14 | **writes** | `a618_gmu.bin` into the GMU's code memory, read back | `THE GMU'S FIRMWARE IS LOADED` |
| 15 | **writes** | RSCC and PDC (0xb290000, 0xb490000) programs; GMU registers; GMU out of reset | `THE GMU'S FIRMWARE RUNS` (0xbabeface) |
| 16 | talks to GMU | "boot" request with the GX rail level, SPTP power on, HFI queues, init / version / speed table / bus table / test | `THE GMU'S MESSAGE QUEUES ARE UP`, `firmware is version 1.8.x` |
| 17 | asks GMU | Power the 3D core (OOB GPU), set the speed (180/267/355 MHz, `adreno_speed=1..3`) | `THE 3D CORE HAS POWER AND ANSWERS` |
| 18 | **writes** | Ring, `a630_sqe.fw`, scratch page; SMMU context 0, stream 0 | no SMMU refusal |
| 19 | asks TrustZone | Load the zap shader into its reserved memory (0x98f95000) and start it | `TRUSTZONE HAS AUTHENTICATED AND STARTED THE ZAP SHADER` |
| 20 | **writes** | The 3D core's registers (Linux's A618 values), start the command processor, "initialize" | `THE 3D CORE TAKES COMMANDS` |
| 21 | commands | Leave secure mode; write 0x600dc0de into our page | `THE 3D CORE WRITES OUR MEMORY ON COMMAND` |

At the end everything is undone in reverse: the 3D core given back, the GMU sent to slumber and
held in reset, the zap shader handed back to TrustZone, the RPMh channel put to sleep, the SMMU
streams cleared, then the clocks and CX as before. `adreno_keep=1` leaves it all on.

As with steps 1-10: one new step per boot (`adreno=11`, then `adreno=12`, ...). The steps that ask
TrustZone or the hypervisor something are the ones most likely to end a run without a word; the log
line before the reset names the call.

## Corrections from ios-surya (patch 0092)

[arbuxiktech/ios-surya](https://github.com/arbuxiktech/ios-surya), another ios-sweet fork, runs on the
Poco X3 NFC: the same SM7150 and Adreno 618, the same GPU firmware. It has brought the GPU up on real
hardware as far as the command processor running commands (its NOTES.md, with photos). Our steps were
written without hardware, so they were checked against it; most of the GMU sequence agreed (chip ID
0x06018000, fence range, PDC and RSC programs, ITCM upload, boot request 22/30). What did not:

| Their finding | Our steps before | Now |
| --- | --- | --- |
| The SMMU's registers need GPUCC `0x5000` bit 0 (`gpu_cc_hlos1_vote_gpu_smmu_clk`); without it the first read stalls the bus | not set: step 8 would have hung | step 5 sets it; steps 8, 9, 13 refuse to run without it |
| Reading `gcc_gpu_memnoc_gfx` (0x17100c) early stopped the phone; after turning on `gpu_cfg_ahb`, `ddrss_gpu_axi`, `gpu_memnoc_gfx` in that order it reads | step 1 read it first thing | step 1 no longer reads it; step 5 uses their order |
| `cxo_aon` on; the GMU clock's wake/sleep counters at 0xff | not done | done in step 5 |
| The boot request alone powered the 3D core; no message queues were used | a failed message stopped step 16 | step 16 goes on if the core has power |
| The command processor ran with no zap shader | step 19 failing stopped everything | step 19 never stops the steps; `adreno_zap=0` skips it |
| Their proof: a register written by command (`0x1337beef`) | memory write only | step 20 does the register test, step 21 the memory write |

They tried, then took out again as risky, writing the SMMU's `attach-impl-defs` and ACTLR and
`RBBM_SECVID_TRUST_CNTL`; those are not done here either.

## The GPU kept on: the SweetGPU service (patch 0090)

Status (2026-10-24): **written and compiled, not run on a phone.** With `adreno=21 adreno_keep=1`,
and all 21 steps done, nothing is undone: the 3D core stays powered and the kernel starts the
IOKit service `SweetGPU`. It speaks the interface of Linux's msm DRM driver, which Mesa's freedreno
uses (HackiOS's `HKGPU.cpp`, same request numbers and structures): parameters (chip ID 0x06010800,
512K GMEM at 1 MB, UBWC 2.0 with highest bank bit 14), buffers (made zeroed and write-combined,
given a GPU address, mapped into the program, given back), command submission and waiting for a
fence, and one request of its own (0x7e) that makes an IOSurface's memory a GPU buffer, for
drawing into what the display shows.

Left out for now (all in HackiOS's version): their cache of given-back buffers (a few ms a frame),
per-submission timing, resetting a hung core (here a hang is reported once and the GPU is given
nothing more), and the display waiting for a surface the GPU is still drawing.

Nothing uses it yet: that needs Mesa built for iOS with HackiOS's `hackios-drm` shim (service name
changed to `SweetGPU`) and loaded into backboardd.

## What comes after

Mesa (freedreno, A618 supported upstream) built for iOS with HackiOS's `hackios-drm` shim and
`eagl.c`, so CoreAnimation draws with OpenGL ES on the GPU. That needs an iOS build toolchain in CI
and a way to load the library into backboardd, which this port does not have yet.
