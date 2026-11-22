#!/usr/bin/env bash
set -euo pipefail

BASE="/home/sean/retrofw-2.4"
STOCK="$BASE/rootfs-2.3-stock.squashfs"
ROOT="$BASE/rootfs-2.4-build"
OUT="$BASE/rootfs-2.4.squashfs"

echo "=== 1. Extracting stock RetroFW 2.3 rootfs ==="
rm -rf "$ROOT" "$OUT"
unsquashfs -d "$ROOT" "$STOCK" >/dev/null

echo "=== 2. Installing MXU-accelerated libSDL-1.2.so.0.11.4 & Native SDL 2.30.11 ==="
cp -f "$BASE/staging/usr/lib/libSDL-1.2.so.0.11.4" "$ROOT/usr/lib/libSDL-1.2.so.0.11.4"
chmod 755 "$ROOT/usr/lib/libSDL-1.2.so.0.11.4"
ln -sf libSDL-1.2.so.0.11.4 "$ROOT/usr/lib/libSDL-1.2.so.0"
ln -sf libSDL-1.2.so.0.11.4 "$ROOT/usr/lib/libSDL.so"

rm -f "$ROOT/usr/lib/libSDL2-2.0.so.0.16.0"
cp -f "$BASE/staging/usr/lib/libSDL2-2.0.so.0.3000.11" "$ROOT/usr/lib/libSDL2-2.0.so.0.3000.11"
chmod 755 "$ROOT/usr/lib/libSDL2-2.0.so.0.3000.11"
ln -sf libSDL2-2.0.so.0.3000.11 "$ROOT/usr/lib/libSDL2-2.0.so.0"
ln -sf libSDL2-2.0.so.0.3000.11 "$ROOT/usr/lib/libSDL2.so"

echo "=== 3. Installing dropbear 2025.87 (SSH/SCP/dbclient) via inetd ==="
cp -f "$BASE/staging/usr/sbin/dropbearmulti" "$ROOT/usr/sbin/dropbearmulti"
chmod 755 "$ROOT/usr/sbin/dropbearmulti"
ln -sf dropbearmulti "$ROOT/usr/sbin/dropbear"
ln -sf ../sbin/dropbearmulti "$ROOT/usr/bin/ssh"
ln -sf ../sbin/dropbearmulti "$ROOT/usr/bin/scp"
ln -sf ../sbin/dropbearmulti "$ROOT/usr/bin/dropbearkey"
cat << 'EOF' > "$ROOT/etc/inetd.conf"
telnet stream tcp nowait root /usr/sbin/telnetd telnetd -i
ftp stream tcp nowait root /usr/sbin/ftpd ftpd -w /
ssh stream tcp nowait root /usr/sbin/dropbear dropbear -i -B -R -r /home/retrofw/.dropbear_ed25519_key
EOF

echo "=== 4. Baking GMenuNX+ Modern, libperfhud.so, 4 Minimal skins & st Terminal ==="
cp -f /home/sean/gmenunx-plus/dist/retrofw/gmenunx "$ROOT/usr/bin/gmenunx"
chmod 755 "$ROOT/usr/bin/gmenunx"
cp -f /home/sean/gmenunx-plus/dist/retrofw/libperfhud.so "$ROOT/usr/lib/libperfhud.so"
chmod 755 "$ROOT/usr/lib/libperfhud.so"

for skin in MidnightMinimal AmberMinimal CrimsonMinimal EmeraldMinimal; do
    rm -rf "$ROOT/usr/share/gmenunx/skins/$skin"
    cp -a "/home/sean/gmenunx-plus/assets/skins/$skin" "$ROOT/usr/share/gmenunx/skins/$skin"
done
# Update Recents icon to clock in Default skin too
cp -f /home/sean/gmenunx-plus/assets/skins/Default/sections/recents.png \
      "$ROOT/usr/share/gmenunx/skins/Default/sections/recents.png"

if [ -x /home/sean/retrofw-emulators/rs97_st-sdl/st ]; then
    cp -f /home/sean/retrofw-emulators/rs97_st-sdl/st "$ROOT/usr/bin/st"
    chmod 755 "$ROOT/usr/bin/st"
fi

echo "=== 5. Baking Kernel+ #12 matching .ko modules ==="
MODSRC="/home/sean/retrofw-kernel-plus/modules-out/lib/modules/2.6.31.3"
if [ -d "$MODSRC" ]; then
    rm -rf "$ROOT/lib/modules/2.6.31.3"
    mkdir -p "$ROOT/lib/modules"
    cp -a "$MODSRC" "$ROOT/lib/modules/2.6.31.3"
fi

