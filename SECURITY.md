# Security policy

Huma Control Center changes hardware settings: it loads a kernel module that writes
to the laptop's embedded controller (EC), runs helpers as root and can change a
UEFI variable. Please report anything that lets an unprivileged user, another
local user or a remote party reach those paths, or that can damage hardware or
firmware state.

## Reporting a vulnerability

Please **do not open a public issue** for security problems.

Use GitHub's private vulnerability reporting: open the repository's
**Security** tab and choose **Report a vulnerability** (a private security
advisory). If that is not available, contact the maintainer listed in
`AUTHORS` by e-mail and put `[security]` in the subject.

Please include the affected version or commit, the component, steps to
reproduce and the impact you expect. You should get an answer within 7 days.
Fixes are released as soon as possible and credited in `CHANGELOG.md` unless
you prefer otherwise.

Supported versions: only the latest release and the main branch.

## Privileged components

| Component | Runs as | Installed to | Reached by |
|---|---|---|---|
| `qc71_laptop` kernel module (`driver/`) | kernel | DKMS | sysfs under `/sys/devices/platform/qc71_laptop/`, hwmon, LEDs, `platform_profile` |
| `huma-control-helper` (bash) | root | `/usr/local/libexec/` | `pkexec`, polkit rule below |
| `huma-control-power` (bash, optional) | root | `/usr/local/libexec/` | `pkexec`, polkit rule below; writes `/etc/tuned/ppd.conf` and `/etc/udev/rules.d/*-huma-control-cardreader-*.rules` |
| `huma-control-uefi` (Python) | root | `/usr/local/libexec/` | `pkexec`, admin authentication |
| `huma-control-service`, `huma-control-keyboard` (Python) | root, systemd services (`ProtectSystem=strict`, `NoNewPrivileges=yes`) | `/usr/local/libexec/`, `/etc/systemd/system/` | settings in `/etc/huma-control/` (root-owned, written only by the helper) |
| `huma-control-light`, `huma-control-presence` | the calling user (GNOME extension, app) or the keyboard service | `/usr/local/libexec/` | camera access for ambient light / presence check |

### polkit rules (`system/50-huma-control.rules`)

- `huma-control-helper`: allowed **without a password** for users who are
  local, active and in the `wheel` group. This covers everyday settings
  (performance profile, Fan Boost, keyboard backlight, charging options, fan
  curve). The helper validates every argument against a fixed list or a strict
  pattern, holds a lock (`/run/huma-control-helper.lock`) and writes
  settings files atomically after validating them.
- `huma-control-power`: allowed **without a password** for the same users.
  It takes exactly one of three fixed settings and `on`/`off` (power-saving
  tuned profile on battery, SD card reader sleep, SD card reader off); the udev
  rules it writes are generated from a fixed device list in the script.
- `huma-control-uefi`: requires **administrator authentication**
  (`auth_admin_keep`) for the same users.

Anyone who can run programs in an active local `wheel` session can therefore
change these settings without a password. They cannot run arbitrary commands
through the helper. Reports of argument injection, path traversal, TOCTOU on
the settings files or ways around the validators are very welcome.

### UEFI write path

`huma-control-uefi` changes one byte (AC power-on) of the vendor variable
`UniWillVariable-9f33f85c-13ca-4fd1-9c4a-96217722c593`. Before writing it
checks the size (184 bytes including attributes), the attributes (`0x07`), the
project id and the support byte, keeps backups in `/var/lib/huma-control/`
(`UniWillVariable.first`, never overwritten, and `UniWillVariable.backup`),
toggles the immutable flag only around a single `write()`, reads the variable
back and restores the backup on any difference. A bug here could leave the
firmware variable corrupted, so this path gets extra scrutiny.

### Kernel driver EC writes

The driver writes EC registers only through a small set of sysfs attributes,
module parameters and re-apply hooks (resume, charger events). Bit changes are
read-modify-write under one lock. Writes that are known to be wrong or unsafe on
a model (raw fan PWM, PL4, unknown bits) are hidden there. Unknown models are
refused unless `force=1`, and are then read-only (writes additionally need
`allow_writes=1`, which taints the kernel). The custom fan curve is
checked before it is accepted (thresholds 30-85 °C, rising, fan at least 20 % by
70 °C, last step 100 %).
The debugfs interface (`debugregs=1`) allows raw EC reads and writes and is
meant for development only; it is off by default and root-only.

## Out of scope

- Settings changed by a user who already has root.
- The vendor's own firmware and Windows software.
- Denial of service by a local `wheel` user changing a setting they are
  allowed to change (for example selecting a quiet profile).
