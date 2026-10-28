# S/PDIF output (coax, optical) — Orange Pi Zero 3

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
| 1   | —    | 3.3 V (buffered variant) |
| 5   | PH4  | SPDIF_OUT (3.3 V CMOS) |
| 6   | —    | GND                    |

Pin 5 is also TWI3_SCK; I2C3 is unavailable while S/PDIF is enabled.

## Minimal coax (tested into a DAC)

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
- Keep the wires from the header to the divider short; use 75 Ω coax after the RCA.

## Buffered: isolated coax + optical

```
pin 1 (3.3 V) ──┬───────────────┬───────────────────────────┬──────── U2 VCC
             [C2 100 nF]      U1 VCC                  [C3 100 nF]
                │                                         │
pin 6 (GND) ────┴───────────────┴─────────────────────────┴──────── U2 GND

pin 5 (PH4) ──[R0 33 Ω]──┬── U1A ──[R1 240 Ω]──┬──[C1 100 nF]──┐  ┌──── RCA centre
                         │                     │               )║║(
                         │                [R2 110 Ω]           )║║(  T1 1:1
                         │                     │               )║║(
                         │                    GND          GND─┘  └──── RCA shell (isolated)
                         └── U1B ──────────────────────────────────────── U2 IN (TOSLINK TX)
```

| Ref    | Part (all on AliExpress)                                                 |
|--------|-------------------------------------------------------------------------|
| U1     | 74LVC2G17GW dual Schmitt buffer, SOT-363 (on a SOT-363/SC70-6 adapter for hand wiring) |
| U2     | DLT1150 TOSLINK transmitter: 2.7–5.5 V, ≤10 mA, pinout 1 GND, 2 VCC, 3 IN. No screw tab: hold its PCB on standoffs behind the panel cutout |
| T1     | Pulse PE-65612NL, 1:1, 2 kV isolation, DIP-4                             |
| R0     | 33 Ω, at the header end of the pin 5 wire                               |
| R1, R2 | 240 Ω, 110 Ω (same divider as above)                                     |
| C1     | 100 nF, keeps the buffer's 1.65 V DC average out of T1                  |
| C3     | 100 nF right at U2's pins                                               |
| J1     | RCA panel jack with insulating washers, or any RCA jack in a plastic panel |

- U1 isolates the SoC pin from both outputs, restores edges after the header wire
  and keeps ESD from the RCA away from the H618.
- T1 breaks the ground loop between the board and the DAC: the RCA shell connects
  only to T1's secondary, never to board GND or a metal panel (use an isolated jack).
- Load per output: coax ≈12 mA peak, TOSLINK ≤10 mA; pin 1 supplies both easily.
- Rates: PE-65612NL is rated 1–7 Mb/s and TOSLINK modules 16 Mb/s NRZ, so both are safe to 96 kHz (6.1 Mb/s); 192 kHz (12.3 Mb/s) is out of spec for T1 and often fails on optical.
- Without the optical output, drop U1B, U2, C3.
- Optical alone is already fully isolated; T1 matters only for coax.

### Alternatives

Buffer (U1). Inverting gates work too: biphase-mark coding ignores polarity. Unused inputs to GND.

| Part                                                     | Package       | Supply | Notes |
|----------------------------------------------------------|---------------|--------|-------|
| 74LVC2G17, 74LVC1G17 ×2, 74LVC1G125, 74LVC2G04, 74LVC14, 74LVC244 | SMD only | 3.3 V  | ±24 mA, fastest; best choice |
| 74AC14, 74AC04, 74AC125, 74AC244, 74AC541                | DIP-14/20     | 3.3 V  | ±12 mA at 3 V; best DIP choice |
| 74AHC14, 74AHC125, 74AHC1G14/1G17/1G125                  | SMD           | 3.3 V  | weaker; parallel 2 gates on the coax output |
| 74HC14, 74HC04, 74HC125, 74HC244                         | DIP-14/20     | 3.3 V  | weak and slow at 3.3 V; parallel 2–3 gates on the coax output; up to 96 kHz |
| 74HCT14/04/125/244, 74ACT14/04/125/244                   | DIP-14/20     | 5 V    | not specified below 4.5 V; run from pin 2 (5 V), TTL input threshold accepts 3.3 V PH4; see 5 V variant |
| 74LS, CD4049/4050/40106                                  | DIP           | —      | not viable: LS has a weak high output, CMOS 4000 is too slow |

Optical transmitter (U2). Pinouts and footprints differ (e.g. GND/VCC/IN vs IN/VCC/GND); check the datasheet before layout.

| Part                                                     | Supply       | Notes |
|----------------------------------------------------------|--------------|-------|
| DLT1150 / DLT1150A                                       | 2.7–5.5 V    | default; no screw tab |
| Cliff FCR684205T (OTJ-5)                                 | 2.7–5.5 V    | same design with shutter and Ø2.5 mm panel screw hole |
| Everlight PLT133/T10W                                    | 2.7–5.5 V    | distributors list it obsolete |
| Toshiba TOTX147, TOTX147L, TOTX147PL                     | 2.7–3.6 V    | discontinued; shutter, M3 self-tap panel hole (L) |
| Toshiba TOTX141, TOTX141L, TOTX141PL                     | 2.7–3.6 V    | older 3 V family |
| Sharp GP1FAV31TK0F                                       | 3 V          | obsolete |
| Toshiba TOTX173, TOTX177, TOTX179(L/PL), TOTX1350; Sharp GP1FA551TZ, GP1FAV55TK0F | 4.75–5.25 V | 5 V only: VCC from pin 2, TTL input (VIH ≈2 V) still driven by the 3.3 V buffer; confirm VIH in the datasheet |

Transformer (T1): 1:1, consumer S/PDIF.

| Part                                    | Package       | Notes |
|-----------------------------------------|---------------|-------|
| Pulse PE-65612NL                        | through-hole  | default; 2 kV; rated 1–7 Mb/s |
| Pulse PE-65812NL                        | SMD?          | same PE-65x12 family; package unverified |
| Murata DA101C                           | DIL-6         | 2 kV, ≈1–2 mH; different pinout from PE-65612 |
| Murata DA101JC (DA100J series)          | DIL-6         | Murata's recommended successor to DA100 |
| Scientific Conversion SC937-02, SC916-01 | SMD          | used in Cirrus Logic reference designs |
| none                                    | —             | minimal circuit above; no ground isolation |

5 V variant (74HCT/74ACT buffer from pin 2): the coax divider becomes R1 390 Ω, R2 91 Ω (≈74 Ω, ≈0.95 V open-circuit); any transmitter from the table works at 5 V except the 3 V-only parts.

## Software

```
aplay -l                                            # card "SPDIF"
speaker-test -D hw:SPDIF -c 2 -r 48000 -t sine      # 440 Hz test tone
aplay -D hw:SPDIF file.wav
```

Without the circuit, a scope on pin 5 during playback shows a biphase-mark
stream (~3.07 MHz cell rate at 48 kHz).

