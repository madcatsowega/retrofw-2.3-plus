#!/usr/bin/env bash
set -euo pipefail

BASE="/home/sean/retrofw-2.4"
BOOT4M="$BASE/boot-assets/rs97plus-boot-4m.bin"
MININIT="$BASE/boot-assets/mininit-syspart"
UIMAGE="/home/sean/retrofw-kernel-plus/arch/mips/boot/uImage"
ROOTFS="$BASE/rootfs-2.4.squashfs"
DIST="$BASE/release"
MTOOLS_BIN="/home/sean/retrofw-dev/sdk-2.3/bin"

IMG="$DIST/RetroFW-2.3+-RS97Plus.img"
IMG_XZ="$DIST/RetroFW-2.3+-RS97Plus.img.xz"
UPG_ZIP="$DIST/RetroFW-2.3+-RS97Plus-Upgrade.zip"
P1_IMG="$DIST/.boot_p1.vfat"

export PATH="$MTOOLS_BIN:/sbin:/usr/sbin:$PATH"

for f in "$BOOT4M" "$MININIT" "$UIMAGE" "$ROOTFS"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: missing required file: $f" >&2
        exit 1
    fi
done

mkdir -p "$DIST"
rm -f "$IMG" "$IMG_XZ" "$UPG_ZIP" "$P1_IMG" "$DIST/SHA256SUMS.txt"

echo "=== 1. Creating 136 MiB (278,528-sector) RS-97 Plus SD image ==="
# Official RetroFW sector geometry:
#   Sectors     0..8191   (0..4 MiB)   : MBR + SPL + U-Boot + RetroFW splash
#   Sectors  8192..16383  (4..8 MiB)   : Kernel+ #12 uImage (max 3 MiB U-Boot window)
#   Sectors 16384..278527 (8..136 MiB) : Partition 1 (128 MiB FAT32 'rootfs')
# On first boot, /boot/.prsz + /boot/.defl automatically append:
#   Partition 2 (start=278528, size=128 MiB, Linux swap)
#   Partition 3 (start=540672, remaining card, FAT32 'RETROFW')
truncate -s $((278528 * 512)) "$IMG"

echo "=== 2. Writing RS-97 Plus bootloader (0..4 MiB) & Kernel+ #12 uImage (4 MiB) ==="
dd if="$BOOT4M" of="$IMG" bs=512 count=8192 conv=notrunc status=none
UIMAGE_SIZE=$(stat -c%s "$UIMAGE")
if [ "$UIMAGE_SIZE" -gt $((3 * 1024 * 1024)) ]; then
    echo "ERROR: uImage ($UIMAGE_SIZE bytes) exceeds 3 MiB U-Boot read window!" >&2
    exit 1
fi
dd if="$UIMAGE" of="$IMG" bs=512 seek=8192 conv=notrunc status=none

echo "=== 3. Writing clean single-partition DOS MBR (p1: 16384..278527, FAT32 LBA, bootable) ==="
python3 - "$IMG" << 'PYEOF'
import struct, sys
img_path = sys.argv[1]
with open(img_path, "r+b") as f:
    mbr = bytearray(f.read(512))
    # Preserve bytes 0..445 (boot code + disk signature), replace partition table cleanly
    # Partition 1: active=0x80, CHS=FE/FF/FF, type=0x0c (W95 FAT32 LBA), lba_start=16384, sectors=262144
    p1 = struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", 0x0c, b"\xfe\xff\xff", 16384, 262144)
    mbr[446:462] = p1
    mbr[462:510] = b"\x00" * 48
    mbr[510:512] = b"\x55\xaa"
    f.seek(0)
    f.write(mbr)
PYEOF
sfdisk -l "$IMG"

echo "=== 4. Building 128 MiB FAT32 /boot partition (rootfs) with first-boot triggers ==="
truncate -s $((262144 * 512)) "$P1_IMG"
mkfs.vfat -F 32 -n "rootfs" "$P1_IMG" >/dev/null

# Populate partition 1 via mtools (no root/mount required)
mmd -i "$P1_IMG" ::/dev ::/root
mcopy -i "$P1_IMG" "$MININIT" ::/mininit-syspart
mcopy -i "$P1_IMG" "$ROOTFS" ::/rootfs.squashfs

# RetroFW official first-boot partition expansion & formatting triggers
TMP_EMPTY=$(mktemp)
mcopy -i "$P1_IMG" "$TMP_EMPTY" ::/.prsz
mcopy -i "$P1_IMG" "$TMP_EMPTY" ::/.defl
rm -f "$TMP_EMPTY"

