# br2-external-opiz3

Buildroot external tree for the Orange Pi Zero 3 (Allwinner H618) on mainline Linux 6.12.6 and U-Boot 2026.07.

## Features

- WiFi and Bluetooth on the onboard UWE5622 (out-of-tree driver + `hciattach-opi`)
- Gigabit Ethernet (YT8531 PHY), stable across warm reboots
- S/PDIF output on header pin 5, exact 32–192 kHz rates — wiring in [docs/spdif-coax.md](docs/spdif-coax.md)
- cpufreq 480–1416 MHz with thermal throttling at 70 °C
- Read-only rootfs, 0 SD writes; persistent config on the onboard SPI NOR (`/nvm`, JFFS2)
- Dropbear SSH (key auth), BusyBox ntpd, alsa-utils, bluez5 tools, stress-ng

## Build

```
cd br2-external-opiz3
make -C ../buildroot-2026.08.1 O=$PWD/output BR2_EXTERNAL=$PWD orangepi_zero3_wifi_defconfig
make -C output
```

Result: `output/images/sdcard.img` (`output/` is git-ignored). Setup, rebuilds and first boot: [docs/build.md](docs/build.md).

CI: pushes to `master` that touch `configs/`, `board/`, `package/` or the tree's Kconfig/make files build the image and publish it as a `build-<n>` release (`sdcard.img.xz`, U-Boot, kernel, DTB, `SHA256SUMS`).

## Kernel patches

`board/orangepi-zero3/patches/linux/`:

| Patch | Purpose |
|-------|---------|
| `0001` | DT: enable SDIO WiFi (mmc1, regulators, pwrseq) — from Armbian |
| `0002` | Bluetooth: drop PARK from default link policy (UWE5622 rejects it) |
| `0003` | DT: S/PDIF sound card |
| `0004` | clk: audio PLL sigma-delta modulation — upstream backport |
| `0005` | clk: fix audio PLL SDM rates, let spdif retune the PLL |
| `0006` | DT: vdd-cpu regulator ramp delay (fixes crashes on frequency changes) |
| `0007` | stmmac: reset EMAC on open, not probe (eth0 after warm reboot) — from Armbian |
| `0008` | DT: CPU cooling map |

U-Boot is bumped from the stock 2024.10, whose SPL misdetected DRAM size on warm reboot.
