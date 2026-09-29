# Contributing to Huma Control Center

Thanks for helping. This project controls laptop hardware through the
embedded controller (EC), so correctness matters more than speed: a wrong
register write can stop a fan or change power limits. Please read
[docs/ec-register-map.md](docs/ec-register-map.md) before touching anything
that writes to the EC.

## Repository layout

| Path | What | Language |
|---|---|---|
| `driver/` | `qc71_laptop` kernel module fork (DKMS), GPL-2.0-only; see `driver/README.md` | C |
| `app/` | Desktop application (GTK 4 / libadwaita), `.desktop` file, icons | Python |
| `huma-control@aygkhn.github.io/` | GNOME Shell extension (Quick Settings, OSD, notifications) | JavaScript (GJS) |
| `system/` | Root helpers, systemd services, udev and polkit rules, modprobe config, YuNet model | Bash, Python |
| `po/`, `app/po/` | Translations (gettext) of the extension and of the app | |
| `tools/` | Developer tools, e.g. `hw-report.sh` | Bash |
| `docs/` | English documentation, EC register map | Markdown |
| `install.sh` | Installer / uninstaller (`--uninstall`) | Bash |

Code, comments, file names and configuration keys are in English.

## Building and installing

```sh
./install.sh               # builds the driver with DKMS, installs helpers, services, app and extension
./install.sh --uninstall   # removes everything again
```

Driver only:

```sh
cd driver && make                       # needs kernel-devel for the running kernel
sudo rmmod qc71_laptop; sudo insmod ./qc71_laptop.ko
```

The project is developed and tested on **Fedora Linux 44 Workstation** (GNOME 50,
Wayland, tuned + tuned-ppd). Other distributions are not tested yet; `install.sh`
uses `dnf`. Ports to other distributions are welcome.

## Code style

**Kernel driver (`driver/`)**
- Linux kernel coding style: tabs, 80-100 columns, `/* */` comments.
  Run `scripts/checkpatch.pl --no-tree -f <file>` from a kernel tree.
<!-- REUSE-IgnoreStart -->
- Every file keeps `SPDX-License-Identifier: GPL-2.0-only`.
<!-- REUSE-IgnoreEnd -->
- EC bit changes go through `qc71_ec_update_bits()`; never write a whole byte
  unless the register is documented as a full-byte register.
- New writable features must be gated on the model (project id and, where one
  exists, the EC capability bit) and must respect the write policy in
  `models.c`.
- Keep EC accesses few: each one costs 30-80 ms through WMI.

**Python (`app/`, `system/`)**
- PEP 8, 4 spaces, standard library plus PyGObject; no new runtime
  dependencies without discussion.
- Do not block the GTK main loop with EC or file I/O; use a worker thread.
- Root helpers validate every argument against a fixed list or strict regex
  and fail closed.

**Shell (`install.sh`, `system/huma-control-helper`, `tools/`)**
- Bash with `set -euo pipefail`, quote every expansion, `shellcheck` clean.

**GNOME Shell extension**
- ES modules, GNOME Shell extension guidelines (no work in the constructor,
  clean up everything in `disable()`), 4 spaces.

<!-- REUSE-IgnoreStart -->
New files get SPDX headers:

```
SPDX-License-Identifier: GPL-2.0-or-later      (GPL-2.0-only in driver/)
SPDX-FileCopyrightText: <year> <your name> <email>
```
<!-- REUSE-IgnoreEnd -->

## Testing on hardware

1. Work with the driver unloaded or loaded from your build, never with the
   vendor software running at the same time (on dual boot, the vendor Windows
   service rewrites some values after boot).
2. Before and after each change, read the relevant sysfs files and, for driver
   work, the EC bytes via debugfs (`modprobe qc71_laptop debugregs=1`, see
   `tools/hw-report.sh --ec`). Compare with the expected values in the
   register map.
3. Check the cases where the EC tends to lose state: suspend/resume, charger
   plug/unplug, reboot from Windows, module reload.
4. Watch `journalctl -k -f` for warnings, and fan speed/temperature
   (`sensors`) after any fan table change.
5. Mention in the pull request what you tested, on which model and BIOS
   version.

## Adding a new model

1. Run `tools/hw-report.sh` (no root needed), read the report and attach it to
   a **New model** issue. It contains DMI data (no serial numbers), the WMI
   GUIDs, the EC project id if the driver can read it, LEDs and battery model.
2. If the machine has Uniwill WMI GUIDs (`ABBC0F6F-…`, `ABBC0F72-…`) but an
   unknown project id, the driver refuses to load. `force=1` loads it
   read-only, which is enough to read sysfs values and collect EC dumps.
3. Compare your EC dumps with `docs/ec-register-map.md`. Measure each setting
   the same way it was done for PH4TUX1: change one setting in the vendor
   software, dump `0x0700-0x07ff`, diff. Record which bytes changed.
4. Add the model to the table in `driver/models.c` with only the features you
   verified; leave power-limit and fan-table writes off until they are
   verified on that BIOS.
5. Never enable writes to a register whose meaning on your model is only
   guessed from another model.

## Translations

The msgids are English. The desktop app (and its launcher) uses `app/po/`
(gettext domain `huma-control`), the GNOME Shell extension uses `po/`. The root
services write English texts plus stable codes that the app and extension
translate.

App (run from the project directory):

```sh
make -C app/po update       # re-extract the strings, update app/po/*.po
msginit -i app/po/huma-control.pot -l de -o app/po/de.po && echo de >> app/po/LINGUAS
make -C app/po check        # check; install.sh compiles them
```

Extension (run from the project directory):

```sh
# extract strings
xgettext --from-code=UTF-8 -k_ -kpgettext:1c,2 -kngettext:1,2 -L JavaScript \
    -o po/huma-control.pot huma-control@aygkhn.github.io/*.js
# new language
msginit -i po/huma-control.pot -l de -o po/de.po
# existing language
msgmerge -U po/tr.po po/huma-control.pot
# compile into the extension (the .mo files are committed on purpose)
lang=tr
mkdir -p huma-control@aygkhn.github.io/locale/$lang/LC_MESSAGES
msgfmt --check-format -o huma-control@aygkhn.github.io/locale/$lang/LC_MESSAGES/huma-control@aygkhn.github.io.mo po/$lang.po
```

Open a **Translation** issue or a pull request with the `.po` file and the
regenerated `.mo`.

## Pull requests

- One topic per pull request; describe what changed and how it was tested.
- Commit messages: short summary line, then details, in English.
- By contributing you agree that your contribution is licensed under the
  license of the files you change (GPL-2.0-or-later, or GPL-2.0-only in
  `driver/`).
- Security issues: see [SECURITY.md](SECURITY.md), do not open a public issue.
