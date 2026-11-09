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

- `nor-system.itb`, `u-boot-sunxi-with-spl.bin`, `nor.img` — NOR images, flashing in the [README](../README.md#flash)
- `sdcard.img` — recovery/install system for a microSD card, e.g. `dd if=sdcard.img of=/dev/sdX bs=4M conv=fsync`
- `Image`, `allwinner/*.dtb(o)`, `rootfs.cpio.xz` (initramfs inside `nor-system.itb`), `rootfs.ext2` (inside `sdcard.img`)

Other `output/` dirs: `build/` (per-package source/build trees), `host/` (toolchain), `target/` (rootfs before packing).

## What this tree contributes

- `configs/orangepi_zero3_wifi_defconfig` — stock `orangepi_zero3_defconfig` plus everything below. Paths into this tree use `$(BR2_EXTERNAL_OPIZ3_PATH)`.
- `Config.in`, `external.mk` — hook in `package/uwe5622` (WiFi/BT kernel modules) and `package/hciattach-opi` (BT UART attach tool).
- `board/orangepi-zero3/`:
  - `linux.config` — full kernel config (savedefconfig format), built from `allnoconfig` plus only what this board uses; all drivers built in except the out-of-tree UWE5622 modules
  - `busybox.fragment` — additions merged into the stock BusyBox config
  - `uboot.fragment` — U-Boot boot command for NOR boot; U-Boot environment kept in RAM only
  - `patches/linux/`, `patches/uboot/` — via `BR2_GLOBAL_PATCH_DIR`, applied with `patch -F0` (no fuzz)
  - `rootfs-overlay/` — firmware, init scripts, network config, SSH key, `automount` (mdev hotplug handler: microSD → `/sdcard`, USB → `/media/<label or UUID>`)
  - `device_table.txt` — file modes (the overlay copy does not keep them)
  - `post-build.sh` — symlinks persistent config into `/nvm`; adds the mdev rules for `automount`; deletes files nothing links or runs (glib GIO/GObject, extra libnl/pcre2 libs, ALSA sample WAVs, libatomic)
  - `post-image.sh` — builds `nor-system.itb` (LZMA kernel, DTB, overlays, `rootfs.cpio.xz`) and `nor.img`; fails if an image outgrows its partition
- `external.mk` — also keeps `/boot` (kernel, extlinux) out of the initramfs
- `flash.sh` — host-side NOR update over SSH
- `stress.sh` — builds a static stress-ng with the Buildroot toolchain into `output/stress-ng/`, runs it on the board from `/tmp`

## Partial rebuilds

Run as `make -C output <target>`, then `make -C output` to regenerate `sdcard.img`.

| Changed                     | Command                                        |
|-----------------------------|------------------------------------------------|
| defconfig                   | rerun the `orangepi_zero3_wifi_defconfig` step |
| `linux.config`              | `make linux-reconfigure uwe5622-rebuild`       |
| `busybox.fragment`          | `make busybox-reconfigure`                     |
| `uboot.fragment`            | `make uboot-reconfigure`                       |
| kernel patch added/changed  | `make linux-dirclean uwe5622-dirclean`         |
| other package patch         | `make <pkg>-dirclean`                          |
| overlay, init scripts       | nothing extra                                  |

Kernel config: `make linux-menuconfig`, then `make linux-update-defconfig` writes it back to `linux.config`. Buildroot never removes old modules from `output/target`; after dropping modules, `rm -rf output/target/lib/modules` before rebuilding.

Patches apply only on a fresh extract, hence `dirclean`. `uwe5622` builds against the kernel tree, so it is cleaned along with it.

## First boot

- Login: `root` / `root` (serial, or SSH with the key in `rootfs-overlay/root/.ssh/authorized_keys`): `ssh root@br2-external-opiz3.local` (mDNS; hostname from `BR2_TARGET_GENERIC_HOSTNAME`).
- From NOR the rootfs is an initramfs in RAM: writable, but changes are lost at reboot. Persistent state is on the NOR at `/nvm`.
- To wipe `/nvm` (it is reformatted empty on the next mount): `/etc/init.d/S00nvm stop; flash_erase -j /dev/$(awk -F: '/"nvm"/ {print $1}' /proc/mtd) 0 0; reboot`. Never erase `/dev/mtd0`: it is the whole chip.
- microSD: auto-mounted on `/sdcard`; USB drives on `/media/<label or UUID>` (`blkid` shows both) (`grep automount /var/log/messages`). A card with a bootloader at 8 KiB (`eGON.BT0` in `od -c -j 8196 -N 8 /dev/mmcblk0`) boots instead of the NOR. If its first partition starts at 1 MiB, wiping the gap keeps the data: `dd if=/dev/zero of=/dev/mmcblk0 bs=1k seek=8 count=1016`. Unmount before pulling a card or drive (`umount /sdcard`); removal only lazily unmounts.
- WiFi: edit `/etc/wpa_supplicant.conf` (→ `/nvm`, survives reflashing), then `ifdown wlan0; ifup wlan0`.
- Booted from the recovery SD the rootfs is the read-only ext4 on the card; maintenance writes: `mount -o remount,rw /`, then remount `ro` or reboot.
