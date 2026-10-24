# Porting notes: sunfish (Google Pixel 4a)

Status (2026-10-21): **ported, not yet booted on hardware.** The shim builds for
sunfish. The device tree generator and the kernel patch `0082` are written. The
first boot on the phone has to confirm the points in "To check on first boot".

## Why it ports at all

sweet (Redmi Note 10 Pro, SM7150-AC) and sunfish (Pixel 4a, SM7150-AB) use the
same SoC. These parts are identical, and the existing code works unchanged:

| Block | Address / fact | Shared code |
| --- | --- | --- |
| CPUs | 6x A55 + 2x A76, MPIDR Aff1 = core, PSCI over `smc` | shim, 0065/0078 |
| GICv3, timer | GICD `0x17a00000`, GICR `0x17a60000`, vtimer PPI 19, 19.2 MHz | mkadt, 0017 |
| RAM | 6 GiB; upper bank `0x1_00000000` is free of carve-outs | `mkadt.py --highmem` |
| Splash framebuffer | `0x9c000000`, a8r8g8b8, stride 4320, region `0x1700000` | shim, fbcon, AppleCLCD |
| UFS | `ufshc@1d84000`, `userdata` on LUN 0 | `sweet_ufs.c` |
| USB | DWC3 at `0xa600000`, QSCRATCH `0xa6f8800` | `sweet_usb.c` |
| SPMI / PMICs | PM6150 + PM6150L, arbiter v5 at `0xc440000` | `sweet_spmi.c`, `sweet_keys.c` |
| Keys | power + volume down on the PM6150 PON, volume up on PM6150L GPIO2 | `sweet_keys.c` |
| Touch bus | QUP SE7 I2C (`0xa84000`), GPIO 6/7, reset GPIO 8, IRQ GPIO 9 | `sweet_touch.c` |

Sources: `linux` (`arch/arm64/boot/dts/qcom/sm7150-google-sunfish.dts` next to
`sm7150-xiaomi-sweet.dts`), `android_kernel_google_msm-4.14`
(`arch/arm64/boot/dts/google/sm7150-sunfish-*.dtsi`,
`drivers/input/touchscreen/fts_touch_s5`), and `android_device_google_sunfish`
(`BoardConfig-common.mk`, `fstab.hardware`).

## What is different, and how it is handled

| | sweet | sunfish | Change |
| --- | --- | --- | --- |
| Panel | 1080x2400 **video mode** (MDP scans RAM continuously) | 1080x2340 Samsung sofef01 **command mode** (MDP sends a frame only when kicked) | `shim/disp.c`: enable the pingpong's autorefresh (`PP_AUTOREFRESH_CONFIG = BIT(31) \| 1`) on every tear-checked pingpong, so every TE pulse re-sends the framebuffer. The downstream SDE driver uses the same mechanism instead of `CTL_START`. Panel height comes from `FB_H` (shim) → boot_args → AppleCLCD / SweetHIDTouch |
| Touch IC | Goodix GT9896 at `0x5d` | ST FTM5 at `0x49`, in-cell | `0082`: `sweet_touch.c` picks the FTM5 path when the ADT root says `google,sunfish`. Reset pin, wait for `CONTROLLER_READY` (`0x03`), scan mode `{a0 00 ff}`, events read 8 bytes at a time from `0x86` (enter `0x13`, motion `0x23`, leave `0x33`). Coordinates are 1:1 panel pixels (`st,max-coords = <1079 2339>`) |
| Touch power | L11C through RPMh + GPIO 90 | Panel rails L13A / L7C (on from ABL) + a load switch on **PM6150 GPIO4** | `0082`: `sweet_spmi_write()`, allowed only for PM6150 GPIO4 config registers (`0xc340..0xc346`), sets it to output high if ABL didn't already. TLMM GPIO 72 is driven low (touch IRQ to the AP, not the SLPI) |
| Boot image | Xiaomi layout, Image text offset 0 | `BoardConfig-common.mk`: ramdisk `0x02000000`, tags `0x01e00000`, header v2, DTB in boot.img. The Pixel ABL **rejects any Image header text offset other than `0x80000`** ("Kernel TextOffset does not match"; see the note in the mainline `head.S`) | `DEVICE=sunfish shim/build.sh` (`-DTEXT_OFFSET=0x80000` in `start.S`; the shim is position independent) |
| Platform expert match | `xiaomi,sweet` | `google,sunfish` | `0082`: `IONameMatch = ('xiaomi,sweet', 'google,sunfish')`; `mkadt.py --device sunfish` |
| Digitizer surface | 68x151 mm | 62x134 mm | `0082`: SweetHIDTouch picks it from the panel height |

