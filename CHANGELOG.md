# Changelog

All notable changes are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [0.9.0] - Unreleased

First public release. Supported hardware: Uniwill PH4TUX1 (Monster Huma H4
V4.1; same chassis as TUXEDO InfinityBook Pro 14 Gen6), EC project id `0x13`.

### Added

- Long descriptions open from an info button instead of filling the pages.
- The charging priority is named "USB-C charging priority" and explains Monster Control
  Center's "Type-C Mode".
- Ambient light page: a screen card shows the current brightness, the mode and a
  "Back to Automatic" button after a manual adjustment.
- Faster login on battery: while turbo is off, it is allowed for 20 s when a user logs in
  (GNOME Shell starts in 2.2 s instead of 3.9 s), then tuned re-applies its profile.

### Fixed

- Automatic brightness ignores light readings older than 15 minutes: after a sleep the
  evening's dark readings kept the screen dim in daylight.
- Automatic brightness: while adjusting by hand the screen now matches the slider (the slider at
  its end could give 89%); "Measure now (back to automatic)" drops the manual adjustment.
- At boot tuned could apply its profile before the driver was loaded, leaving the laptop in
  Balanced Medium (32 W) on battery with Power Saver selected; the service now asks tuned to
  re-apply its profile when the driver appears.

### Kernel driver (`qc71_laptop` fork, driver version 0.10)

- Based on Slimbook-Team/qc71_laptop PR #5 (white keyboard backlight); no EC
  write at load for the RGB keyboard manual mode.
- Charging profile (high capacity / balanced / stationary) and USB-C power
  priority; the "application present" bit is managed so the EC accepts them.
- Performance profiles: Balance low/medium/high and Quiet 20/30/40 dB with
  the matching PL1/PL2 values, exposed as `performance_profile` and through the
  kernel `platform_profile` interface (GNOME power mode).
- Fan Boost as its own attribute, keeping the mode bits; the hwmon "automatic"
  fan setting no longer switches the machine into the quiet mode.
- Fan tables written with each profile, only the bytes that changed, in the
  background; optional custom fan curve (`fan_curve`) with safety checks
  (30-85 °C, monotonic, 100 % last step, at least 20 % at 70 °C).
- USB power while off, touchpad toggle hotkey, Fn lock, super key lock (fixed:
  it no longer clears USB power), microphone mute LED and key, performance mode
  key (Balance ↔ Quiet), screen lock and vendor keys, `fan_duty`, `fan_fault`,
  `project_id`.
- Settings are re-applied on resume, on charger plug/unplug and on power
  source changes; module parameters apply saved settings at load.
- All EC bit changes are read-modify-write under a single lock.
- Fixed: Bluetooth turning itself on at every boot (no `SW_RFKILL_ALL` on this
  model).
- Fixed: the battery profile set on unplug could be overwritten by the
  re-apply work.
- Safer unload order (events and sysfs first).
- Model table (DMI + EC project id): the module loads automatically only on a
  verified model; unknown models are refused, `force=1` loads read-only and
  `allow_writes=1` enables writes (taints the kernel). Power-limit and fan-table
  writes stay read-only on an unverified BIOS. All EC writes pass one check.

### Desktop application (GTK 4 / libadwaita)

- Pages for performance, fan (with fan curve editor), keyboard, ambient light,
  battery (health options, top battery consumers with per-app details),
  power saving, history graphs and device information.
- Automatic profiles by running application, time of day and power source,
  with an "only on battery" option.
- Keyboard backlight: off when idle, off during daytime, on in the dark based
  on the ambient light measured with the camera.
- Automatic screen brightness from the camera's ambient light measurement,
  with per-level brightness settings.
- Optional lock when nobody is at the computer (YuNet face detection), with a
  10 s warning; never locks in the dark.
- Power saving page: SD card reader can be switched off, background updates,
  battery-only automations.
- AC power-on setting (UEFI variable, admin authentication, backup and
  read-back verification) and USB power while off, mutually exclusive like the
  vendor software.
- Only changed settings are saved, so Quick Settings changes are not lost.

### System services and helpers

- Root helper with argument validation and a lock, polkit rules (no password
  for everyday settings, admin authentication for the UEFI write).
- System service for automatic profiles, history log (90 days) and alerts
  (overheating, fan fault, battery health).
- Keyboard backlight service that works on the login and lock screens,
  started by udev when the backlight appears; event-based instead of polling.
- Power-saving helper (`huma-control-power`): power-saving tuned profile on
  battery (`huma-control-battery`, used for "Balanced" on battery) and SD card
  reader sleep / off rules; optional component (`--no-power-saving`).
- Power use reduced: cached keyboard level, fewer EC reads, camera
  measurements pause during full-screen video.

### GNOME Shell extension

- Quick Settings power mode with fine tuning and Fan Boost, on-screen display
  for mode and power source changes, top bar button, alerts as notifications,
  compact battery and performance menus.

### Installation

- `install.sh` installs everything with DKMS and uninstalls it again
  (`--uninstall`); Secure Boot and missing `kernel-devel` are reported clearly.
- Installed files: `/usr/local/libexec/huma-control-helper`, `-power`, `-uefi`,
  `-service`, `-keyboard`, `-light`, `-presence`; settings in
  `/etc/huma-control/keyboard.json` and `automation.json`; runtime state in
  `/run/huma-control/`; history, battery usage and UEFI backups in
  `/var/lib/huma-control/`; the app as `~/.local/bin/huma-control`
  (`--page overview|performance|automation|fan|keyboard|ambient|battery|power|history|about`).
- The app and the extension are translated with gettext (English and Turkish).
