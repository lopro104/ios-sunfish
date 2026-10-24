#!/bin/bash
# Build the shim and wrap it in a boot.img v2 that mirrors the stock header.
#   DEVICE=sweet   (default) Redmi Note 10 Pro, stock DTB from dtb/boot_unpacked/dtb
#   DEVICE=sunfish Pixel 4a,  stock DTB from dtb/sunfish/dtb
# DTB=path overrides the stock DTB location.
# sunfish: Google's ABL takes `fastboot boot` only of an LZ4-compressed kernel
# with an AVB hash footer (found on the Pixel 4 by ios-flame, NOTES.md "Google
# ABL boot image rules"; sunfish's BoardConfig has Image.lz4 and a 64 MiB boot
# partition). Needs lz4 and AOSP's avbtool: AVBTOOL=path, else avbtool on PATH,
# else ~/evox17/external/avb/avbtool.py (an Android tree).
# mkbootimg likewise: MKBOOTIMG=path, else mkbootimg on PATH, else the Android
# tree's system/tools/mkbootimg/mkbootimg.py. ANDROID_TREE=path changes ~/evox17.
set -euo pipefail
cd "$(dirname "$0")"
DEVICE=${DEVICE:-sweet}
ANDROID_TREE=${ANDROID_TREE:-$HOME/evox17}
if [ -z "${MKBOOTIMG:-}" ]; then
	if command -v mkbootimg >/dev/null; then
		MKBOOTIMG=mkbootimg
	elif [ -f "$ANDROID_TREE/system/tools/mkbootimg/mkbootimg.py" ]; then
		MKBOOTIMG="python3 $ANDROID_TREE/system/tools/mkbootimg/mkbootimg.py"
	else
		echo "error: mkbootimg not found: install it, or set MKBOOTIMG= or ANDROID_TREE= (an Android tree)" >&2
		exit 1
	fi
fi
LZ4_KERNEL=0
AVB_PARTITION_SIZE=
CC=aarch64-linux-gnu-gcc
OBJCOPY=aarch64-linux-gnu-objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
case "$DEVICE" in
sweet)
	# 1080x2400 video-mode panel; Xiaomi/PixelOS boot.img layout.
	DTB=${DTB:-../dtb/boot_unpacked/dtb}
	MKBOOTIMG_LAYOUT="--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 --tags_offset 0x100 --dtb_offset 0x1f00000"
	;;
sunfish)
	# 1080x2340 command-mode panel (disp.c); BoardConfig-common.mk layout.
	# Pixel ABL rejects an Image header text_offset other than 0x80000.
	CFLAGS="$CFLAGS -DFB_H=2340 -DCMD_MODE_PANEL -DTEXT_OFFSET=0x80000"
	DTB=${DTB:-../dtb/sunfish/dtb}
	MKBOOTIMG_LAYOUT="--base 0 --kernel_offset 0x8000 --ramdisk_offset 0x2000000 --tags_offset 0x1e00000 --dtb_offset 0x1f00000"
	LZ4_KERNEL=1
	AVB_PARTITION_SIZE=67108864     # BOARD_BOOTIMAGE_PARTITION_SIZE 0x04000000
	;;
*)
	echo "error: unknown DEVICE=$DEVICE (sweet or sunfish)" >&2
	exit 1
	;;
esac
OBJS="start shim console mmu fdt loader ufs disp"
rm -rf out/*.o
mkdir -p out
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c disp.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$(aarch64-linux-gnu-readelf -r $(for o in $OBJS; do [ $o = start ] || echo out/$o.o; done) | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	$(for o in $OBJS; do echo out/$o.o; done) -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
KERNEL=out/Image
if [ "$LZ4_KERNEL" = 1 ]; then
	command -v lz4 >/dev/null || { echo "error: lz4 not found (Fedora: sudo dnf install lz4)" >&2; exit 1; }
	# An LZ4 frame, as stock and TWRP kernels are (ios-flame: lz4 -9 -B7).
	lz4 -q -9 -B7 -f out/Image out/Image.lz4
	KERNEL=out/Image.lz4
fi
# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio (ABL dislikes a zero-size ramdisk on some builds) -> diagnostics.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
fi
[ -f "$DTB" ] || { echo "error: stock DTB $DTB missing (see README: unpack the stock boot.img)" >&2; exit 1; }
# shellcheck disable=SC2086 # MKBOOTIMG may be "python3 path"; MKBOOTIMG_LAYOUT is a list of arguments
$MKBOOTIMG --header_version 2 --kernel "$KERNEL" --ramdisk out/ramdisk.bin \
	--dtb "$DTB" $MKBOOTIMG_LAYOUT --pagesize 4096 \
	--os_version 17.0.0 --os_patch_level 2035-06 \
	--cmdline "console=null" -o out/shim-boot.img
if [ -n "$AVB_PARTITION_SIZE" ]; then
	AVBTOOL=${AVBTOOL:-$(command -v avbtool || true)}
	[ -n "$AVBTOOL" ] || [ ! -f "$ANDROID_TREE/external/avb/avbtool.py" ] || AVBTOOL="$ANDROID_TREE/external/avb/avbtool.py"
	[ -n "$AVBTOOL" ] || { echo "error: avbtool not found: set AVBTOOL=/path/to/avbtool.py (AOSP external/avb)" >&2; exit 1; }
	# Unsigned (algorithm NONE); ABL only wants the footer to be there on an unlocked phone.
	python3 "$AVBTOOL" add_hash_footer --image out/shim-boot.img \
		--partition_size "$AVB_PARTITION_SIZE" --partition_name boot
fi
# Host render of the console layout.
gcc -O1 -Wall $(echo "$CFLAGS" | grep -o -- '-DFB_H=[0-9]*' || true) -o out/host_test console.c host_test.c && ./out/host_test
echo "DEVICE=$DEVICE"
ls -l "$KERNEL" out/shim-boot.img