echo "=== 6. Optimizing cold-boot init scripts & runtime tuning ==="
# Fast S05retrofw-plus: I/O scheduler, mmap_min_addr=0 (PCSX4All+), and async LZO ramzswap0 + SD swap
cat << 'EOF' > "$ROOT/etc/init.d/S05retrofw-plus"
#!/bin/sh
case "$1" in
    start)
        for dev in /sys/block/mmcblk*; do
            [ -w "$dev/queue/scheduler" ] && echo noop > "$dev/queue/scheduler" 2>/dev/null
            [ -w "$dev/queue/read_ahead_kb" ] && echo 256 > "$dev/queue/read_ahead_kb" 2>/dev/null
        done
        [ -w /proc/sys/vm/mmap_min_addr ] && echo 0 > /proc/sys/vm/mmap_min_addr 2>/dev/null
        [ -w /proc/sys/vm/swappiness ] && echo 60 > /proc/sys/vm/swappiness 2>/dev/null
        # Activate LZO compressed RAM swap (prio 100) + SD swap + SSH host key in background
        (
            if [ -b /dev/ramzswap0 ] && ! grep -q ramzswap0 /proc/swaps 2>/dev/null; then
                mkswap /dev/ramzswap0 >/dev/null 2>&1 && swapon -p 100 /dev/ramzswap0 2>/dev/null
            fi
            /sbin/swapon -a 2>/dev/null
            if [ -d /home/retrofw ] && [ ! -f /home/retrofw/.dropbear_ed25519_key ] && [ -x /usr/bin/dropbearkey ]; then
                /usr/bin/dropbearkey -t ed25519 -f /home/retrofw/.dropbear_ed25519_key >/dev/null 2>&1
            fi
        ) &
        ;;
esac
exit 0
EOF
chmod 755 "$ROOT/etc/init.d/S05retrofw-plus"

# Fast S10udev: start udevd without blocking 1.2s on 200+ non-block sysfs devices
cat << 'EOF' > "$ROOT/etc/init.d/S10udev"
#!/bin/sh
UDEV_CONFIG=/etc/udev/udev.conf
test -r $UDEV_CONFIG || exit 6
. $UDEV_CONFIG

case "$1" in
    start)
        [ -e /proc/sys/kernel/hotplug ] && printf '\000\000\000\000' > /proc/sys/kernel/hotplug
        /sbin/udevd -d || exit 1
        ;;
    stop)
        udevadm control --stop-exec-queue
        killall udevd
        ;;
    *)
        exit 1
        ;;
esac
exit 0
EOF
chmod 755 "$ROOT/etc/init.d/S10udev"

# Skip swap partitions in 99-media-automount.rules (prevents 3s exfat/udf probe stall & empty /media/mmcblk1p2)
sed -i '/IMPORT{program}="\/sbin\/blkid -o udev -p %N"/a ACTION=="add", ENV{ID_FS_TYPE}=="swap", GOTO="end"' \
    "$ROOT/etc/udev/rules.d/99-media-automount.rules"

# Fast S60udev: wait briefly for udevd control socket, then trigger external SD/USB block
# partition uevents directly via /sys/block (Linux 2.6.31 CONFIG_SYSFS_DEPRECATED_V2 does
# not populate /sys/class/block, so udevadm --subsystem-match=block matches 0 devices!).
# Mount FAT/exFAT/NTFS partitions FIRST synchronously (~40ms!) so /media/RETROFW is 100%
# ready before GMenuNX+ starts; defer slow ext2/3/4 journal recovery to the background!
cat << 'EOF' > "$ROOT/etc/init.d/S60udev"
#!/bin/sh
case "$1" in
    start)
        i=0
        while [ ! -S /run/udev/control ] && [ "$i" -lt 40 ]; do
            usleep 25000 2>/dev/null || sleep 1
            i=$((i + 1))
        done
        slow_devs=""
        for u in /sys/block/mmcblk1/mmcblk1p*/uevent /sys/block/sd*/sd*/uevent; do
            [ -w "$u" ] || continue
            devname=$(basename "$(dirname "$u")")
            fstype=$(/sbin/blkid -s TYPE -o value "/dev/$devname" 2>/dev/null)
            case "$fstype" in
                swap|"")
                    continue
                    ;;
                vfat|msdos|exfat|ntfs)
                    echo add > "$u" 2>/dev/null
                    ;;
                *)
                    slow_devs="$slow_devs $u"
                    ;;
            esac
        done
        /sbin/udevadm settle --timeout=3 2>/dev/null
        if [ -n "$slow_devs" ]; then
            (
                for u in $slow_devs; do
                    echo add > "$u" 2>/dev/null
                done
            ) &
        fi
        ;;
esac
exit 0
EOF
chmod 755 "$ROOT/etc/init.d/S60udev"

# Remove blocking ::sysinit:/sbin/swapon -a from inittab (handled asynchronously by S05retrofw-plus)
sed -i '\|^::sysinit:/sbin/swapon -a$|d' "$ROOT/etc/inittab"

# Stamp RetroFW 2.3+ release metadata
cat << 'EOF' > "$ROOT/etc/os-release"
NAME="RetroFW+"
ID=retrofw
VERSION_ID=2.3+
PRETTY_NAME="RetroFW 2.3+ (RS-97 Plus)"
ANSI_COLOR="0;36"
HOME_URL="https://github.com/madcatsowega/retrofw-2.3-plus"
EOF

echo "=== 7. Repacking SquashFS 4.0 (gzip, 128 KiB blocks, root:root, SUID preserved) ==="
fakeroot -- sh -c "
    chmod 4755 '$ROOT/bin/busybox' '$ROOT/usr/bin/fusermount'
    mksquashfs '$ROOT' '$OUT' -comp gzip -b 131072 -no-xattrs -no-exports -all-root -noappend
"

echo "=== 8. Verification ==="
unsquashfs -s "$OUT"
ls -lh "$STOCK" "$OUT"
unsquashfs -ll "$OUT" | grep -E "(bin/busybox|fusermount|libSDL-1\.2\.so\.0\.11\.4|libSDL2-2\.0\.so\.0\.3000\.11|dropbearmulti|libperfhud|MidnightMinimal/skin\.conf|S05retrofw|S60udev|g_ether\.ko|usr/bin/gmenunx)$"
md5sum "$OUT"
