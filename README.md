# RetroFW 2.3+ (RS-97 Plus)

A modernized, faster, drop-in upgrade to RetroFW 2.3 built specifically for the **RS-97 Plus** (`hwvariant=5b`, `Y030XX067A` 320×480 IPS screen, 128 MB RAM).

Your existing RetroFW 2.3 games, emulators, saves, and OPKs continue to work out of the box — while the whole handheld feels noticeably faster, cleaner, and far more capable.

---

## Features & Enhancements

### System, Power & Performance
- **Faster Boot & Shutdown:** Trimmed startup delays and sped up SD card detection so the handheld boots into the menu in ~3.5 seconds (down from over 5 seconds) and powers down cleanly.
- **True Pitch-Black Sleep Mode:** Standby now fully shuts off the screen backlight instead of leaving a dim gray glow, extending battery life and waking cleanly on any button press—even at the lowest brightness setting (`1`).
- **Battery-Friendly Menus & Smooth Clock Scaling:** Menus default to a cool 420 MHz to conserve battery, while games scale smoothly to your chosen clock speed with steady 60 Hz display timing (typically stable up to ~660–678 MHz depending on the individual handheld, with extra headroom available in settings).
- **System-Wide Graphics & Audio Acceleration:** Unlocked the handheld's built-in hardware accelerator (MXU) in both the kernel and the core SDL 1.2 library, giving existing games and emulators faster screen drawing (`4.7×` fills) and audio mixing (`2.3×`) automatically.
- **Smarter Memory Management:** Prioritizes fast compressed RAM (`ramzswap0`) before touching SD-card swap, keeping memory-heavy games and ports responsive instead of stuttering or crashing.
- **Drop-In Compatibility:** Existing RetroFW 2.3 games, saves, and OPK apps continue to work out of the box, with both a fresh flashable SD image (auto-expanding on first boot) and a non-destructive in-place upgrade.

### GMenuNX+ Launcher & Apps
- **Modern Refreshed Interface:** Ships with an updated **GMenuNX+ Modern** launcher featuring 4 clean minimalist themes (**MidnightMinimal** by default, plus **Amber**, **Crimson**, and **Emerald**), sharper fonts, and smoother navigation.
- **Recents, Instant Search & Play Time Tracking:** Jump right back into recently played games, filter large ROM libraries instantly with a rewritten search index, and keep track of how long you've spent in each title.
- **Built-In Performance Overlay:** Toggle a live on-screen performance HUD for any game or app straight from the `SELECT` menu.
- **Polished Controls & Stability:** Fixed double-skipping when tapping menu/tab buttons, patched a menu memory leak, and fixed a hang when booting with a USB cable connected.
- **On-Device Terminal App:** Includes a lightweight `st` terminal pre-installed on first boot for quick troubleshooting and tinkering right on the handheld.

### Connectivity & Modern Game Port Support
- **More Reliable USB & Modern File Transfers:** Fixed USB connection reliability across host/device modes and added lightweight Dropbear SSH/SCP support (using zero memory when idle) alongside Telnet and FTP.
- **Complete, Working SDL2 Support:** Replaced RetroFW 2.3's unfinished dummy SDL2 library with a full native **SDL 2.30.11** video, input, and audio driver with hardware scaling up to `800×600`—unlocking modern indie ports and game decompilations (EasyRPG, Cataclysm, OpenLara, modern `fheroes2`, 2D decomps, and more) without needing to backport them to SDL 1.2.

### Optimized SNES & PlayStation Emulators
- **PocketSNES+ (Super Nintendo):** Major performance upgrades to tile rendering, zero-LUT transparency blending, cache-friendly screen clearing, MXU audio mixing, and special-chip (SA-1) handling—bringing demanding classics like *Donkey Kong Country 2*, *Kirby Super Star*, and *Super Mario RPG* up to much smoother, more playable speeds, alongside smoother frameskip options.
- **PCSX4All+ (PlayStation):** Extensive 3D geometry, texture/lighting, MXU audio, and memory optimizations that deliver major frame rate gains across the PS1 library, plus compressed **`.chd`** disc support, hitch-free background CD loading, fixed save states, and a refreshed MidnightMinimal UI.

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
  - Atomic `/proc/jz/clock` (`240–816 MHz` configurable; typical silicon stability ~660–678 MHz) with 60 Hz LCD `LPCDR` + `DDRC` sync and dual-tier `1:3:6:6` OC mode.
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
- **PlayStation**: [`madcatsowega/pcsx4all-plus`](https://github.com/madcatsowega/pcsx4all-plus) (`PCSX4All+` — MidnightMinimal UI, MonoCLUT/LitCLUT GPU, MIPS32 MADD GTE, MXU SPU, CHD + AsyncCD)
- **SNES**: [`madcatsowega/pocketsnes-plus`](https://github.com/madcatsowega/pocketsnes-plus) (`PocketSNES+` — zero-LUT SWAR color math, occlusion-skip `DrawTile16`, direct BW-RAM & SA-1 shutdown, `pref 30` clears, MXU echo/FIR)

---

## Hardware Compatibility
- Built and verified on **RS-97 Plus** (`hwvariant=5b`, `Y030XX067A` 320×480 IPS panel, 128 MB RAM, Ingenic JZ4760B).
- **Do not** flash the RS-97 Plus `uImage` onto non-`5b` LCD revisions without building a matching kernel defconfig from `retrofw-kernel-plus`.
