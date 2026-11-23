# RetroFW 2.3+ (RS-97 Plus)

A modernized, faster, drop-in upgrade to RetroFW 2.3 built specifically for the **RS-97 Plus** (`hwvariant=5b`, `Y030XX067A` 320×480 IPS screen, 128 MB RAM).

Your existing RetroFW 2.3 games, emulators, saves, and OPKs continue to work out of the box — while the whole handheld feels noticeably faster, cleaner, and far more capable.

---

## What's Better in RetroFW 2.3+? *(In Plain English)*

- **Much Faster Startup (~3.5 seconds)**: Stock RetroFW 2.3 takes over 5 seconds to reach the menu. RetroFW 2.3+ trims out slow startup delays and mounts your external SD card in a quarter of a second, getting you into your games noticeably faster.
- **A Fresh, Modern Look Out of the Box**: Includes our upgraded **GMenuNX+ Modern** launcher with 4 clean minimalist themes (**MidnightMinimal** by default, plus **Amber**, **Crimson**, and **Emerald**), sharp fonts, smooth icons, and a built-in **Performance HUD** you can toggle with one tap from the `SELECT` menu.
- **Snappier Menus & Much Faster Game Search**: Fixed a menu memory leak, added text caching, and rewrote the game search index so searching large ROM collections feels instant instead of sluggish.
- **Crisp, Reliable Controls**: Fixed double-skipping in menus/tabs when tapping buttons, and fixed a hang that could happen if you booted with a USB cable plugged in.
- **Real Pitch-Black Sleep & Better Battery Life**:
  - Idle sleep now truly shuts off the screen backlight (pitch black instead of glowing gray) and wakes cleanly on any button press — even at the lowest brightness setting (`1`).
  - Menus default to a cool, battery-friendly **420 MHz** (adjustable from `240` to `816 MHz` in Settings), while games still boost cleanly to whatever clock speed you pick.
- **Faster Graphics & Audio Across Existing Games**: The core graphics/sound library (**SDL 1.2**) now uses the RS-97's hidden **MXU hardware accelerator**, speeding up screen fills (`4.7×` faster), audio mixing (`2.3×` faster), and sprite copies with zero visual changes.
- **Real SDL2 Support — Unlocking Modern Ports & Decomps**: Stock RetroFW 2.3 shipped an unfinished "dummy" SDL2 library that couldn't display anything on screen. RetroFW 2.3+ includes a brand-new, working **SDL 2.30.11** display/input/audio driver with hardware scaling up to `800×600`, opening the door to modern ports (EasyRPG, Cataclysm, OpenLara, modern `fheroes2`, 2D decomps, and more) without backporting to SDL 1.2!
- **Smarter Memory Management**: Uses fast **compressed RAM (`ramzswap0`)** ahead of SD-card swap so memory-hungry ports and emulators stay responsive instead of stuttering or crashing.
- **Modern SSH / SCP File Transfers + On-Device Terminal**: Adds **Dropbear SSH/SCP** alongside Telnet/FTP (only runs when you connect, using zero memory when idle) and seeds the **`st` Terminal** app on first boot.

---

## Downloads & Installation

