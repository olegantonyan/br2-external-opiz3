#!/bin/sh

rm -f "$TARGET_DIR/etc/init.d/S01seedrng"
rm -rf "$TARGET_DIR/var/lib/bluetooth"
ln -snf /nvm/bluetooth "$TARGET_DIR/var/lib/bluetooth"
ln -snf /nvm/dropbear "$TARGET_DIR/etc/dropbear"
ln -snf /nvm/wpa_supplicant.conf "$TARGET_DIR/etc/wpa_supplicant.conf"
