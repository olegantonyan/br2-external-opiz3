# S/PDIF coax output — Orange Pi Zero 3

The H618 has a native S/PDIF transmitter; no external transmitter chip, I2S or I2C needed.
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

