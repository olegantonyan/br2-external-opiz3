# br2-external-opiz3

Buildroot external tree for the Orange Pi Zero 3 (Allwinner H618) on mainline Linux 6.18.60 (LTS) and U-Boot 2026.07.

## Features

- Boots from the onboard 16 MiB SPI NOR, no SD card needed; Linux runs from RAM (initramfs)
- WiFi and Bluetooth on the onboard UWE5622 (out-of-tree driver + `hciattach-opi`)
- Gigabit Ethernet (YT8531 PHY), stable across warm reboots
- S/PDIF output on header pin 5, exact 32–192 kHz rates — wiring in [docs/spdif-coax.md](docs/spdif-coax.md)
- HDMI text console (fbcon, login on tty1)
- Header peripherals as DT overlays: UART5, I2C4 by default; I2C2, UART2, SPI1 (spidev) selectable; USB2/USB3, IR receiver and line out on the 13-pin header — pin map in [docs/header.md](docs/header.md)
- cpufreq 480–1416 MHz with thermal throttling at 70 °C
- Board-only kernel config: everything built in except the UWE5622 modules
- Persistent config (WiFi, SSH host key, BT pairings) on the NOR `nvm` partition (`/nvm`, JFFS2)
- mDNS: reachable as `br2-external-opiz3.local` on Ethernet and WiFi (mdnsd, also advertises SSH)
- Dropbear SSH (key auth), BusyBox ntpd, alsa-utils, bluetoothctl, i2c-tools
- Stress test without stress-ng in the image: `./stress.sh br2-external-opiz3.local --cpu 4 --vm 2 --vm-bytes 25% --verify -t 10m --metrics-brief` builds a static binary once and runs it from `/tmp` (RAM)

## Build

```
cd br2-external-opiz3
make -C ../buildroot-2026.08.1 O=$PWD/output BR2_EXTERNAL=$PWD orangepi_zero3_wifi_defconfig
make -C output
```

Results in `output/images/` (`output/` is git-ignored):

| File | Use |
|------|-----|
| `nor-system.itb` | `system` partition: kernel, DTB, overlays, rootfs (one FIT, CRC-checked at boot) |
| `u-boot-sunxi-with-spl.bin` | `u-boot` partition |
| `nor.img` | whole 16 MiB chip, empty `env` and `nvm` — first install |
| `sdcard.img` | recovery/install system on microSD |

Setup and partial rebuilds: [docs/build.md](docs/build.md).

CI: pushes to `master` that touch `configs/`, `board/`, `package/`, `flash.sh`, `stress.sh` or the tree's Kconfig/make files build everything and publish a `build-<n>` release (`nor-system.itb`, `u-boot-sunxi-with-spl.bin`, `nor.img.xz`, `sdcard.img.xz`, `flash.sh`, kernel, DTB, `SHA256SUMS`).

## Flash

NOR layout (also in the DT, `patches/linux/0009`):

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| `u-boot` | 0x000000 | 960 KiB | SPL + TF-A + U-Boot |
| `env` | 0x0f0000 | 64 KiB | boot settings written by `fw_setenv` |
| `system` | 0x100000 | 14.5 MiB | `nor-system.itb` |
| `nvm` | 0xf80000 | 512 KiB | JFFS2, `/nvm` |

### Update a running board

```
./flash.sh br2-external-opiz3.local                      # u-boot + system from output/images
./flash.sh br2-external-opiz3.local system               # system only
./flash.sh -d ~/Downloads/build-42 br2-external-opiz3.local   # files from a CI release
```

The host needs mDNS name resolution (nss-mdns/Avahi, on by default on most desktops); an IP address works too. `flash.sh` copies each image over SSH, skips partitions that already match, writes with `flashcp`, verifies by reading back, then reboots (`-n`: don't). `env` and `nvm` are never touched. Safe on a running board: Linux runs from RAM and does not use the NOR except `/nvm`. If verification fails, don't reboot: run it again (or recover below).

### First install, or the board does not come up

The boot ROM tries the SD card before the NOR, so a recovery card always works, whatever is on the NOR.

1. Write `sdcard.img` to a microSD card: `xz -dc sdcard.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync` (or `dd if=sdcard.img …`).
2. Insert it and power on. The board boots Linux from the SD and gets its address by DHCP as usual; it answers to `br2-external-opiz3.local`.
3. Write the NOR:
   - Board already had this layout (keeps `/nvm`): `./flash.sh -n br2-external-opiz3.local`
   - First install, or the NOR is unknown/corrupt (wipes `/nvm`: WiFi config, SSH host key, BT pairings):
     ```
     cat output/images/nor.img | ssh root@br2-external-opiz3.local 'cat > /tmp/nor.img && /etc/init.d/S00nvm stop && flashcp -v /tmp/nor.img /dev/mtd0'
     ```
4. Power off, remove the card, power on.

Over the serial console (115200 8N1), U-Boot can also rewrite the NOR itself, e.g. from a FAT USB stick (not tested): `usb start; fatload usb 0 0x50000000 nor-system.itb; sf probe 0; sf update 0x50000000 0x100000 ${filesize}`.

### Boot settings

U-Boot reads `overlays` and `extra_bootargs` from the `env` partition. From Linux:

```
fw_setenv overlays "uart5 i2c2 spi1"     # overlay names: board/orangepi-zero3/dts/allwinner/opiz3-*.dtso
fw_setenv overlays                       # back to the default: uart5 i2c4 usb23 ir
fw_setenv extra_bootargs "loglevel=7"    # appended to the kernel command line
reboot
```

An unknown overlay name makes U-Boot boot the base DTB without overlays. The first `fw_printenv`/`fw_setenv` on an empty `env` prints `Bad CRC, using default environment`; that is expected.

WiFi: edit `/etc/wpa_supplicant.conf` (stored in `/nvm`), then `ifdown wlan0; ifup wlan0`.

## Kernel patches

`board/orangepi-zero3/patches/linux/`:

| Patch | Purpose |
|-------|---------|
| `0001` | DT: enable SDIO WiFi (mmc1, regulators, pwrseq) — from Armbian |
| `0002` | Bluetooth: drop PARK from default link policy (UWE5622 rejects it) |
| `0003` | DT: S/PDIF sound card |
| `0005` | clk: fix audio PLL SDM rates, let spdif retune the PLL |
| `0006` | DT: vdd-cpu regulator ramp delay (fixes crashes on frequency changes) |
| `0007` | stmmac: reset EMAC on open, not probe (eth0 after warm reboot) — from Armbian |
| `0008` | DT: CPU cooling map |
| `0009` | DT: SPI NOR partitions |
| `0101`–`0142` | H616 display engine (DE33), TCON-TV, HDMI PHY, DT pipeline + Zero 3 HDMI — Jernej Škrabec's series via Armbian `sunxi-6.18/patches.drm` @ `fb22cb0f` |

U-Boot is bumped from the stock 2024.10, whose SPL misdetected DRAM size on warm reboot. `board/orangepi-zero3/uboot.fragment` makes it boot from the NOR (`system` FIT + overlays from `env`) when not started from an SD card.
