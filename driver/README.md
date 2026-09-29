# qc71_laptop (Huma Control Center fork)

Out-of-tree Linux kernel module for Uniwill/Intel QC71 based laptops, extended
for the Uniwill **PH4TUX1** barebone (EC project id `0x13`), sold as
Monster Huma H4 V4.1 and, with the same chassis, as TUXEDO InfinityBook Pro 14 Gen6.
It is built and installed with DKMS by `../install.sh`. The module name is still
`qc71_laptop` and its sysfs directory is `/sys/devices/platform/qc71_laptop/`.

The EC registers used here are documented in
[../docs/ec-register-map.md](../docs/ec-register-map.md); the version history
at the end of this file lists what changed in each release.

## Provenance

| | |
|---|---|
| Original project | [pobrn/qc71_laptop](https://github.com/pobrn/qc71_laptop) by Barnabás Pőcze |
| Base of this fork | [Slimbook-Team/qc71_laptop](https://github.com/Slimbook-Team/qc71_laptop), PR #5 (white keyboard backlight, by Michael de By) |
| Base commit | `725bf59b299db0f6bf053139ab6b10c16b050a3f` |
| License | **GPL-2.0-only**, see [LICENSE](LICENSE) |

The rest of the project is GPL-2.0-or-later; this directory, including the
files added here, stays GPL-2.0-only like the code it is derived from.

## Changes against the base commit

Modified files:

| File | Change |
|---|---|
| `main.c` | No EC write at load for the RGB keyboard "manual mode"; model detection before anything else; WMI events are stopped first on unload; new submodules registered |
| `ec.c`, `ec.h` | `qc71_ec_update_bits()`: read-modify-write under one module-wide lock, skipped when the value does not change; every write checks the model write policy |
| `events.c` | PH4TUX1 event codes: super key lock and Fan Boost notifications, charger event (`0xab`) re-applies settings, performance mode key (`0xb0`), keyboard backlight hardware change (`0xb3`/`0xb4`), mic mute (`0xb7`), screen lock (`0xa9`), vendor app key (`0xba` → `KEY_PROG1`), lid events silenced; no `SW_RFKILL_ALL` on PH4TUX1 (it re-enabled Bluetooth on every boot) |
| `fan.c` | On PH4TUX1 "full speed" is Fan Boost only (`0x0751` bit 6), the mode bits are kept and no raw fan speed is written |
| `features.c`, `features.h` | PH4TUX1 detection and feature flags (Fn lock, white keyboard backlight maximum) |
| `hwmon_pwm.c` | Raw `pwm1`/`pwm2` hidden on PH4TUX1; `pwm1_enable` 0/2 maps to Fan Boost on/off |
| `led_keyboard.c`, `led_keyboard.h` | White keyboard backlight detected from `0x078c` bit 0 (maximum level 2); the "off" bit (`0x078c` bit 1) is cleared when turning on; Fn+F6 reported through `brightness_hw_changed` |
| `misc.c` | Fn lock written with `qc71_ec_update_bits()` |
| `pdev.c` | All bit writes through `qc71_ec_update_bits()`; super key lock toggles only `0x0767` bit 0 and waits for the state bit (`0x0768` bit 0), with one retry; attributes that wrote the wrong bytes on PH4TUX1 are hidden there |
| `battery.c` | Charge limit written with `qc71_ec_update_bits()`; respects the write policy |
| `Makefile` | Builds the new objects; module version taken from `dkms.conf` |
| `dkms.conf` | Package version |

New files:

| File | Content |
|---|---|
| `uniwill_ext.c`, `uniwill_ext.h` | Charging profile and priority, performance profiles and `platform_profile`, Fan Boost, USB power share, touchpad toggle key, mic mute LED, fan tables and custom fan curve, fan duty/fault, project id, re-applying settings on resume, charger events and power supply changes |
| `models.c`, `models.h` | Per-model table and EC write policy: unknown models are refused unless `force=1`, and are then read-only |
| `fan_tables.h` | Fan tables written with each performance profile (the values the vendor application uses on this model, verified against the EC; see `THIRD_PARTY.md`) |

The authoritative list is a diff of this directory against the base commit
(`git fetch https://github.com/Slimbook-Team/qc71_laptop pull/5/head`).

## sysfs attributes added by `uniwill_ext.c`

All under `/sys/devices/platform/qc71_laptop/`, visible only where the
hardware supports them (most only on PH4TUX1):

`charging_profile`, `charging_priority`, `performance_profile`,
`performance_profile_available`, `fan_boost`, `usb_powershare`,
`touchpad_toggle_enable`, `fan_duty`, `fan_fault`, `fan_curve`, `project_id`,
plus a `platform_profile` handler and the `platform::micmute` LED.

Module parameters (`modinfo qc71_laptop`) apply values at load; nothing is
written at load unless a parameter asks for it.

## Verified hardware

Monster Huma H4 V4.1 (BIOS `N.1.15MON06`, EC project id 19 = `0x13`):

- `white:kbd_backlight` 0-2 (the same three levels as Fn+F6)
- hwmon: `fan1_input`, `temp1_input` (fan sensor temperature),
  `pwm1_enable` (0 = Fan Boost / full speed, 2 = automatic)
- `super_key_lock`, Fn lock, `charging_priority`, `charging_profile`
- the six performance profiles: the EC bytes written match the values measured
  under Windows with the vendor application; changing the GNOME power mode
  changes `platform_profile` and the EC together; Fan Boost keeps the mode bits
  (Balance + boost = `0x40`, 20dB + boost = `0xe0`)

Not supported on this model: battery charge limit and light bar (no DMI match).
Power-on when AC is connected is not stored in the EC but in the
`UniWillVariable` UEFI variable; the driver does not touch it.

## Behavior notes

- **Model table and write policy** (`models.c`): the driver is fully functional
  only on a verified model (DMI vendor/product/board/SKU plus the EC project
  id). The table also tells the driver about the PH4TUX1 extras, the white
  keyboard (maximum level 2) and the missing charge limit and light bar, so the
  `nobattery`/`nolightbar`/`kbd_white` options are not needed (they are still
  accepted). Power limit (PL1/PL2) and fan table writes are allowed only on the
  listed BIOS versions (`N.1.15MON06`); on any other BIOS they are read-only.
  On an unknown model the module does not load; `force=1` loads it read-only
  (every write returns `-EPERM`), and `force=1 allow_writes=1` enables writes
  and taints the kernel (`TAINT_USER`). The policy is checked again in
  `qc71_ec_transaction()`. Autoloading uses `MODULE_DEVICE_TABLE(dmi)`, so it
  happens only on models in the table (not through the `wmi:` GUID alias).
- **Slow EC access:** on this machine every EC access goes through ACPI-WMI and
  takes about 30-80 ms. Fan tables are therefore written in the background
  (the vendor application also queues them) and only bytes that differ from
  the previous table are written. Reading the performance profile waits for
  the write lock so that a half-written profile never reads as `unknown`.
- **Performance profiles:** `balanced-low`/`-medium`/`-high` (PL1 30/32/38 W,
  High is 35 W on battery, PL2 = EC default `0x0731`) and
  `quiet-20db`/`-30db`/`-40db` (`0x0751` = `0xa0`, PL1 15/25/35 W, PL2 35 W,
  `0x07c5` bit 4 at 30/40 dB). Write order as in the vendor application:
  `0x0751` (bit 6 kept) -> `0x07c5` -> PL1 -> PL2. PL4 (`0x0785`), `0x0786` and
  `0x0727` are never written. `platform_profile` maps `low-power` = quiet-20db,
  `quiet` = quiet-30db, `balanced` = balanced-medium,
  `performance` = balanced-high; anything else reads as `custom`.
- **Fan tables** (`fan_tables.h`, M1T1-M2T3) are the tables read back from the
  EC (`0x0f00`-`0x0f2f`) while the vendor application was running. Sequence:
  clear `0x07c6` bit 2 -> `0x07c5` bit 7 = 0 -> write `0x0f00`-`0x0f2f` -> set
  `0x07c6` bit 2. Identical tables are not rewritten; the table is verified by
  reading it back on resume and on power source changes. On unload the table
  is cleared and the EC returns to its own curve. `fan_table=0` disables this.
- **Custom fan curve** (`fan_curve`, rw): `auto` = vendor tables, or 48 numbers
  (16 rising thresholds, 16 falling thresholds, 16 duty values in % x 2) used
  as the table in every mode. Safety checks: thresholds 30-85 °C and
  increasing, each falling threshold below its rising one, duty never
  decreasing, last step 100%; the EC's own overheat protection (`0x07c5` bit 4)
  stays on with a custom table. It persists through the `fan_curve=` module
  option (`/etc/modprobe.d/qc71_laptop-fan-curve.conf`). `fan_fault` (ro) is
  `0x0741` bit 5. Changes to performance, Fan Boost and the fan curve emit a
  udev `change` event (`HUMA_CONTROL=performance|fan_boost|fan_curve`).
- **Charging profile:** the EC accepts a battery health write only while
  `0x0741` bit 0 ("control application present") is 1. As in the vendor
  application and upstream `uniwill-laptop`, the driver sets it to 1 at load
  and 0 at unload (with 0 the EC falls back to High capacity).
- **Re-applying settings:** on resume (PM notifier), on AC power source changes
  (power_supply notifier) and on the charger event `0xab`, the last requested
  values (charging profile and priority, USB power share, touchpad toggle key,
  Fn lock, performance profile, Fan Boost) are written again. At load only the
  given module options are applied (`charging_profile`, `charging_priority`,
  `performance_profile`, `usb_powershare`, `touchpad_toggle`, `fn_lock`);
  anything not given is left alone. `0x0766 |= 0x03` is set at load and on
  re-apply, as the vendor application does.
- **Super key lock:** only `0x0767` bit 0 is toggled (bit 4 is USB power in
  hibernation). The EC swallows a trigger that arrives within ~0.5 s of the
  previous change, so the driver waits for the state bit (`0x0768` bit 0),
  with a 700 ms wait and one retry.
- **Mode key** (`0xb0`): toggles Balanced <-> Quiet and keeps the last level of
  each mode; if the EC changed the mode by itself it is only tracked.
  `mode_key=0` disables this.
- **White keyboard backlight:** detected from `0x078c` bit 0 (maximum 2); when
  turning it on, `0x078c` bit 1 (the "off" bit used on Windows) is cleared, and
  if bit 1 is set the level reads as 0. `0xb3`/`0xb4` (Fn+F6) no longer send
  `KEY_KBDILLUMTOGGLE`; they report through `brightness_hw_changed`.
- **Events:** `0x40`/`0x41` super key lock, `0xa7`/`0xac` Fan Boost, `0xb0`
  performance, `0xb7` -> `KEY_MICMUTE`, `0xa9` -> `KEY_SCREENLOCK`, `0xba` ->
  `KEY_PROG1` (the GNOME shortcut `Launch1` opens the application); `0xc3`/`0xc4`
  (lid) and `0x43` (sent after every Fan Boost change) are silenced.
- **rfkill:** the input device used to report `SW_RFKILL_ALL = 1`. The
  kernel's rfkill input handler applied that as "unblock all radios" whenever
  the desktop did not hold `/dev/rfkill` (at boot, right after systemd-rfkill
  restored the saved state), so Bluetooth turned itself on at every boot.
  PH4TUX1 has no hardware radio switch, so `SW_RFKILL_ALL` is not reported;
  the airplane mode key (`0xa4`, `KEY_RFKILL`) is left to the desktop.

## Version history

- **0.2** Charging profile (`0x07a6` bits 5:4, support flag `0x078e` bit 3)
  and charging priority (`0x07cc` bit 7, support flag `0x0742` bit 5), with
  addresses from TUXEDO's tuxedo-drivers (same chassis); Fn lock (`0x074e`
  bit 4) enabled for PH4TUX1.
- **0.3** Fixes based on Windows measurements: `qc71_ec_update_bits()`,
  Fan Boost-only "full speed" on PH4TUX1, attributes that wrote wrong bytes
  hidden, super key lock bit-only write with retry, charging profile flag.
- **0.4** Performance profiles, `platform_profile`, Fan Boost, USB power
  share, touchpad toggle key, re-applying settings, new event handling.
- **0.5** Fan tables per performance profile, mode key, white keyboard
  backlight auto-detection, mic mute LED, `fan_duty`, `project_id`, module
  version from `dkms.conf`.
- **0.6** Background fan table writes, only changed bytes written; a profile
  change dropped from 4-7 s to about 1 s.
- **0.7** No `SW_RFKILL_ALL` on PH4TUX1 (Bluetooth no longer turns on at boot).
- **0.8** Custom fan curve (`fan_curve`), `fan_fault`, udev change events.
- **0.10** (current) Model table and EC write policy (`models.c`).

## Building

```sh
make                      # against the running kernel
sudo make dkmsinstall     # or let ../install.sh do it
```
