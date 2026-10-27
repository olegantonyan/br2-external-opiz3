# S/PDIF coax output — Orange Pi Zero 3

The H618 has a native S/PDIF transmitter, so no Digi+/WM8804, no I2S, no I2C.
Mainline driver `sun4i-spdif` (`allwinner,sun50i-h616-spdif`), enabled by
`board/orangepi-zero3/patches/linux/0003-arm64-dts-orangepi-zero3-enable-spdif-out.patch`.
Correct sample rates also need the audio PLL patches:
- `0004` backports upstream SDM support (6.13); without it 48 kHz plays at ~42 kHz.
- `0005` fixes its table rates (upstream reports half the real PLL rate, so
  playback runs 2x fast) and lets the spdif clock retune the PLL (44.1 kHz).
  Upstream v7.0 still has both bugs.

Verified 32/44.1/48/88.2/96/192 kHz: spdif clock 24.576 / 22.5792 MHz exact.
Upstream tested S/PDIF audio on this board when H616 support was added (2024).

## Header pins (26-pin, numbering per board silkscreen)

| Pin | SoC  | Use                    |
|-----|------|------------------------|
| 5   | PH4  | SPDIF_OUT (3.3 V CMOS) |
| 6   | —    | GND                    |

Pin 5 is also TWI3_SCK; I2C3 is unavailable while S/PDIF is enabled.

## Coax interface (3.3 V logic → 0.5 Vpp / 75 Ω)

```
pin 5 (PH4) ──[R1 240 Ω]──┬──||── RCA centre
                          │  C 100 nF
                     [R2 110 Ω]
                          │
pin 6 (GND) ──────────────┴──────── RCA shell
```

- R1/R2 form a ≈75 Ω source with ≈1.0 V open-circuit, i.e. ≈0.5 Vpp into the
  receiver's 75 Ω termination (IEC 60958-3 consumer level).
- C blocks DC; any 47–220 nF ceramic works.
- Optional: put a 1:1 S/PDIF pulse transformer (e.g. Pulse PE-65612,
  Scientific Conversion SC937-02) between C and the RCA to break ground loops.
  Then the RCA shell connects to the transformer secondary, not board GND.
- Keep the wires from the header to the divider short; use 75 Ω coax after the RCA.

## Software

```
aplay -l                                            # card "SPDIF"
speaker-test -D hw:SPDIF -c 2 -r 48000 -t sine      # 440 Hz test tone
aplay -D hw:SPDIF file.wav
```

Without the circuit, a scope on pin 5 during playback shows a biphase-mark
stream (~3.07 MHz cell rate at 48 kHz).

## Why not the Digi+ (WM8804) board

The Digi+ takes I2S from the host and is configured over I2C. On H616/H618 the
I2S controllers sit behind the AHUB audio hub, which mainline Linux does not
support (still true in v7.0); only the vendor BSP / Armbian's `sunxi_v2` port
drives it. The on-chip S/PDIF makes the WM8804 redundant anyway.

Fallback pin map, if the AHUB route is ever revisited (functions per mainline
`pinctrl-sun50i-h616.c`; Digi+ numbers are Raspberry Pi header pins):

| Signal         | Digi+ / RPi pin | OPi Zero 3 pin | SoC function    |
|----------------|-----------------|----------------|-----------------|
| BCLK           | 12              | 23             | PH6 i2s3 BCLK   |
| LRCK           | 35              | 19             | PH7 i2s3 SYNC   |
| DATA (to WM)   | 40              | 21             | PH8 i2s3 DOUT0  |
| I2C SDA        | 3               | 10             | PH3 i2c2 SDA    |
| I2C SCL        | 5               | 8              | PH2 i2c2 SCK    |
| 5 V / GND      | 2 / 6           | 2 / 6          | —               |

MCLK (PH5, pin 3) is not needed: the WM8804 has its own crystal. I2C2 on
PH2/PH3 replaces UART5 and does not collide with I2S3. WM8804 I2C address: 0x3b.