The kernel binary is the same for both phones (machine config `SWEET`). Only the
shim image and the ADT are per device.

Presented display: iOS still sees the iPhone X panel (1125x2436). The 1080x2340
panel shows its centred part: 22 px are cropped at each side, 48 px at the top
and the bottom (the status bar and home indicator sit inside that safe area).

## Build

Same as the README, with the device switches:

```sh
# stock DTB from the phone's own boot.img (factory image or a backup)
python3 tools/unpack_bootv2.py backup/boot.img dtb/sunfish          # -> dtb/sunfish/dtb

python3 tools/mkadt.py shim/out/sunfish.adt --highmem --device sunfish
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/sunfish.adt shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=6 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30'
DEVICE=sunfish shim/build.sh ../shim/out/payload.bin                # -> shim/out/shim-boot.img
```

The iOS root image is the same as on sweet (iPhone X 16.7.16). Flash it to
`userdata` with `fastboot flash userdata ios16-root.simg`. That **erases Android's user data**. The
bootloader must be unlocked (`fastboot flashing unlock`). Then run
`fastboot boot shim/out/shim-boot.img`. Nothing is flashed to `boot_a`/`boot_b`.

Read the persistent log from TWRP for sunfish (adb root) with
`tools/sweet-log-pull.sh`. It finds `userdata` by name.

## To check on first boot

Try these steps in order. Each one can be tested without the steps after it.

1. **Shim without payload** (`DEVICE=sunfish shim/build.sh`, no argument): the
   red screen must **update** (UFS probe lines appear one by one). The `DISP`
   lines show:
   - `DISP SRC addr=9C000000 size=09240438`: ABL scans out our framebuffer
     (size = 2340 << 16 | 1080). A `NOT FB!` line means ABL used another buffer.
     Then set `FB_BASE` from it.
   - `DISP PP n=… ar now=80000001`: autorefresh is on. If the line says `no
     tear-checked pingpong`, ABL left the panel in video mode (unexpected) or
     the display was off. If the screen freezes on the first frame, autorefresh
     did not take. The fallback is to kick `CTL_START` (CTL base + `0x1c`) after
     each frame, in the shim's console and in AppleCLCD's present path.
2. **Kernel**: the early console and the Apple logo phase, then the UFS line
   `"userdata" on LUN 0`.
3. **Touch**: log lines `SWEET TOUCH: PM6150 GPIO4 …`, `FTM5 0x49 err=0
   ready=1`, `scan mode active err=0`. With `err=6` (ENXIO, no ACK), check the
   load switch and the reset pin. With `ready=0` and `err=0`, the IC answered
   but no READY event came. Sensing is still turned on.
4. **Keys and USB**: these should behave as on sweet (same PMIC and the same
   DWC3 wrapper).

## Clock, battery and display fallback (patch 0084)

- **Clock.** The kernel reads the PM6150 RTC for the calendar. Before 0084, every boot waited 30 s
  for a clock driver that never came and then started at 1970. Android's kernel can't write this
  RTC on sunfish (`qcom,qpnp-rtc-write = <0>`), so the raw count is not the real time: Android
  keeps the difference itself. Get it from Android once, while it still boots, and pass it as a
  boot-arg:

  ```sh
  adb shell 'echo $(( $(date +%s) - $(cat /sys/class/rtc/rtc0/since_epoch) ))'
  # -> e.g. 1761000000 ; add  sweetrtc_offset=1761000000  to the --cmdline
  ```

  The difference only changes if the RTC loses power (battery fully drained). Without the
  boot-arg, the date is wrong but the boot is still 30 s faster.
- **Battery and charger.** Add `sweetbattery=1` to read the real voltage-based charge, the
  charging state and USB presence from the PM6150 (the same chip as on sweet). Without it, the
  battery shows a static 100 %.
- **Display fallback.** If the shim's autorefresh did not take, the kernel tries again when the
  display service starts; if it still does not take, it sends `CTL_START` after every frame it
  copies to the panel. Look for `SWEET FB: command-mode pingpong` in the log. `sweetfb_kick=0`
  turns the fallback off.

## Temperatures (patch 0085)

