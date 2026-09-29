# Tested hardware

Huma Control Center is developed and tested on the laptop below. Every feature that
writes to the embedded controller (EC) or UEFI has been verified on exactly this
model and BIOS. Other laptops built on the same Uniwill PH4TUX1 platform may work,
but are unverified: please run `tools/hw-report.sh` and open a "New model" issue.

## Monster Huma H4 V4.1 (Uniwill PH4TUX1)

| | |
|---|---|
| Vendor / model | MONSTER HUMA H4 V4.1 |
| SKU | H4V41PH4TUX1 |
| Platform (ODM) | Uniwill PH4TUX1, EC project id `0x13` |
| BIOS | American Megatrends N.1.15MON06 (2021-12-23) |
| CPU | Intel Core i7-1165G7 (11th Gen, Tiger Lake), 4 cores / 8 threads |
| Graphics | Intel Iris Xe (TigerLake-LP GT2), no discrete GPU |
| Display | 14" 1920×1200 (16:10), 60 Hz and 40 Hz modes, PSR2 |
| Memory | 32 GB |
| Storage | Samsung PM9A1 NVMe SSD |
| Wireless | Intel Wi-Fi 6 AX201, Intel AX201 Bluetooth |
| Camera | Chicony integrated camera with IR sensor (04f2:b71a) |
| Card reader | Realtek USB 3.0 card reader (0bda:0316) |
| Keyboard | White single-colour backlight, 3 levels (off / low / high) |
| Ports | Thunderbolt 4 |
| Battery | 46.7 Wh (4100 mAh, 11.4 V) |

### Software

| | |
|---|---|
| Distribution | Fedora Linux 44 Workstation |
| Kernel | 7.2 (DKMS module build) |
| Desktop | GNOME 50 (Wayland) |
| Reference | Monster Control Center 4.8.47.13 on Windows 11 (dual boot), used only to compare EC values |

### Verified features

- Performance modes (Balanced / Quiet × Low / Medium / High) with power limits and
  per-mode fan tables, Fan Boost, platform_profile integration (power-profiles-daemon
  / tuned-ppd)
- Custom fan curve with safety validation
- White keyboard backlight: levels, Fn+F6 events, idle turn-off
- Charging profile and USB-C charging priority, USB power while off, touchpad toggle key,
  Fn lock, Windows-key lock, microphone mute LED
- AC Recover (power on when the charger is plugged in) through UEFI, with backup and
  read-back
- Ambient light measurement with the webcam (no light sensor on this model)

## Battery test

Measured on 2026-09-29 with a per-minute log of the battery (`current_now` ×
`voltage_now`), the performance mode, fan speed and playback state.

| | |
|---|---|
| Workload | YouTube 1080p (24 fps), Firefox 156, looped, full screen; hardware video decoding (VA-API) |
| Settings | Brightness 3516/39425 (~9 %), Wi-Fi on, Bluetooth off, SD card reader off, power mode *Balanced* (→ Quiet · 20 dB on battery), keyboard backlight off when idle |
| Result | **98 % → 2 % in 5 h 6 min**, then GNOME shut down at the critical level |
| Average draw | **7.9 W** (7.2–12.1 W); steady over the whole run (8.0, 7.9, 7.8, 7.8, 7.8 W per hour) |
| Fan | Off 93 % of the time; CPU average 53 °C |

Netflix in Firefox is different: Widevine decodes the video on the CPU. In short
measurements the CPU package drew ~3.6 W at Netflix's "Medium" quality and ~4.2 W at
"Auto"; no full battery run was done for Netflix.

## Same platform, not yet tested

These laptops are reported to use the Uniwill PH4TUX1 board. They are **not** enabled
by default: the driver refuses to load on unknown models (see `SECURITY.md`).

- Other Monster Huma H4 variants and BIOS versions
- TUXEDO InfinityBook 14 Gen6 and other PH4TUX1-based rebrands

If you own one, `tools/hw-report.sh` collects the (non-identifying) information needed
to add it.
