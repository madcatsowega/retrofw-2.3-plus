# RetroFW 2.3+ (RS-97 Plus Modernization Overlay)

Modernized rootfs overlay, build scripts, and hardware-benchmarked **JZ4760B MXU SIMD `SDL 1.2`** acceleration for the **RS-97 Plus** (`Ingenic JZ4760B`, `128 MB RAM`, `Y030XX067A` 320×480 IPS panel), built to pair with:
- [`retrofw-kernel-plus`](https://github.com/madcatsowega/retrofw-kernel-plus) (`Linux 2.6.31.3 #12`, ABI-compatible MXU context-switch, LZO `ramzswap0`, atomic `/proc/jz/clock`, `HZ=250`, instant PWM-off backlight with 2 ms boost-IC wake kick, quiet UART boot)
- [`gmenunx-plus`](https://github.com/madcatsowega/gmenunx-plus) (`MidnightMinimal` + `AmberMinimal` + `CrimsonMinimal` + `EmeraldMinimal`, 160-entry LRU text cache, fast Game Search modal, 1-tap `Perf HUD`, exposed `Menu clock`)
- [`pcsx4all-plus`](https://github.com/madcatsowega/pcsx4all-plus) & [`pocketsnes-plus`](https://github.com/madcatsowega/pocketsnes-plus)

## Real-Hardware Benchmarks (`600 MHz`, JZ4760B + MXU)

Every kernel in `sdl12-mxu/` passed **0/N CRC-32 bit-exact verification** against stock RetroFW 2.3 `/usr/lib/libSDL-1.2.so.0.11.4`:

| Benchmark (`bench/sdl_zlib_bench.c`) | Stock RetroFW 2.3 | RetroFW 2.3+ | Speedup |
|---|---:|---:|---:|
| `SDL_FillRect` (16-bit, 400× 320×240) | `645.16 ms` | **`138.44 ms`** | **`4.66×`** |
| `SDL_MixAudio` (`AUDIO_S16LSB`, 600× 4096s) | `209.34 ms` | **`91.59 ms`** | **`2.29×`** |
| `SDL_BlitCopy` (16-bit, 400× 320×240) | `762.52 ms` | **`675.51 ms`** | **`1.13×`** |
| `BlitARGB565` (PNG/TTF, 150× 320×240) | `705.54 ms` | **`685.54 ms`** | **`1.03×`** |

*Note: Non-winning C blitters (`Blit565Alpha`, `Blit2to2Key`) and `zlib-ng` (slower than stock `zlib 1.2.11` on MIPS32r1 without SIMD) were benchmarked on hardware and intentionally excluded.*

## What's Baked into `rootfs.squashfs`

- **MXU-Accelerated `libSDL-1.2.so.0.11.4`** (all 16 official RetroFW 2.3 `2021.11.x` patches + runtime `/proc/cpuinfo` MXU detection + MIPS `pref 30` cached `FillRect`, safe uncached `/dev/fb0` burst, MXU `BlitCopy`, MXU `SDL_MixAudio`).
- **`dropbear 2025.87` (`ssh` / `scp` / `dropbearkey`)** via `/etc/inetd.conf` (0 KB RAM / 0 ms boot overhead until connected).
- **Fast Cold Boot (`5.14s → ~3.6s`):** Targeted `S10udev` + `S60udev` external SD mount + async `S05retrofw-plus` (`noop`, `read_ahead_kb=256`, `mmap_min_addr=0`, LZO `ramzswap0` prio 100 + SD swap).
- **GMenuNX+ Modern**, `libperfhud.so`, `st` Terminal, 4 Minimal color themes (`MidnightMinimal` default), and matching Kernel+ `#12` `.ko` modules.

## Build & Safe Deploy

```bash
# Build rootfs-2.4.squashfs (preserves root:root + 4755 SUID via fakeroot)
./build-rootfs.sh

# Safely deploy over USB Ethernet (169.254.1.1) with stock backup & post-drop_caches MD5 verify
python3 deploy-rootfs-usb.py
```
