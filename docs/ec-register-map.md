# EC register map: Uniwill PH4TUX1

This is the embedded controller (EC) map used by the `qc71_laptop` fork in
`driver/`. It covers the Uniwill
**PH4TUX1** barebone (EC project id `0x13`), sold as Monster Huma H4 V4.1 and,
with the same chassis, as TUXEDO InfinityBook Pro 14 Gen6.

Reference machine: Monster Huma H4 V4.1, BIOS N.1.15MON06, vendor Control
Center 4.8.47.13 on Windows 11.

## How the values were obtained

- **Measured**: a setting was changed in the vendor application, then the EC
  range `0x0700-0x07ff` was dumped read-only and compared with the dump taken
  before. Self-changing bytes (sensors, counters) were identified beforehand
  with two dumps taken without touching anything. The raw dumps and lab notes are
  not part of this repository.
- **Observed behaviour**: what the vendor application writes to the EC, the
  registry and the UEFI variable when a setting is changed, on charger
  plug/unplug, on resume and when its service starts or stops.
- **Cross-checked** with the open-source drivers `tuxedo-drivers` and upstream
  `uniwill-laptop` for the same chassis, and with the original `qc71_laptop`
  register definitions (`driver/ec.h`).
- **Verified on Linux** by writing through the driver and reading back via
  debugfs.

Legend used in the tables: **M** measured, **O** observed behaviour of the
vendor application, **T** matches tuxedo-drivers, **Q** matches qc71_laptop,
**L** verified on Linux.

Other Uniwill models share many of these addresses but not all of them: the
same register can mean something else on another barebone. Do not write any of
these on a machine whose project id is not `0x13` without checking first
(see `tools/hw-report.sh` and `CONTRIBUTING.md`).

## Access method

The EC is reached through ACPI-WMI, on Linux and on Windows.

| | |
|---|---|
| Method GUID | `ABBC0F6F-8EA1-11D1-00A0-C90629100000` (`AcpiTest_MULong`) |
| Method | id 4, `GetSetULong` |
| Event GUID | `ABBC0F72-8EA1-11D1-00A0-C90629100000` (`AcpiTest_EventULong`) |
| Other event GUIDs | `ABBC0F70-…` (`AcpiTest_EventPackage`), `ABBC0F71-…` (`AcpiTest_EventString`) |

Addresses are 16 bit (`page << 8 | offset`).

Linux (`driver/ec.c`, `wmi_evaluate_method`): an 8 byte input buffer
`{addr_lo, addr_hi, data_lo, data_hi, 0, read ? 1 : 0, 0, 0}`; the value is
the first byte of the returned buffer.

Windows (WMI class `AcpiTest_MULong` in `root\wmi`, instance `ACPI\PNP0C14\1_1`):
input `Data` (UInt64), output `Return` (UInt32).

- read: `Data = addr | (1 << 40)`, value = low byte of `Return`
- write: `Data = (value << 16) + addr` (no flag)

Timing: each access takes about 30-80 ms on this machine (a full 256 byte
dump takes ~38 s on Windows). The vendor application waits ~10 ms after each
access. Keep the number of accesses low, serialize them behind one lock and do
bit changes as read-modify-write (RMW). Only `0x0751` is written as a full byte.

## Identification and capability bits

| Address | Value here | Meaning | Source |
|---|---|---|---|
| `0x0740` | `0x13` | Project id; `0x13` = PH4TUX1 | M T Q |
| `0x0742` | `0x22` | bit 5: USB-C power priority supported | O T |
| `0x0765` | `0xa0` | bit 5: super key lock supported, bit 7: Fan Boost supported | Q |
| `0x0766` | `0xf3` | bit 0: silent mode, bit 1: USB charging; the host sets bits 0-1 at start | O Q |
| `0x0782` | `0x49` | bit 4: default mode is Balance; bit 1: mode key cycles performance modes | O |
| `0x078c` | `0x43` | bit 0: single-colour (white) keyboard backlight present | O T Q |
| `0x078e` | `0x6c` | bit 6: RAM fan table supported, bit 5: keyboard backlight power control, bit 3: charging profile supported | O T |
| `0x049f` | `0x30` | bit 1: turbo mode available (0 here, so no turbo) | O |
| `0x0730-0x0733` | `20 3c 6b 01` | EC defaults for Balance: PL1, PL2, PL4, - | M |
| `0x0734-0x0737` | `0f 23 6b 01` | EC defaults for Quiet: PL1, PL2, PL4, - | M |