The kernel reads all 25 of the SoC's temperature sensors (CPU cores, GPU, memory, modem area, camera,
video, ...) every 250 ms. It is ported from HackiOS's thermal service (`metal` branch,
`hk_thermal.c`). It is on by default on sunfish (`thermal=0` turns it off) and only reads: the
sensors' own thresholds and the SoC's hardware protection stay as the firmware set them.

- In the log: `SWEET THERMAL: first reading: aoss-0=34.5 cpu-0-0=...` at boot, a line whenever the
  heat level changes, and every sensor once a minute.
- For iOS: `kern.hackios.thermal_pressure` (0 nominal ... 4 sleeping), the same names HackiOS uses,
  so their `thermald` can pass it to apps. `sysctl kern.hackios.thermal` prints the whole table.
- What it does **not** do yet: slow the cores when hot. That needs a CPU frequency driver for the
  SM7150 (its OSM block), which this kernel doesn't have. The cores run at the speed ABL left them.

## CPU speed (patch 0086)

The bootloader leaves the cores at a modest speed. The SM7150's speed block (OSM) has a table of
speeds per group of cores, built by the firmware, and switches clock and voltage by itself when
told which row to use. Ported from HackiOS (`hk_cpufreq.c`, `metal` branch); their chip has a
newer block, so the registers here are the SM7150's (Linux `qcom-cpufreq-hw`).

| Boot-arg | Effect |
| --- | --- |
| *(none)* | Nothing touched; the cores stay at the bootloader's speed |
| `cpufreq=2` | Read only: each group's speed table and current speed go to the log (`SWEET CPUFREQ:`). **Try this first.** |
| `cpufreq=1` | Each group with one of our cores (see `cpus=N`) goes to its top speed (A55s and, with `cpus=8`, the A76s). The thermal service then slows them a row at a time when a sensor passes 95 C, and ends the run with a panic after 2 s past 110 C with everything at the lowest speed (as HackiOS does). |

The log also shows how fast a simple loop runs before and after: the speed-up, in one number.

## Memory speed and battery temperature (patch 0087)

- **Memory speed.** Drawing everything on the CPU is memory-heavy. As HackiOS does, the kernel asks
  RPMh for the most memory bandwidth there is: a vote on the memory controller's and the shared
  hub's bus clock managers (MC0, SH0) and on ACV for the application processor. HackiOS measured
  12.4 GB/s writing before and 15.4 GB/s after on their phone.

  | Boot-arg | Effect |
  | --- | --- |
  | *(default)* | The vote happens with `cpufreq=1`, otherwise not |
  | `ddr=1` | Only look the bus clock managers up and log where they are |
  | `ddr=2` | Vote (also without `cpufreq=1`); `ddr_vote=N` (1..16383) instead of the most |
  | `ddr=0` | Never |

  The vote stays until the phone restarts. Log lines: `SWEET RPMH: DDR bandwidth ...`.
- **Battery temperature.** Read once a second from the PM6150's ADC (the battery thermistor, as
  Android's driver reads it) and given to the thermal service. One limit, Google's own for sunfish
  (`thermal_info_config_sunfish.json`, where Android shuts down at 60 C): from 60 C the cores go to
  their lowest speed and the heat level is "trapping"; below it the battery changes nothing (0088;
  HackiOS's 45/50/58 C, used in 0087, were stricter than Google's).
  A thermistor that reads off the table (open or shorted) counts as no reading, so a broken sensor
  can't slow the phone. `sysctl kern.hackios.battery_temperature_tenths`.

## Battery (patch 0091)

The battery service (`SweetBattery.cpp`) had the Redmi's 5020 mAh battery and a generic voltage curve
built in. On `google,sunfish` it now uses the Pixel 4a's: 3140 mAh, and Google's own curve of open-circuit
voltage against charge at 25 C (msm-4.14 `batterydata-qcom-s5-SWD-ATL.dtsi`; full is 4.45 V, not 4.35).
Reading the PMIC is on by default on the Pixel (`sweetbattery=0` turns it off; it only reads). The
temperature iOS sees is the battery thermistor's, from the thermal service, instead of a fixed 25 C.

It is still an estimate from the voltage alone: under load the voltage sags and the percentage reads low,
while charging it reads high. Google's driver tracks charge going in and out over time (Qualcomm's QG
algorithm), which is not ported. Log line: `SWEET BATTERY: google,sunfish battery, 3140 mAh; PM6150 polling on`.
