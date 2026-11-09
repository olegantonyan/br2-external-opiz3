#!/bin/sh

rm -f "$TARGET_DIR/etc/init.d/S01seedrng"
rm -rf "$TARGET_DIR/var/lib/bluetooth"
ln -snf /mnt/nvm/bluetooth "$TARGET_DIR/var/lib/bluetooth"
ln -snf /mnt/nvm/dropbear "$TARGET_DIR/etc/dropbear"
ln -snf /mnt/nvm/wpa_supplicant.conf "$TARGET_DIR/etc/wpa_supplicant.conf"
sed -i "s/ console=\${console}/ console=tty1 console=\${console}/" "$TARGET_DIR/boot/extlinux/extlinux.conf"
grep -q "^tty1:" "$TARGET_DIR/etc/inittab" || echo "tty1::respawn:/sbin/getty 38400 tty1" >> "$TARGET_DIR/etc/inittab"
grep -q automount "$TARGET_DIR/etc/mdev.conf" || printf 'mmcblk[0-9]+(p[0-9]+)?\troot:disk 660 */usr/sbin/automount\nsd[a-z][0-9]*\troot:disk 660 */usr/sbin/automount\n' >> "$TARGET_DIR/etc/mdev.conf"
OVERLAYS="uart5 i2c4 usb23 ir"
grep -q fdtoverlays "$TARGET_DIR/boot/extlinux/extlinux.conf" || sed -i "/^ *devicetreedir /a\\  fdtoverlays$(for o in $OVERLAYS; do printf ' /boot/allwinner/opiz3-%s.dtbo' $o; done)" "$TARGET_DIR/boot/extlinux/extlinux.conf"
cd "$TARGET_DIR" && rm -rf usr/bin/gio usr/bin/gsettings usr/bin/gapplication usr/bin/gdbus usr/bin/gresource usr/bin/gio-querymodules usr/bin/gi-* usr/lib/gio usr/share/glib-2.0 usr/lib/libgio-2.0.so* usr/lib/libgobject-2.0.so* usr/lib/libgirepository-2.0.so* usr/lib/libgmodule-2.0.so* usr/lib/libgthread-2.0.so* usr/lib/libffi.so* usr/bin/pcre2test usr/bin/pcre2grep usr/lib/libpcre2-posix.so* usr/lib/libnl-route-3.so* usr/lib/libnl-nf-3.so* usr/lib/libnl-xfrm-3.so* usr/lib/libnl-idiag-3.so* usr/lib/libnl-cli-3.so* usr/share/sounds usr/lib/libatopology.so* usr/bin/aserver usr/lib/libbluetooth.so* lib/libatomic.so*