## Control registers

| Address | Bits | Meaning | Access | Source |
|---|---|---|---|---|
| `0x0741` | bit 0 | "Application present". Set while control software runs; the EC accepts a new charging profile only while it is 1 | RMW | O T L |
| `0x0741` | bit 5 | Fan fault (read only) | read | Q |
| `0x074e` | bit 4 | Fn lock (1 = locked) | RMW | O T Q L |
| `0x0751` | full byte | Performance mode: `0x00` Balance, `0xa0` Quiet (bits 7 and 5). Bit 4 would be turbo (not present) | full byte, keep bit 6 | M O T L |
| `0x0751` | bit 6 | Fan Boost (fan at full speed); `0x40` in Balance, `0xe0` in Quiet | RMW | M O T L |
| `0x075b` | byte | CPU fan duty, percent × 2 | read | M |
| `0x075c` | byte | Reads `0xc8` while Fan Boost is on | read | M |
| `0x0767` | bit 0 | Super key (Windows key) lock **trigger** | RMW | M O T Q L |
| `0x0767` | bit 4 | USB power while off / hibernating (level, not a trigger) | RMW | M O T |
| `0x0768` | bit 0 | Super key lock state (1 = locked) | read | M O |
| `0x0768` | bit 2 | Fan Boost state | read | M O |
| `0x0783` | byte | PL1 in W | byte | M O T L |
| `0x0784` | byte | PL2 in W | byte | M O L |
| `0x078c` | bits 7:5 | White keyboard backlight level 0-2; bit 4 must be set when writing a level | RMW | O T Q L |
| `0x078c` | bit 1 | Keyboard backlight off (1 = off) | RMW | O L |
| `0x07a6` | bit 2 | Microphone mute LED | RMW | O L |
| `0x07a6` | bits 5:4 | Charging profile: 0 high capacity, 1 balanced, 2 stationary | RMW | M O T L |
| `0x07a6` | bit 6 | Touchpad toggle hotkey **disabled** (1 = Fn shortcut off) | RMW | M O L |
| `0x07c5` | bit 4 | Set for the Quiet 30 dB and 40 dB levels, clear for 20 dB; related to the EC's fan safety handling, exact meaning unknown | RMW | M O |
| `0x07c5` | bit 7 | Separate CPU/GPU fan tables (0 here) | RMW | O T |
| `0x07c6` | bit 2 | Use the RAM fan table at `0x0f00` | RMW | M O T L |
| `0x07cc` | bit 7 | USB-C power priority: 0 charge battery, 1 performance | RMW | M O T L |
| `0x0464:0x0465` | word | Fan speed in RPM, big-endian | read | M Q |
| `0x043e` | byte | Fan temperature sensor (°C) | read | Q |

Measured examples:

- Fan Boost on: `0x0751` `0x00`→`0x40`, `0x0768` `0x00`→`0x04`, fan ~2520 → ~6000 RPM.
- Charging profile: `0x07a6` `0x18` (balanced) → `0x08` (high capacity) → `0x28` (stationary).
- Touchpad toggle off: `0x07a6` `0x18`→`0x58`.
- USB-C performance priority: `0x07cc` `0x01`→`0x81`.

### Performance profiles

| Profile | `0x0751` | PL1 (`0x0783`) | PL2 (`0x0784`) | `0x07c5` bit 4 | Fan table |
|---|---|---|---|---|---|
| Balance Low | `0x00` | 30 W | EC default (`0x0731`, 60 W) | 0 | M1T1 |
| Balance Medium (default) | `0x00` | 32 W | EC default | 0 | M1T2 |
| Balance High | `0x00` | 38 W (35 W on battery) | EC default | 0 | M1T3 |
| Quiet 20 dB | `0xa0` | 15 W | 35 W | 0 | M2T1 |
| Quiet 30 dB | `0xa0` | 25 W | 35 W | 1 | M2T2 |
| Quiet 40 dB | `0xa0` | 35 W | 35 W | 1 | M2T3 |