dd if="$P1_IMG" of="$IMG" bs=512 seek=16384 conv=notrunc status=none
rm -f "$P1_IMG"

echo "=== 5. Byte-for-byte Verification of Assembled SD Image ==="
UIMAGE_MD5=$(md5sum "$UIMAGE" | awk '{print $1}')
IMG_UIMAGE_MD5=$(dd if="$IMG" bs=1 skip=$((8192 * 512)) count="$UIMAGE_SIZE" status=none | md5sum | awk '{print $1}')
echo "  uImage MD5 expected: $UIMAGE_MD5"
echo "  uImage MD5 in .img : $IMG_UIMAGE_MD5"
[ "$UIMAGE_MD5" = "$IMG_UIMAGE_MD5" ] || { echo "ERROR: uImage MD5 mismatch!" >&2; exit 1; }

ROOTFS_MD5=$(md5sum "$ROOTFS" | awk '{print $1}')
IMG_ROOTFS_MD5=$(mcopy -i "$IMG@@$((16384 * 512))" ::/rootfs.squashfs - | md5sum | awk '{print $1}')
echo "  rootfs MD5 expected: $ROOTFS_MD5"
echo "  rootfs MD5 in .img : $IMG_ROOTFS_MD5"
[ "$ROOTFS_MD5" = "$IMG_ROOTFS_MD5" ] || { echo "ERROR: rootfs MD5 mismatch!" >&2; exit 1; }

echo "  Partition 1 contents:"
mdir -a -i "$IMG@@$((16384 * 512))" ::/

echo "=== 6. Compressing flashable .img.xz (xz -9 -T0) ==="
xz -9 -T0 -k "$IMG"
rm -f "$IMG"

echo "=== 7. Packaging in-place Upgrade ZIP (for existing RetroFW 2.3 RS-97 Plus installs) ==="
UPG_STAGE=$(mktemp -d)
cp -f "$UIMAGE" "$UPG_STAGE/uImage"
cp -f "$ROOTFS" "$UPG_STAGE/rootfs.squashfs"
cat << 'EOF' > "$UPG_STAGE/UPGRADE.txt"
RetroFW 2.3+ In-Place Upgrade Pack (RS-97 Plus / Y030XX067A / hwvariant=5b)
===========================================================================

This upgrades an existing RetroFW 2.3 RS-97 Plus SD card WITHOUT wiping your
ROMs, saves, or apps on the RETROFW data partition!

Option A — From a PC (SD card reader):
  1. Back up your existing /boot/rootfs.squashfs first!
  2. Copy `rootfs.squashfs` onto the `rootfs` (FAT32 boot) partition, replacing
     the existing `rootfs.squashfs`.
  3. Flash `uImage` to raw offset 4 MiB (sector 8192) of the SD card:
       sudo dd if=uImage of=/dev/sdX bs=512 seek=8192 conv=fsync
     (Verify /dev/sdX is your RS-97 SD card before running dd!)

Option B — Live over USB Network (Telnet/FTP/SSH to 169.254.1.1):
  1. Back up stock files:
       cp /boot/rootfs.squashfs /home/retrofw/rootfs.squashfs.2.3.bak
       dd if=/dev/mmcblk0 bs=512 skip=8192 count=6144 of=/home/retrofw/uImage.2.3.bak
  2. Upload `uImage` and `rootfs.squashfs` via FTP/SCP.
  3. Flash Kernel+ #12 (unlocking boot-sector guard temporarily):
       echo 0 > /proc/jz/mmc
       dd if=uImage of=/dev/mmcblk0 bs=512 seek=8192 conv=fsync
       sync
       echo 1 > /proc/jz/mmc
  4. Atomically replace `/boot/rootfs.squashfs` (after verifying MD5) and reboot.
  5. If you had a custom `/home/retrofw/autoexec.sh` pointing to an old GMenu2X,
     update it to `exec /usr/bin/gmenunx` (or remove it to use the built-in
     GMenuNX+ Modern launcher directly).
EOF
(cd "$UPG_STAGE" && sha256sum uImage rootfs.squashfs > SHA256SUMS.txt && zip -q -9 "$UPG_ZIP" uImage rootfs.squashfs UPGRADE.txt SHA256SUMS.txt)
rm -rf "$UPG_STAGE"

echo "=== 8. Generating Release SHA256SUMS.txt ==="
(cd "$DIST" && sha256sum "$(basename "$IMG_XZ")" "$(basename "$UPG_ZIP")" > SHA256SUMS.txt)
ls -lh "$DIST"
cat "$DIST/SHA256SUMS.txt"
