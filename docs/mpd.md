# MPD size study (2026-11-09)

Not in the image. Question: does MPD with all audio codecs fit into the free space of the `system` partition, with its database and state on `/sdcard`?

Free space at the time: `nor-system.itb` 8.41 MB of 15.20 MB (14.5 MiB) → **6.79 MB**. The initramfs is xz-compressed inside the FIT, so what counts is compressed size.

## Method

Separate test build (`O=../output-mpdtest`) from `orangepi_zero3_wifi_defconfig` plus `BR2_TOOLCHAIN_BUILDROOT_CXX=y` and MPD 0.24.15 with every decoder plugin, ALSA, curl + OpenSSL + CA certificates, soxr, ID3 tags; ffmpeg 6.1.5 with all decoders/demuxers but no encoders, muxers, filters, bsfs, devices or `ffmpeg` binary. Sizes: files MPD added to the target (from `packages-file-list.txt`), stripped, packed as cpio and compressed with `xz -9 -C crc32` like `rootfs.cpio.xz`. Per-group sums match the real image growth (10.6 vs 10.7 MB). The full build failed the size check: `nor-system.itb` 19.1 MB.

## Results

| Part | Raw | On flash (xz) |
|---|---|---|
| MPD + libstdc++ + fmt, libid3tag, libsoxr, libmad | 4.3 MB | 1.2 MB |
| Native decoders: flac, libvorbis/libogg, opus, mpg123, faad2, wavpack, libsndfile, libsidplay2, fluidsynth, libmodplug | 5.7 MB | 1.8 MB |
| ffmpeg, all decoders + demuxers (incl. video) | 12.8 MB | 4.9 MB |
| ffmpeg, audio decoders + demuxers only (list below) | 5.2 MB | 2.0 MB |
| libcurl without TLS (HTTP streams) | | 0.35 MB |
| libcurl + OpenSSL + CA certificates (HTTPS streams) | 7.7 MB | 2.7 MB (OpenSSL alone 2.3) |

| Combination | xz total | Fits 6.79 MB |
|---|---|---|
| Everything, full ffmpeg | 10.5 MB | no |
| Native decoders + audio-only ffmpeg + HTTPS | 7.7 MB | no |
| **MPD + audio-only ffmpeg + HTTPS** | 5.9 MB | yes, ~0.9 MB left |
| MPD + native decoders + audio-only ffmpeg + HTTP only | 5.3 MB | yes, ~1.4 MB left |
| MPD + native decoders, no ffmpeg | 3.0 MB | yes, ~3.8 MB left |

## Findings

- MPD needs C++; the current toolchain has none. Enabling `BR2_TOOLCHAIN_BUILDROOT_CXX` rebuilds the toolchain and everything after it (full clean build).
- Without ffmpeg there is no `.m4a`: MPD's faad2 plugin reads raw AAC/ADTS only, not the MP4 container (AAC, ALAC). ffmpeg also brings APE, WMA, AC3/DTS, Musepack, TAK.
- Audio-only ffmpeg covers MP3, FLAC, Vorbis, Opus, AAC, ALAC, WavPack, APE, WMA, WAV/AIFF, DSD, so most native decoder plugins become redundant. Only SID (libsidplay2), MIDI (fluidsynth) and MOD (libmodplug) need their own libraries.
- HTTPS is the expensive part. OpenSSL includes the `openssl` binary and modules; mbedTLS or wolfSSL would likely save 1–2 MB (not measured, see below).
- MPD links `libavfilter` even with no filters enabled (125 KB stripped). `libavdevice` (18 KB) and `libswresample` (100 KB) come along.

## Recommended config (not tested on the board)

MPD + audio-only ffmpeg + HTTPS, ~5.9 MB:

