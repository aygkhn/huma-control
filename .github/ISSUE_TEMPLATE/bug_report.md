---
name: Bug report
about: Something does not work as expected
title: ""
labels: bug
---

**What happened**

**What you expected**

**Steps to reproduce**

1.
2.

**Component**

- [ ] Kernel driver (`qc71_laptop`)
- [ ] Desktop application
- [ ] GNOME Shell extension
- [ ] System services / helpers
- [ ] Installer (`install.sh`)

**Environment**

- Huma Control Center version or commit:
- Laptop model and BIOS version:
- Distribution and kernel (`uname -r`):
- GNOME Shell version:

**Hardware report**

Please run `tools/hw-report.sh`, read the file it writes and attach it
(it contains no serial numbers).

**Logs**

```
journalctl -k -b | grep -iE 'qc71|uniwill'
journalctl -b -u huma-control-service -u huma-control-keyboard
```

Security problems: do not open an issue, see SECURITY.md.