The driver maps them to `platform_profile`: `low-power` = Quiet 20 dB,
`quiet` = Quiet 30 dB, `balanced` = Balance Medium, `performance` = Balance
High; anything else reads as `custom`.

### Fan table

| Range | Content |
|---|---|
| `0x0f00-0x0f0f` | 16 "up" thresholds in °C (unused steps `0xff`) |
| `0x0f10-0x0f1f` | 16 "down" thresholds in °C |
| `0x0f20-0x0f2f` | 16 duty values, percent × 2 (`0xc8` = 100 %) |
| `0x0f30-0x0f5f` | GPU fan table (not present on this model) |

The tables the vendor application uses (read back from the EC) peak at
60 % (M1T1), 85 % (M1T2), 100 % (M1T3), 30 % (M2T1); M2T2 equals M1T1 and M2T3
equals M1T2. Thresholds start at 54/60/65/70/73 °C, the "down" steps are about
3 °C lower. The values are in `driver/fan_tables.h`.

A custom curve (`fan_curve`) is accepted only if: thresholds are 30-85 °C and
increasing, each down threshold is below its up threshold, duty never
decreases, the last step is 100 %, and the duty at 70 °C is at least 20 %.

### Other addresses

| Address | Notes |
|---|---|
| `0x0785` (PL4), `0x0786` | Not written by the vendor application on this model; the driver does not write them |
| `0x0727` bit 6 | "Custom mode" in qc71_laptop; not used on this model, not written |
| `0x07b9`, `0x07d0` | Charge limit on other models; not supported here, not written |
| `0x1804`, `0x1809` | Raw fan PWM on other models; writing it can stop the fan, not exposed on PH4TUX1 |
| `0x07a4` bit 3 | "Fn lock switch" in qc71_laptop; meaning unknown here, not exposed |
| `0x0726` bit 3 | AC auto power-on on some TUXEDO models; unused here (see UEFI below) |
| `0x070a`, `0x070f`, `0x0713`, `0x0714`, `0x0716-0x0718` | Change on their own (sensors, counters) |

## Write order and timing

**Performance profile**: `0x0751` (keep bit 6) → `0x07c5` bit 4 → fan table →
PL1 (`0x0783`) → PL2 (`0x0784`). The vendor application re-applies PL1/PL2 when
the power source changes (Balance High uses 35 W on battery).

**Fan table**: clear `0x07c6` bit 2 → clear `0x07c5` bit 7 → write
`0x0f00-0x0f2f` (only bytes that differ from the previous table) → set
`0x07c6` bit 2. Skip the whole sequence if the table is unchanged. Because each
access is slow, the driver does this in the background and reads the table back
after resume and charger changes. On unload the table is cleared and the EC
returns to its own curve.

**Super key lock**: read `0x0768` bit 0; if it differs from the requested
state, set `0x0767` bit 0 (RMW, leave bit 4 alone). The EC toggles the state
and clears the trigger. It ignores a trigger that comes within about 0.5 s of
the previous change, so wait ~700 ms, re-read `0x0768` and trigger once more if
needed.

**Charging profile**: `0x0741` bit 0 must be 1, otherwise the EC drops the new
value at once. The driver sets it at load and clears it at unload (the EC then
returns to high capacity).

**USB power while off**: set `0x0766 |= 0x03`, then `0x0767` bit 4. The vendor
UI turns AC power-on off when USB power is enabled (they are mutually exclusive
in its UI; the one-way switch is done by the UI, not the EC).

**Keyboard backlight**: RMW on `0x078c`, keep the low nibble, write the level
into bits 7:5 and set bit 4 (apply). When turning on also clear bit 1 (the
vendor application may leave it set to mean "off"); a level of 0 turns the
backlight off. Some white keyboards ignore a new level while off until
`0x1808` is non-zero, so the driver writes `0x01` there first if it reads 0
(same workaround as tuxedo-drivers).

**Re-applying settings**: the EC may lose settings on charger plug/unplug,
across suspend (the machine uses s2idle) and after booting Windows. The driver
rewrites the last requested values (charging profile and priority, USB power,
touchpad toggle, Fn lock, performance profile, Fan Boost, fan table) on
resume, on a mains power supply change and on EC event `0xab`. At load it only
writes values given as module parameters.

