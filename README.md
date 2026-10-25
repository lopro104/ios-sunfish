#Not yet tested on the fish

# ios-sunfish

**Apple's iOS 16 on the Google Pixel 4a.**

This is a port of [ios-sweet](https://github.com/dipa-studio/ios-sweet). That project runs Apple's open-source XNU kernel (xnu-8796, iOS 16 generation) with the **unmodified iOS 16.7.16 userland of the iPhone X** on the Redmi Note 10 Pro. The Pixel 4a (`sunfish`) uses the same Qualcomm SM7150 chip. Most of that work therefore carries over, and this repository adds what differs on the Pixel.

> **Status (2026-10-24): ported, not yet tested on a Pixel 4a.** Everything in the sweet column of the table below has run on real hardware. The sunfish-specific parts (patches 0082-0095: board support, display refresh, touch, RTC, thermal, CPU and memory speed, battery, the GPU bring-up and the `SweetGPU` service, and the boot image) are written and build in CI, but nobody has tried them on a phone yet. The first-boot checklist is in [`docs/SUNFISH.md`](docs/SUNFISH.md). Reports are welcome.

> Experimental bring-up, not a usable phone OS. Apple's security model is mostly switched off. It installs into the `userdata` partition, so **it erases Android's user data**.

## Status

| Area | sweet (Redmi Note 10 Pro) | sunfish (Pixel 4a) |
| --- | --- | --- |
| Boot (`fastboot boot` shim → XNU) | works | untested; Pixel boot image layout and `0x80000` text offset handled |
| CPUs, memory, interrupts, timers | works (8 cores) | same SoC, same code |
| UFS storage, iOS root in `userdata` | works | same SoC, same code |
| Display | works (video-mode panel) | untested; command-mode panel, kept refreshing with MDP autorefresh |
| iOS GUI (SpringBoard, Settings, Control Center) | works | depends on the display |
| Touch | works (Goodix GT9896) | untested; new ST FTM5 driver path |
| Buttons (power, volume) | works | same PMIC wiring, same code |
| USB networking + live kernel log | works | same USB controller, same code |
| Wi-Fi, Bluetooth, cellular, audio, camera, sensors, GPU, SEP | no | no |

The table under "What does not work" in the sweet README applies here too. Rendering is CPU-only, the first boot takes about 4 minutes on the Apple logo, and there are few apps, no App Store, permissive code signing and a stub keystore.

## How it boots

```
Pixel ABL (unlocked bootloader)
  -> fastboot boot shim-boot.img          (temporary; nothing is flashed to boot_a/boot_b)
     -> shim: turns on display autorefresh, reads the live FDT, loads the payload from the ramdisk
        -> XNU Mach-O + generated Apple Device Tree (ADT) + boot-args
           -> XNU -> UFS driver -> HFS+ root in the userdata partition
              -> dyld + shared cache -> launchd -> backboardd + SpringBoard
```

iOS sees an iPhone X (`iPhone10,3`, 1125x2436). The Pixel's 1080x2340 panel shows the centred part of that screen.

## What is different from sweet

Both phones are SM7150. The CPUs, GIC, RAM layout, the splash framebuffer at `0x9c000000`, UFS, USB, the PM6150/PM6150L PMICs, the buttons and the touch I2C bus are shared, so **one kernel binary runs on both**. Only the boot shim and the device tree are built per phone.

| | sweet | sunfish | Where |
| --- | --- | --- | --- |
| Boot image | Xiaomi layout, text offset 0 | ramdisk `0x2000000`, tags `0x1e00000`; ABL requires Image text offset **`0x80000`**, an **LZ4** kernel and an **AVB hash footer** (64 MiB) | `DEVICE=sunfish shim/build.sh` |
| Panel | 1080x2400, video mode | 1080x2340 Samsung sofef01, **command mode**: the display controller only sends a frame when kicked | `shim/disp.c` enables pingpong autorefresh |
| Touch | Goodix GT9896 @ `0x5d` | in-cell **ST FTM5 @ `0x49`** | kernel patch `0082`, `sweet_touch.c` |
| Touch power | L11C via RPMh | panel rails (on from ABL) + load switch on PM6150 GPIO4 | `0082`, `sweet_spmi_write()` (limited to that pin) |
| Device tree | `xiaomi,sweet` | `google,sunfish`; the kernel picks board drivers by it | `tools/mkadt.py --device sunfish` |

Details and register-level notes are in [`docs/SUNFISH.md`](docs/SUNFISH.md).

## How to do it yourself

You need a Pixel 4a (`sunfish`, not the 4a 5G) with an **unlockable bootloader**. Carrier-locked models, Verizon's in particular, cannot be unlocked. You also need a Linux PC and a GitHub account for the kernel build. **Everything on `userdata` is lost.** The bootloaders, modem and persist partitions are never touched.

### 0. Unlock and back up

Enable *OEM unlocking* in Android's developer options, then run `fastboot flashing unlock`. This wipes the phone. Keep a copy of the stock `boot.img` from the factory image that matches your build. Partition backups and device dumps stay out of git; `.gitignore` excludes `backup/`, `dtb/` and `recovery/`.

The shim needs the stock DTB:

```sh
python3 tools/unpack_bootv2.py backup/boot.img dtb/sunfish   # -> dtb/sunfish/dtb
```

### 1. Build the kernel

The kernel builds on GitHub's macOS runners with [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build). The workflow fetches xnu-8796 and applies `xnu/patches/` in order. It also stages the pinned open-source libpthread, HFS and BearSSL sources.

```sh
gh workflow run xnu.yml -R YOU/ios-sunfish
gh run download RUN_ID -R YOU/ios-sunfish -n xnu-kernel -D xnu/out
```

The output is `kernel.development.sweet`. The same binary is used for both phones.

### 2. Build the iOS root image

Same as on sweet. Download the iPhone X (iPhone10,3) iOS 16.7.16 (20H392) IPSW from Apple, then on Linux, with `apfs-fuse`, `hfsprogs` and sudo:

```sh
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios16-root.img 16G
sudo fsck.hfsplus -fy ios16-root.img
sudo mount -t hfsplus -o loop,force ios16-root.img /mnt/iosroot
sudo python3 tools/sweet-root-tweaks.py /mnt/iosroot
sudo umount /mnt/iosroot
img2simg ios16-root.img ios16-root.simg
```

`mkiosroot.sh` merges the OS volume and both cryptexes into one case-sensitive journaled HFS+ volume. `sweet-root-tweaks.py` applies the bring-up changes; its header explains each one. The device tree builder also needs `/product` from the real iPhone X device tree:

```sh
python3 tools/remotezip.py IPSW_URL DeviceTree.d22ap.im4p research/ipsw-x/dt
ipsw img4 im4p extract research/ipsw-x/dt/Firmware/all_flash/DeviceTree.d22ap.im4p -o research/ipsw-x/d22ap.adt
```

### 3. Build the boot image

Needs `aarch64-linux-gnu-gcc`, `lz4` (Fedora: `sudo dnf install lz4`), and AOSP's `mkbootimg` and `avbtool`. Any Android
ROM tree has both (`system/tools/mkbootimg/mkbootimg.py`, `external/avb/avbtool.py`); the script uses them from
`~/evox17` when they are not installed (`ANDROID_TREE=` for another tree, or `MKBOOTIMG=` / `AVBTOOL=` for single files). Without a ROM tree,
LineageOS's two repositories do:

```sh
git clone --depth 1 -b lineage-23.0 https://github.com/LineageOS/android_system_tools_mkbootimg tools-android/system/tools/mkbootimg
git clone --depth 1 -b lineage-23.0 https://github.com/LineageOS/android_external_avb tools-android/external/avb
ANDROID_TREE=$PWD/tools-android DEVICE=sunfish shim/build.sh
```
Google's bootloader refuses `fastboot boot` of a kernel that is not LZ4-compressed or an image without an AVB
footer (found on the Pixel 4 by [ios-flame](https://github.com/NWylynko/ios-flame)); `DEVICE=sunfish` does both.

```sh
python3 tools/mkadt.py shim/out/sunfish.adt --highmem --device sunfish
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/sunfish.adt shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=6 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30'
DEVICE=sunfish shim/build.sh ../shim/out/payload.bin      # -> shim/out/shim-boot.img
```

**First time on a Pixel:** boot the shim *without* a payload first (`DEVICE=sunfish shim/build.sh`). It draws diagnostics on a red screen. If the lines keep appearing one after another, the display refresh works. The `DISP` lines show which buffer the display controller scans out. See [`docs/SUNFISH.md`](docs/SUNFISH.md#to-check-on-first-boot).

| Boot-arg | Effect |
| --- | --- |
| `rd=ufsroot0 ufsrw=1 rootrw=1` | Root on the `userdata` partition, writable |
| `cpus=6` | Start the six A55 cores (`cpus=8` adds the A76s, `cpus=1` is the safe fallback) |
| `launchd_unsecure_cache=1` | launchd accepts its job cache without AMFI |
| `wdt=-1 wdt_test=no_user_checkin` | No hardware watchdog |
| `sweetfb_noconsole=1` | Stop drawing kernel text over the GUI (the log is kept; panics are drawn) |
| `sweetfb_hz=30` | Rate of the CPU copy into the framebuffer |
| `sweettouch=0`, `sweetkeys=0` | Disable the touch or button driver |
| `sweetusb=2` | USB networking with the live kernel log (`tools/sweet-usbnet.sh`) |

### 4. Flash the root and boot

```sh
fastboot -S 512M flash userdata ios16-root.simg
fastboot boot shim/out/shim-boot.img
```

To get back to the bootloader, hold Power + Volume down. Android comes back once you flash a factory image (or just format `userdata`).

### 5. Read the kernel log

The kernel keeps a 4 MiB log ring at the end of `userdata`. Boot a recovery with adb root access (TWRP for sunfish) temporarily and pull it:

```sh
fastboot boot recovery/twrp-sunfish.img
tools/sweet-log-pull.sh logs/boot.txt
```

With `sweetusb=2` the log also streams live over USB (`tools/sweet-usbnet.sh logs/live.txt`). `tools/sweet-crash.py` symbolicates crash reports against the shared cache.

## Repository layout

| Path | What |
| --- | --- |
| `shim/` | Bare-metal boot shim packed as an Android boot.img v2 (`DEVICE=sweet` or `sunfish`); `disp.c` is the command-mode display refresh |
| `xnu/patches/` | The XNU patch series, applied in order by CI; `0082` is the sunfish board support |
| `.github/workflows/xnu.yml` | Kernel CI build |
| `tools/mkadt.py` | Generates the Apple Device Tree (`--device sunfish`) |
| `tools/mkpayload.py` | Packs kernel + ADT + boot-args into the shim payload |
| `tools/mkiosroot.sh`, `sweet-root-tweaks.py` | Build the HFS+ iOS root and apply the bring-up changes |
| `tools/sweet-log-pull.sh`, `sweet-usbnet.sh`, `sweet-log.py`, `sweet-crash.py` | Kernel log over recovery or USB, crash symbolication |
| `tools/qemu-sweet.py` | QEMU test boots for kernel-only changes |
| `docs/SUNFISH.md` | Pixel 4a hardware differences and the first-boot checklist |
| `NOTES.md` | Engineering log (mostly the sweet bring-up), plus the current handoff |

Many names still say `sweet` (drivers, boot-args, the `SWEET` machine config). They come from the original port and are shared by both phones.

## Kernel patches

| # | What |
| --- | --- |
| 0001–0018 | Toolchain compatibility, machine config, non-PAC arm64, stubs, early framebuffer console, device-tree platform expert, timer, boot CPU |
| 0019–0034 | libpthread, BearSSL, RAM-disk and UFS root, minimal AMFI, HFS+, DMA above 4 GiB |
| 0035–0057 | Entitlements, AppleKeyStore stub, watchdog, crash reports, IOSurface + IOMobileFramebuffer, persistent log, sandbox policy |
| 0058–0071 | HID services, iPhone X identity, keys and touch, display size, battery, SMP, home gesture |
| 0072–0081 | USB device mode, CDC-ECM networking and live log, 8 cores, display power, network trace |
| 0082 | Pixel 4a (`sunfish`): ST FTM5 touch, PM6150 GPIO4 touch power, panel size from the boot framebuffer, `google,sunfish` platform match |
| 0083 | Adreno 618 bring-up probes, steps 1-10 (`adreno=N`), ported from HackiOS; see [`docs/GPU.md`](docs/GPU.md) |
| 0084 | PM6150 RTC as the calendar (`sweetrtc_offset=`), no more 30 s IORTC wait at boot; sunfish display fallback: autorefresh re-armed by the kernel, else `CTL_START` after every frame |
| 0085 | Thermal service (watch and report): all 25 TSENS sensors every 250 ms, iOS pressure level in `kern.hackios.thermal_pressure`; ported from HackiOS (`metal` branch) |
| 0086 | CPU speed (SM7150 OSM): `cpufreq=2` reads the speed tables, `cpufreq=1` raises the cores to their top speed; the thermal service then slows them when hot. Ported from HackiOS |
| 0087 | Memory bandwidth vote through RPMh (`ddr=`, on with `cpufreq=1`), battery temperature from the PM6150 ADC into the thermal service; ported from HackiOS |
| 0088 | Battery limit set to Google's sunfish value (60 C) instead of HackiOS's 45/50/58 C |
| 0089 | Adreno 618 bring-up steps 11-21: TrustZone and the zap shader, the (legacy) GMU started and talked to, the 3D core powered, its command processor started and writing to memory; firmware from `/fw/adreno/` (docs/GPU.md) |
| 0090 | `SweetGPU`: with `adreno=21 adreno_keep=1` the GPU stays on and programs get it through Linux's msm DRM interface (what Mesa's freedreno speaks); ported from HackiOS's `HKGPU` |
| 0091 | Battery: the Pixel 4a's 3140 mAh and Google's voltage curve, battery temperature from the thermistor, PMIC reading on by default on sunfish |
| 0092 | GPU bring-up corrected with hardware findings from ios-surya (same SoC and GPU): SMMU clock vote, safe clock order, no zap shader needed, register test (docs/GPU.md) |
| 0093 | From [ios-flame](https://github.com/NWylynko/ios-flame) (Pixel 4): early framebuffer as Device memory (no speculative reads past it), planar IOSurface bytes-per-row (blank Safari pages), vsync refresh period for Core Animation, AMFI answers dyld's policy query (third-party apps with frameworks load) |
| 0094 | From [Bergamota](https://github.com/ry-diffusion/Bergamota) and [ios-topaz](https://github.com/MakrSas/ios-topaz): sandbox extensions (TCC grants such as Photos work), the container query answered, IOSurface views released at the right trap (a backboardd crash), an empty `IOAcceleratorES` (no 30 s wait in Core Animation at every backboardd start) |
| 0095 | From ios-topaz: IOSurface calls that would hand out a zeroed client refused (a Core Animation crash), and IOKit allowed to quiesce (configd, Wi-Fi setup and SpringBoard waited for it) |
| 0096 | A UFS start-up failure now puts its cause (host registers, last SCSI command and its status) into the panic text, which is on screen after the console lines scroll away |
| 0097 | Pixel 4a first boot: TEST UNIT READY answered CHECK CONDITION three times. The UFS driver now reads the sense key, starts a not-ready unit (START STOP UNIT), retries ten times and prints sense key/asc/ascq in the panic |
| 0098 | The Pixel bootloader leaves the UFS device powered down (sense: NOT READY, initializing command required). The driver now sends REQUEST SENSE then START STOP UNIT (Active) to the UFS Device well-known LU (0xD0), as the Pixel kernel's ufshcd_set_dev_pwr_mode does |
| 0099 | Battery board check reads the device tree root (IODeviceTree:/), not the registry root: on the Pixel the sunfish profile (3140 mAh, PM6150 fuel gauge polling) was never selected, so iOS saw a fixed 100%, charging |
| 0100 | Pixel battery percentage as the mainline sunfish qcom_qg driver computes it: voltage corrected by I x 150 mOhm, charge counted from the current every 5 s (pulled back to the voltage estimate while idle), shown value moves at most 1% per 20 s and never against the current |
| 0101 | GPU firmware from the boot image: `mkadt.py --firmware DIR` puts the GMU firmware, SQE microcode and zap shader into the device tree (`/sweet-fw`); the GPU steps read it there before `/fw/adreno/` on the root |
| 0102 | GPU SMMU stream matches without the downstream mask 0x400 (first Pixel run: the hypervisor dropped SMR 0x84000005 at step 13); exact matches as in the mainline sm7150.dtsi and ios-surya |

## Key hardware facts (sunfish)

- SoC SM7150-AB: 6x Cortex-A55 + 2x Cortex-A76, GICv3, PSCI over `smc`, 19.2 MHz timer, no pointer authentication.
- RAM: the payload uses the upper bank, `0x100000000` + `0xfcc00000`.
- Panel: 1080x2340 Samsung sofef01 OLED, DSI command mode, continuous splash at `0x9c000000` (a8r8g8b8, stride 4320).
- Touch: ST FTM5 at I²C `0x49` on QUP SE7 (`0xa84000`), reset GPIO 8, IRQ GPIO 9, IRQ route switch GPIO 72, power switch PM6150 GPIO4.
- Buttons: power and volume down on the PM6150 PON block, volume up on PM6150L GPIO2.
- Storage: UFS at `0x1d84000`, `userdata` on LUN 0.

Sources: the device trees and drivers in Google's `msm-4.14` kernel, the mainline `sm7150-google-sunfish.dts`, and `device/google/sunfish`.

## Credits

Built on [ios-sweet](https://github.com/dipa-studio/ios-sweet) and Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs). Also uses [BearSSL](https://bearssl.org), [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) and [ipsw](https://github.com/blacktop/ipsw). Hardware details come from Google's and Qualcomm's published kernel sources. iOS itself is Apple's and is not distributed here.
