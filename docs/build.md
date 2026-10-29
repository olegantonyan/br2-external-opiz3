# Building the image

## Layout

Two sibling directories; only this one is versioned:

| Directory              | Role                                                        |
|------------------------|-------------------------------------------------------------|
| `buildroot-2026.08.1/` | Upstream Buildroot, unmodified.                             |
| `br2-external-opiz3/`  | This tree: `BR2_EXTERNAL` named `OPIZ3` (`external.desc`). Build output goes to its git-ignored `output/` (`O=`), ~16 GB. |

## Host setup

Buildroot's mandatory host tools: `which sed make binutils diffutils gcc g++ bash patch gzip bzip2 perl tar cpio unzip rsync file bc findutils awk wget` (plus `build-essential` on Debian). Everything else, including the cross toolchain, is built by Buildroot.

Get Buildroot next to this tree:

```
wget https://buildroot.org/downloads/buildroot-2026.08.1.tar.xz https://buildroot.org/downloads/buildroot-2026.08.1.tar.xz.sign
tar xf buildroot-2026.08.1.tar.xz
```

## Build

```
cd br2-external-opiz3
make -C ../buildroot-2026.08.1 O=$PWD/output BR2_EXTERNAL=$PWD orangepi_zero3_wifi_defconfig
make -C output
```

- `O=` selects the output directory (default would be `buildroot-2026.08.1/output/`). It must be absolute and cannot be moved after a build: generated files hold absolute paths.
- `BR2_EXTERNAL=` attaches this tree. Buildroot stores it in `output/.br2-external.mk`, so later commands only need `make -C output`.
- The defconfig step writes `output/.config` and a wrapper `output/Makefile`; `make -C output` builds everything.
- Downloads are cached in `buildroot-2026.08.1/dl/` (`BR2_DL_DIR`); `BR2_JLEVEL=0` uses all cores.
- `BR2_DOWNLOAD_FORCE_CHECK_HASHES=y`: every downloaded source needs a `.hash` (custom U-Boot version: `board/orangepi-zero3/patches/uboot/uboot.hash`).

## Output

`output/images/`:

- `sdcard.img` — write to a microSD card, e.g. `dd if=sdcard.img of=/dev/sdX bs=4M conv=fsync`
- `Image`, `allwinner/*.dtb`, `rootfs.ext2`, `u-boot-sunxi-with-spl.bin`

Other `output/` dirs: `build/` (per-package source/build trees), `host/` (toolchain), `target/` (rootfs before packing).

## What this tree contributes

- `configs/orangepi_zero3_wifi_defconfig` — stock `orangepi_zero3_defconfig` plus everything below. Paths into this tree use `$(BR2_EXTERNAL_OPIZ3_PATH)`.
- `Config.in`, `external.mk` — hook in `package/uwe5622` (WiFi/BT kernel modules) and `package/hciattach-opi` (BT UART attach tool).
- `board/orangepi-zero3/`:
  - `linux.fragment`, `busybox.fragment` — config additions merged into the stock kernel/BusyBox configs
  - `patches/linux/`, `patches/uboot/` — via `BR2_GLOBAL_PATCH_DIR`, applied with `patch -F0` (no fuzz)
  - `rootfs-overlay/` — firmware, init scripts, network config, SSH key
  - `device_table.txt` — file modes (the overlay copy does not keep them)
  - `post-build.sh` — symlinks persistent config into `/nvm`

## Partial rebuilds

Run as `make -C output <target>`, then `make -C output` to regenerate `sdcard.img`.

| Changed                     | Command                                        |
|-----------------------------|------------------------------------------------|
| defconfig                   | rerun the `orangepi_zero3_wifi_defconfig` step |
| `linux.fragment`            | `make linux-reconfigure`                       |
| `busybox.fragment`          | `make busybox-reconfigure`                     |
| kernel patch added/changed  | `make linux-dirclean uwe5622-dirclean`         |
| other package patch         | `make <pkg>-dirclean`                          |
| overlay, init scripts       | nothing extra                                  |

Patches apply only on a fresh extract, hence `dirclean`. `uwe5622` builds against the kernel tree, so it is cleaned along with it.

## First boot

- Login: `root` / `root` (serial, or SSH with the key in `rootfs-overlay/root/.ssh/authorized_keys`).
- The rootfs is read-only; persistent state is on the SPI NOR at `/nvm`. Once per board, erase it: `/etc/init.d/S00nvm stop; flash_erase -j /dev/mtd0 0 0; reboot`.
- WiFi: edit `/etc/wpa_supplicant.conf` (→ `/nvm`, survives reflashing), then `ifdown wlan0; ifup wlan0`.
- Maintenance writes to the SD: `mount -o remount,rw /`, then remount `ro` or reboot.
