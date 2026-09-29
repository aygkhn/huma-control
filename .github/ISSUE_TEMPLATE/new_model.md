---
name: New model
about: Request or help with support for another Uniwill-based laptop
title: "New model: <brand> <model>"
labels: new-model
---

**Laptop**

- Brand and model:
- Barebone / chassis name, if known (e.g. PH4TUX1):
- BIOS version:
- Vendor control software and version (Windows), if any:

**Hardware report**

Run `tools/hw-report.sh` (no root needed), read the file and attach it.
If the driver refuses to load on your machine, `sudo modprobe qc71_laptop force=1`
loads it read-only; run the report again after that.

**What works / what you need**

- [ ] Keyboard backlight
- [ ] Performance modes / power limits
- [ ] Fan Boost / fan curve
- [ ] Charging profile / USB-C priority
- [ ] Fn lock / Windows key lock
- [ ] Other:

**Measurements (optional but very helpful)**

If you can compare with the vendor software, describe which setting you
changed and attach EC dumps taken before and after (see
`tools/hw-report.sh --ec` and CONTRIBUTING.md, "Adding a new model").
Please do not write to EC registers yourself.
