#!/bin/sh

rm -f "$TARGET_DIR/etc/init.d/S01seedrng"
rm -rf "$TARGET_DIR/var/lib/bluetooth"
ln -snf /nvm/bluetooth "$TARGET_DIR/var/lib/bluetooth"
ln -snf /nvm/dropbear "$TARGET_DIR/etc/dropbear"
ln -snf /nvm/wpa_supplicant.conf "$TARGET_DIR/etc/wpa_supplicant.conf"
sed -i "s/ console=\${console}/ console=tty1 console=\${console}/" "$TARGET_DIR/boot/extlinux/extlinux.conf"
grep -q "^tty1:" "$TARGET_DIR/etc/inittab" || echo "tty1::respawn:/sbin/getty 38400 tty1" >> "$TARGET_DIR/etc/inittab"
OVERLAYS="uart5 i2c4 usb23 ir"
grep -q fdtoverlays "$TARGET_DIR/boot/extlinux/extlinux.conf" || sed -i "/^ *devicetreedir /a\\  fdtoverlays$(for o in $OVERLAYS; do printf ' /boot/allwinner/opiz3-%s.dtbo' $o; done)" "$TARGET_DIR/boot/extlinux/extlinux.conf"