```
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_PACKAGE_MPD=y
BR2_PACKAGE_MPD_ALSA=y
BR2_PACKAGE_MPD_CURL=y
BR2_PACKAGE_MPD_LIBSOXR=y
BR2_PACKAGE_MPD_DSD=y
BR2_PACKAGE_MPD_FFMPEG=y
# BR2_PACKAGE_MPD_MAD is not set
BR2_PACKAGE_OPENSSL=y
BR2_PACKAGE_LIBCURL_OPENSSL=y
BR2_PACKAGE_CA_CERTIFICATES=y
# BR2_PACKAGE_FFMPEG_FFMPEG is not set
BR2_PACKAGE_FFMPEG_ENCODERS=""
BR2_PACKAGE_FFMPEG_MUXERS=""
BR2_PACKAGE_FFMPEG_BSFS=""
BR2_PACKAGE_FFMPEG_FILTERS=""
BR2_PACKAGE_FFMPEG_PROTOCOLS="file pipe"
# BR2_PACKAGE_FFMPEG_INDEVS is not set
# BR2_PACKAGE_FFMPEG_OUTDEVS is not set
BR2_PACKAGE_FFMPEG_DECODERS="aac aac_fixed aac_latm ac3 ac3_fixed adpcm_4xm adpcm_adx adpcm_afc adpcm_agm adpcm_aica adpcm_argo adpcm_ct adpcm_dtk adpcm_ea adpcm_ea_maxis_xa adpcm_ea_r1 adpcm_ea_r2 adpcm_ea_r3 adpcm_ea_xas adpcm_g722 adpcm_g726 adpcm_g726le adpcm_ima_acorn adpcm_ima_alp adpcm_ima_amv adpcm_ima_apc adpcm_ima_apm adpcm_ima_cunning adpcm_ima_dat4 adpcm_ima_dk3 adpcm_ima_dk4 adpcm_ima_ea_eacs adpcm_ima_ea_sead adpcm_ima_iss adpcm_ima_moflex adpcm_ima_mtf adpcm_ima_oki adpcm_ima_qt adpcm_ima_qt_at adpcm_ima_rad adpcm_ima_smjpeg adpcm_ima_ssi adpcm_ima_wav adpcm_ima_ws adpcm_ms adpcm_mtaf adpcm_psx adpcm_sbpro_2 adpcm_sbpro_3 adpcm_sbpro_4 adpcm_swf adpcm_thp adpcm_thp_le adpcm_vima adpcm_xa adpcm_xmd adpcm_yamaha adpcm_zork alac als amrnb amrwb ape atrac3 atrac3p cook dca dfpwm dsd_lsbf dsd_lsbf_planar dsd_msbf dsd_msbf_planar eac3 flac gsm mlp mp1 mp1float mp2 mp2float mp3 mp3adu mp3adufloat mp3float mp3on4 mp3on4float mpc7 mpc8 opus pcm_alaw pcm_alaw_at pcm_bluray pcm_dvd pcm_f16le pcm_f24le pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_lxf pcm_mulaw pcm_mulaw_at pcm_s16be pcm_s16be_planar pcm_s16le pcm_s16le_planar pcm_s24be pcm_s24daud pcm_s24le pcm_s24le_planar pcm_s32be pcm_s32le pcm_s32le_planar pcm_s64be pcm_s64le pcm_s8 pcm_s8_planar pcm_sga pcm_u16be pcm_u16le pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 pcm_vidc qdm2 ra_144 ra_288 ralf shorten speex tak truehd tta vorbis wavpack wmalossless wmapro wmav1 wmav2 wmavoice"
BR2_PACKAGE_FFMPEG_DEMUXERS="aac ac3 aiff amr ape asf au caf dsf dts eac3 flac iff loas matroska mlp mov mp3 mpc mpc8 ogg pcm_alaw pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_mulaw pcm_s16be pcm_s16le pcm_s24be pcm_s24le pcm_s32be pcm_s32le pcm_s8 pcm_u16be pcm_u16le pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 pcm_vidc rm shorten sox spdif tak truehd tta voc w64 wav wsd wv xwma"
```

`BR2_PACKAGE_MPD_MAD` defaults to y; ffmpeg decodes MP3, so it is off here. Add `BR2_PACKAGE_MPD_SIDPLAY`, `_FLUIDSYNTH` or `_MODPLUG` only if those formats are needed.

MPD settings for state on the card: `db_file`, `state_file`, `sticker_file`, `playlist_directory` and `music_directory` under `/sdcard`. `/sdcard` is mounted asynchronously by the mdev daemon (S10mdev) and the card is polled (`broken-cd`), so an MPD init script should wait for `mountpoint -q /sdcard` with a timeout and cope with no card.

## Next to try: lighter TLS library

Replace OpenSSL (2.3 MB xz, the biggest single item after ffmpeg) with a smaller curl TLS backend. Buildroot 2026.08.1 offers `BR2_PACKAGE_LIBCURL_MBEDTLS` (mbedTLS 3.6.7) and `BR2_PACKAGE_LIBCURL_WOLFSSL` (wolfSSL 5.9.4). In the recommended config, swap `BR2_PACKAGE_OPENSSL=y` + `BR2_PACKAGE_LIBCURL_OPENSSL=y` for one of them, rebuild, and measure the same way (xz of the added files). Expected saving 1–2 MB, which would leave ~2–3 MB free. Then check on the board that HTTPS radio streams play and certificates verify against `ca-certificates`.
