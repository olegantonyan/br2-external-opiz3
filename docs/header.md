# Header peripherals

Functions are DT overlays (`board/orangepi-zero3/dts/allwinner/opiz3-*.dtso`), installed as `/boot/allwinner/opiz3-*.dtbo` and applied by U-Boot from the `fdtoverlays` line in `/boot/extlinux/extlinux.conf`.

Default: `uart5 i2c4 usb23 ir` (set by `OVERLAYS` in `board/orangepi-zero3/post-build.sh`).

## 26-pin header

| Pin | Ball | Default | Alternatives |
|-----|------|---------|--------------|
| 3   | PH5  | GPIO    | UART2 TX, SPI1 CS0 |
| 5   | PH4  | S/PDIF out | — (I2C3 SCK, unusable while S/PDIF is on) |
| 8   | PH2  | UART5 TX | I2C2 SCK |
| 10  | PH3  | UART5 RX | I2C2 SDA |
| 19  | PH7  | I2C4 SDA | UART2 RTS, SPI1 MOSI |
| 21  | PH8  | GPIO    | UART2 CTS, SPI1 MISO |
| 23  | PH6  | I2C4 SCK | UART2 RX, SPI1 CLK |
| 24  | PH9  | GPIO    | SPI1 CS1 |
| 7, 11, 12, 13, 15, 16, 18, 22, 26 | PC9, PC6, PC11, PC5, PC8, PC15, PC14, PC7, PC10 | GPIO | — |

All 3.3 V. The I2C overlays enable the weak internal pull-ups so an empty bus scans clean; real devices need 4.7 kΩ to 3.3 V.

## 13-pin header

| Pin | Function |
|-----|----------|
| 3/4, 5/6 | USB2, USB3 host (`usb23`) |
| 7/8 | Line out R/L (H616 codec, ALSA card `Codec`; `S41codec` unmutes it at boot, −9 dB) |
| 13 | IR receiver PH10 (`ir`): raw on `/dev/lirc0`; decoders off by default, enable with `echo nec > /sys/class/rc/rc0/protocols` |
| 10, 12 | PC1, PI6 GPIO |

## Overlays

| Overlay | Device | Pins | Conflicts with |
|---------|--------|------|----------------|
| `uart5` | `/dev/ttyS5` | 8, 10 | `i2c2` |
| `i2c2`  | `/dev/i2c-2` | 8, 10 | `uart5` |
| `i2c4`  | `/dev/i2c-4` | 23, 19 | `uart2`, `spi1` |
| `uart2` | `/dev/ttyS2` (RTS/CTS) | 3, 23, 19, 21 | `i2c4`, `spi1` |
| `spi1`  | `/dev/spidev1.0`, `/dev/spidev1.1` | 23, 19, 21, 3, 24 | `i2c4`, `uart2` |
| `usb23` | 2 USB host ports | 13-pin 3–6 | — |
| `ir`    | IR receiver | 13-pin 13 | — |

Notes:

- The codec is ALSA card 0, so `/etc/asound.conf` makes `SPDIF` the default device.
- I2C buses with an alias keep their number (`i2c-2`, `i2c-4`); the PMIC bus and HDMI DDC get the next free numbers.
- `/dev/ttyS0`–`ttyS5` always exist; `ttyS5`/`ttyS2` only work with their overlay.
- `spi1` also aliases `spi0`, otherwise the NOR would become `spi2.0` and `/nvm` (`mtd:spi0.0`) would not mount.
- Hardware PWM (PH2/PH3) is not offered: mainline has no H616 PWM driver.

## Switching

```
mount -o remount,rw /
vi /boot/extlinux/extlinux.conf
mount -o remount,ro /
reboot
```

Edit the `fdtoverlays` line, e.g. `fdtoverlays /boot/allwinner/opiz3-i2c2.dtbo /boot/allwinner/opiz3-spi1.dtbo`. U-Boot prints `Failed to apply overlay` on the serial console if one is broken; the kernel then boots with the base DTB alone.