Grab the latest release files from the [**Releases**](https://github.com/madcatsowega/retrofw-2.3-plus/releases) page:

### Option 1 — Fresh MicroSD Card (`RetroFW-2.3+-RS97Plus.img.xz`)
1. Download `RetroFW-2.3+-RS97Plus.img.xz` (and verify against `SHA256SUMS.txt`).
2. Burn it to a microSD card using **BalenaEtcher**, **Raspberry Pi Imager**, **Rufus**, or on Linux:
   ```bash
   xzcat RetroFW-2.3+-RS97Plus.img.xz | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
   ```
3. Insert the card into the RS-97 Plus internal SD slot and power on. On first boot, it automatically sets up the swap partition, expands the `RETROFW` FAT32 data partition across the rest of your SD card, reboots once, and opens straight into **`GMenuNX+ Modern` (`MidnightMinimal`)**.

> **Note:** The U-Boot boot splash logo intentionally remains the classic RetroFW `2.3` artwork — that's normal and why this release is named **RetroFW 2.3+**.

### Option 2 — In-Place Upgrade (`RetroFW-2.3+-RS97Plus-Upgrade.zip`)
Already have RetroFW 2.3 set up with all your ROMs and saves? Download `RetroFW-2.3+-RS97Plus-Upgrade.zip` and follow `UPGRADE.txt` inside to update just `uImage` + `rootfs.squashfs` without wiping your data partition.

---

## Technical Details

- **Kernel (`RetroFW Kernel+ #12`)** — [`madcatsowega/retrofw-kernel-plus`](https://github.com/madcatsowega/retrofw-kernel-plus):
  - ABI-compatible JZ4760B MXU SIMD register context-switch (`0/400` errors on stress test; 100% compatible with stock `.ko` modules).
  - LZO `/dev/ramzswap0` (48 MiB, priority 100) ahead of SD swap.
  - Atomic `/proc/jz/clock` (`240–816 MHz`) with 60 Hz LCD `LPCDR` + `DDRC` sync and dual-tier `1:3:6:6` OC (`708–816 MHz`).
  - True PWM-off `backlight=0` on `Y030XX067A` with automatic 2 ms 20% LED boost-IC wake kick; physical brightness button steps `1 → 5 → 10..100 → 1`.
  - `HZ=250`, `PREEMPT_VOLUNTARY`, `!SLUB_DEBUG`, `noop` I/O scheduler, SquashFS fragment cache `8`, quiet UART boot (`DEFAULT_CONSOLE_LOGLEVEL=4`).
- **Native `SDL 2.30.11` (`libSDL2-2.0.so.0.3000.11`)**:
  - Custom `retrofw` `/dev/fb0` driver (`sdl2-retrofw/`) with hardware IPU scaling up to `800×600` RGB565, triple-buffered `FBIOPAN_DISPLAY` tear-free VSync (verified **63.6 FPS** on hardware), cached RAM shadow framebuffer, zero-udev `/dev/input/event*` `evdev` scanner, OSS `/dev/dsp` audio, and automatic software-renderer fallback for `SDL_RENDERER_ACCELERATED`.
  - 100% ABI drop-in (`SONAME: libSDL2-2.0.so.0`) for stock `SDL2_image`, `SDL2_ttf`, `SDL2_mixer`, `SDL2_gfx`, and `SDL2_net`.
- **MXU-Accelerated `SDL 1.2` (`libSDL-1.2.so.0.11.4`)**:
  - Built from `SDL 1.2.15` + all 16 official RetroFW 2.3 patches + hardware-verified (`0/N` CRC-32 diffs) JZ4760B SIMD/cache kernels (`FillRect 4.66×`, `MixAudio 2.29×`, `BlitCopy 1.13×`, `BlitARGB565 1.03×`).
- **Cold-Boot Pipeline (`5.14s → 3.45s`)**:
  - Trimmed `S10udev` + direct `busybox blkid` FAT-first `S60udev` (`0.25s`, mounts `/media/RETROFW` synchronously before GMenuNX+ and defers slow `ext3` journals to the background) + async `S05retrofw-plus`.
- **Userspace Stack**:
  - `GMenuNX+ Modern` ([`madcatsowega/gmenunx-plus`](https://github.com/madcatsowega/gmenunx-plus)) baked into `/usr/bin/gmenunx`, `libperfhud.so`, Dropbear `2025.87` SSH/SCP via `inetd`, and `st` Terminal (`st.opk` seeded non-destructively on first boot).

---

## Companion `+` Projects
- **Kernel+**: [`madcatsowega/retrofw-kernel-plus`](https://github.com/madcatsowega/retrofw-kernel-plus)
- **Launcher**: [`madcatsowega/gmenunx-plus`](https://github.com/madcatsowega/gmenunx-plus)
- **PlayStation**: [`madcatsowega/pcsx4all-plus`](https://github.com/madcatsowega/pcsx4all-plus) (`PCSX4All+` — CHD, AsyncCD, MIPS GTE, MXU SPU)
- **SNES**: [`madcatsowega/pocketsnes-plus`](https://github.com/madcatsowega/pocketsnes-plus) (`PocketSNES+ v2` — Fast SA-1, MXU echo/FIR, Half-V/Smooth frameskip)

---

## Hardware Compatibility
- Built and verified on **RS-97 Plus** (`hwvariant=5b`, `Y030XX067A` 320×480 IPS panel, 128 MB RAM, Ingenic JZ4760B).
- **Do not** flash the RS-97 Plus `uImage` onto non-`5b` LCD revisions without building a matching kernel defconfig from `retrofw-kernel-plus`.