## EC events

Delivered as integers on the event GUID `ABBC0F72-…`.

| Code | Meaning | Driver action |
|---|---|---|
| `0x01-0x05` | Caps/Num/Scroll lock, touchpad OSD | ignored |
| `0x40` / `0x41` | Super key lock changed | sysfs notification |
| `0x43` | Follows every Fan Boost change | ignored |
| `0xa4` | Airplane mode key | `KEY_RFKILL` (left to the desktop) |
| `0xa7` | Fan Boost key | sysfs notification |
| `0xa9` | Screen lock key | `KEY_SCREENLOCK` |
| `0xab` | Charger plugged / unplugged | re-apply settings |
| `0xac` | Fan Boost turned off by the EC | sysfs notification |
| `0xb0` | Performance mode key | Balance ↔ Quiet, each mode keeps its last level (`mode_key=0` disables) |
| `0xb3` / `0xb4` | Keyboard backlight level / power (Fn+F6) | `brightness_hw_changed` |
| `0xb7` | Microphone mute key | `KEY_MICMUTE` |
| `0xb8` | Fn lock (Fn+Esc) | sysfs notification |
| `0xba` | Vendor control-center key | `KEY_PROG1` |
| `0xc3` / `0xc4` | Lid | ignored |

On PH4TUX1 there is no hardware wireless switch, so `SW_RFKILL_ALL` is not
reported (reporting it made the kernel unblock Bluetooth on every boot).

## UEFI variable `UniWillVariable`

Not an EC register, but some settings live here.

| | |
|---|---|
| GUID | `9f33f85c-13ca-4fd1-9c4a-96217722c593` |
| Size | 180 bytes, attributes `0x07` (efivarfs file: 4 attribute bytes + 180 data bytes = 184) |
| Byte 6 | Project id (`0x13`) |
| Byte 47 | Power mode last used by the vendor application (1 = Balance) |
| Byte 94 | AC power-on ("AC Recover") supported (1) |
| Byte 95 | **AC power-on state**, 1 = on; takes effect on the next boot |
| Byte 103 | Fn lock copy kept by the vendor application |

Measured: switching AC power-on off and on in the vendor application changes
only byte 95 (1 → 0 → 1); nothing changes in the EC or the registry. The rest
of the layout is unknown.

Safe write procedure (`system/huma-control-uefi`):

1. Read the whole variable and keep a backup.
2. Check: size 184, attributes `07 00 00 00`, data[6] = `0x13`, data[94] = 1.
   Refuse to write otherwise.
3. Change only data[95]; skip the write if it already has the requested value.
4. `chattr -i`, write the 184 bytes with one `write()`, `chattr +i`.
5. Read back and compare all 180 bytes; on any difference restore the backup
   and fail.

Reading on Linux:

```sh
f=/sys/firmware/efi/efivars/UniWillVariable-9f33f85c-13ca-4fd1-9c4a-96217722c593
dd if=$f bs=1 skip=$((4+94)) count=2 2>/dev/null | od -An -tu1   # support, state
```

## Not implemented on purpose

- Syncing UEFI byte 47 (power mode) on every mode change: it would wear the
  flash; on Linux the mode comes from the GNOME power mode.
- UEFI byte 103 for Fn lock: the module parameter is enough on Linux.
- PL4, `0x0786`, `0x0727`, raw fan PWM, charge limit: see "Other addresses".
- Periodic "keep-alive" writes: the vendor application does not do any.

## Relation to upstream `uniwill-laptop`

The in-kernel `uniwill-laptop` driver (Linux 6.19+) knows this chassis as
InfinityBook Pro 14 Gen6, but matches on DMI vendor `TUXEDO`, so it does not
bind on the Monster-branded machine. Loading it with `force=1` is **not**
recommended here: at load it writes `0x0743-0x0746` (RGB keyboard) and assumes
AC power-on lives in `0x0726`. Sysfs names in this fork follow upstream where
an upstream attribute exists (`touchpad_toggle_enable`, `fn_lock`, ...), so the
user-space side can switch later. Features upstream does not have (performance
profiles and PL1/PL2, `0x07c5` bit 4, fan tables) would stay in a small
out-of-tree module; two drivers must never write `0x0751` at the same time.
