# HDMI status screen

`statusd` (`package/statusd/src/statusd.c`) draws a status screen with the CPU into KMS dumb buffers. It is started by init (`::respawn:/usr/sbin/statusd`) and logs to syslog (`grep statusd /var/log/messages`).

## Why KMS and not `/dev/fb0`

An image written to `/dev/fb0` does not stay up:

- fbcon draws into the same buffer: kernel messages, the tty1 getty, cursor blink, unblank.
- On every HPD event the kernel's fbdev client re-modesets and fbcon repaints. Many monitors toggle HPD on standby or input switch.
- The fbdev buffer size is fixed when fbdev is first set up (the boot monitor's mode, or nothing without a monitor), so a later monitor can't get its own resolution.

`statusd` holds DRM master instead. While it does, fbdev skips hotplug handling, panning and modesets (`drm_fb_helper.c`), so fbcon can't touch the screen. The only override is a VT switch from `KD_GRAPHICS` to text, and `statusd` never changes the VT mode. When `statusd` exits or crashes, the kernel restores the text console, and init restarts it.

## Behaviour

- Hotplug: kernel `HOTPLUG=1` uevents (netlink), debounced 500 ms. A full connector probe also runs every 30 s in case an event was missed.
- Mode: a `video=` mode wins (see below). Otherwise the monitor's preferred mode if it is ≤2560x1440 and ≤60 Hz, otherwise the largest mode within those limits. If buffer allocation or the modeset fails, the next mode is tried.
- Disconnect: the output keeps running, so a monitor waking from standby finds a signal right away.
- Reconnect: no modeset if the EDID and mode list are unchanged, so there's no flicker on monitor sleep/wake. Any change (another monitor) gets a new modeset at that monitor's resolution.
- Self-heal: if the CRTC stops showing a `statusd` framebuffer, or a page flip doesn't complete within 2 s, it re-applies the mode. `kill -HUP $(pidof statusd)` forces a modeset.
- Buffers: 2 × XRGB8888 plus a RAM shadow; 14.7 MB of CMA each at 2560x1440. The display engine has no IOMMU, so scanout needs `CONFIG_CMA` (96 MiB). Without it, nothing larger than 4 MiB can be allocated and HDMI is dead.

## Forcing a mode

For monitors with broken EDID or HPD (KVMs, adapters), use the kernel `video=` option:

```
fw_setenv extra_bootargs "video=HDMI-A-1:1280x720@60"     # force this mode
fw_setenv extra_bootargs "video=HDMI-A-1:1280x720@60D"    # also force the connector on (no HPD needed)
fw_setenv extra_bootargs                                  # back to automatic
reboot
```

## Testing without touching the cable

`/sys/class/drm/card0-HDMI-A-1/status` accepts `on`, `off` and `detect`, and each write sends a hotplug event. `on` without a monitor gives the no-EDID modes (1024x768).

**`off` powers the HDMI PHY down** (`dw_hdmi_connector_force` → `dw_hdmi_update_power`), so the monitor really loses signal. Going back with `detect` can leave the PHY off for good: dw-hdmi then powers on only when its rxsense flag is set, and after a forced `off` it stayed false. Seen 2026-11-09: screen dark under a working DRM state, and neither a full modeset, a monitor power cycle nor a cable replug helped. `echo on` brings it back. A reboot clears the forced state. A real cable unplug/replug doesn't go through this path and works.

## Screen

Header: hostname, kernel, uptime, clock, date, NTP sync state (`adjtimex`). Cards:

- **CPU**: total load, current/max frequency, load average, per-core bars, 2-minute history.
- **Memory**: used/available, page cache, CMA (display buffers), RAM root filesystem, history.
- **Temperature**: all thermal zones (GPU, VE, CPU, DDR), hottest one large, history. Colours: amber from 65 °C, red from 75 °C (cpufreq throttles at 70 °C).
- **Network**: eth0 and wlan0 state, IPv4, link speed; WiFi SSID, RSSI, link rate and band from wpa_supplicant's control socket; rx/tx rates with history. Bluetooth rfkill state.
- **Audio**: every ALSA playback device: idle/playing, rate, format, channels, owning process (`/proc/asound`); last format played and when.
- **Storage**: `/mnt/nvm`, `/mnt/sdcard`, `/media/*` with filesystem and usage; USB devices (no hubs) with speed.

Footer: Esc hint (dimmed when no keyboard is attached), mode, monitor, render time per frame, hotplug/modeset counters.

Layout scales with the mode (unit = min(W/64, H/36)). Text is the kernel's Terminus 16x32 bitmap, box-filtered to each size, so it is crisp at 1440p (1:1) and anti-aliased elsewhere. Background and card frames are rendered once per mode and copied each frame. About 60 ms per frame at 2560x1440, once a second.

## Console login (Esc)

Keyboards (any evdev device with `KEY_ESC`) are opened when they appear (uevent, plus a 30 s rescan) and grabbed exclusively, so keys pressed on the status screen don't reach tty1. On Esc release, `statusd` ungrabs, closes the DRM device (the kernel brings the text console back), clears tty1 and starts `getty -t 60 38400 tty1`. When that process ends (logout, or no login name within 60 s), it takes the display back. There is no permanent tty1 getty in inittab; the serial console and SSH are unaffected.

If `statusd` restarts while someone is logged in on tty1, the new instance takes the display and the session keeps running hidden. Press Esc again for a second login, or log the old session out over SSH.

## Signals

- `SIGHUP`: forced modeset.
- `SIGUSR1`: screenshot of the current frame to `/tmp/statusd.ppm` (`magick statusd.ppm statusd.png` on the host).
- `SIGUSR2`: same as Esc (console login), for remote testing.
