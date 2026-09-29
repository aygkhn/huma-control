# Third-party components and credits

Huma Control Center is licensed under GPL-2.0-or-later (see `COPYING`), except for
the parts listed below. Per-file licensing is recorded in `REUSE.toml`.

## Included code and data

### qc71_laptop (kernel driver, `driver/`)

- Upstream: <https://github.com/pobrn/qc71_laptop>, by Barnabás Pőcze
- Base of this fork: Slimbook-Team fork, <https://github.com/Slimbook-Team/qc71_laptop>,
  commit `725bf59b299db0f6bf053139ab6b10c16b050a3f` (PR #5, white keyboard backlight,
  by Michael de By)
- License: **GPL-2.0-only** (`driver/LICENSE`, `LICENSES/GPL-2.0-only.txt`)
- Changes made in this project are listed in `driver/README.md`. The whole
  `driver/` directory, including the new files, stays GPL-2.0-only.

### YuNet face detection model (`system/yunet.onnx`)

- Source: OpenCV Zoo, `face_detection_yunet_2023mar.onnx`,
  <https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet>
- Copyright (c) 2020 Shiqi Yu
- License: **MIT** (`system/yunet.onnx.LICENSE`, `LICENSES/MIT.txt`)
- Shipped unmodified. It is used only by the optional presence check.

### References inherited from upstream qc71_laptop

The upstream driver lists these sources in `driver/main.c`; they are kept there:
the Intel QC71 product specification (whitebook), LWN article 391230 (WMI),
the "MOF decompilation" article by nietrzeba.pl (reading the ACPI-WMI interface
description), TUXEDO's `tuxedo-cc-wmi` and `tuxedo-keyboard`, and a
NotebookReview forum thread about TongFang GK7C laptops.

## Register knowledge (no code copied)

The EC register addresses and bit meanings used by the driver are facts about the hardware. Besides our own measurements they
were cross-checked against these open-source drivers. No code was copied from
them.

- **tuxedo-drivers** by TUXEDO Computers GmbH,
  <https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers>:
  charging profile and charging priority registers, fan table layout,
  USB power share bit, the PH4TUX1 / InfinityBook Pro 14 Gen6 platform, and the
  idea of touching the immediate brightness register before setting a white
  keyboard level (`uniwill_write_kbd_bl_brightness_white_workaround`),
  re-implemented here in three lines.
- **uniwill-laptop**, the upstream Linux kernel driver by Armin Wolf
  (`drivers/platform/x86/uniwill/`): "application present"
  bit, touchpad toggle, Fn lock and super key handling, attribute naming.

## Runtime dependencies (not included)

Used when installed on the system, not shipped with this project: Linux kernel
(DKMS), GTK 4, libadwaita, PyGObject, GNOME Shell, tuned / tuned-ppd,
power-profiles-daemon API, UPower, polkit, systemd, FFmpeg and v4l-utils (camera
capture), OpenCV (Python, optional presence check, installed from PyPI into a
private virtual environment). Each keeps its own license.

## Fan tables

`driver/fan_tables.h` contains six fan tables
(temperature thresholds and fan duties, 48 bytes each). The values are the ones
Monster Control Center 4.8.47.13 uses on this model; they were taken from the
vendor application's per-model table files and converted to the EC layout, and
they were verified to be identical to what the EC holds at `0x0f00-0x0f2f`
while the vendor application runs. The vendor's files are not included. One
threshold was changed locally (see the comment in the file).

## Trademarks and affiliation

This project is not affiliated with, endorsed by or supported by Monster
Notebook, Uniwill, TUXEDO Computers, Slimbook or Intel. "Monster", "Huma",
"Control Center", "Uniwill", "TUXEDO", "InfinityBook" and other names are
trademarks of their respective owners and are used here only to identify
compatible hardware and software.
