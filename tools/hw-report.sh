#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#
# Collects a read-only hardware report for adding support for a new laptop
# model. Needs no root and writes nothing except the report file; it reads
# only sysfs/procfs (the driver's sysfs files query the EC read-only). Serial
# numbers and UUIDs are left out; review the file before sharing it.
#
# Usage: tools/hw-report.sh [-o FILE] [--ec]
#   -o FILE   write the report to FILE (default: ./hw-report-<date>.txt)
#   --ec      print how to add an EC dump (this script never accesses the EC directly)
set -euo pipefail

usage() {
    sed -n '5,12p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

ec_instructions() {
    cat <<'EOF'
EC register dump (optional, needs root and the qc71_laptop driver)
------------------------------------------------------------------
This script never accesses the embedded controller directly. If a maintainer
asks for an EC dump, the driver's debugfs interface can provide one. The
driver must be loaded with debugregs=1:

  sudo modprobe -r qc71_laptop
  sudo modprobe qc71_laptop debugregs=1
  # 256 bytes starting at 0x0700, read only:
  sudo dd if=/sys/kernel/debug/qc71_laptop/ec bs=1 skip=$((0x0700)) count=256 \
      status=none | od -Ax -tx1 > ec-0700.txt

Every EC access goes through ACPI-WMI and is slow (tens of ms), so this takes
several seconds. Only read: the same debugfs file is writable, and on some
models a wrong byte can stop the fan or change power limits. Reload the driver
without debugregs=1 afterwards.

Attach the dump to the issue together with this report and say what the
machine was doing (on battery or AC, which mode was selected in the vendor
software, ...).
EOF
}

out=""
want_ec=0
while [ $# -gt 0 ]; do
    case "$1" in
        -o) [ $# -ge 2 ] || usage 2; out=$2; shift 2 ;;
        --ec) want_ec=1; shift ;;
        -h|--help) usage 0 ;;
        *) usage 2 ;;
    esac
done

if [ "$want_ec" = 1 ]; then
    ec_instructions
    exit 0
fi

[ -n "$out" ] || out="./hw-report-$(date +%Y%m%d-%H%M%S).txt"

# Prints a file's content or "-" if it is missing/unreadable
val() {
    local v
    if [ -r "$1" ] && v=$(tr -d '\0' < "$1" 2>/dev/null); then
        printf '%s\n' "${v:--}"
    else
        echo "-"
    fi
}

section() { printf '\n## %s\n\n' "$1"; }

report() {
    echo "# Huma Control Center hardware report"
    echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ) by tools/hw-report.sh"
    echo "No serial numbers or UUIDs are included. Review before sharing."

    section "DMI"
    local f
    for f in sys_vendor product_name product_version product_family product_sku \
             board_vendor board_name board_version \
             chassis_vendor chassis_type chassis_version \
             bios_vendor bios_version bios_date bios_release ec_firmware_release; do
        printf '%-20s %s\n' "$f:" "$(val "/sys/class/dmi/id/$f")"
    done

    section "System"
    echo "kernel:  $(uname -r) ($(uname -m))"
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        echo "distro:  $(. /etc/os-release && echo "${PRETTY_NAME:-${NAME:-unknown}}")"
    fi
    if [ -d /sys/firmware/efi ]; then
        echo "boot:    UEFI"
    else
        echo "boot:    legacy BIOS"
    fi
    if command -v mokutil >/dev/null 2>&1; then
        echo "secure boot: $(mokutil --sb-state 2>/dev/null | head -n1 || echo unknown)"
    fi
    echo "platform_profile: $(val /sys/firmware/acpi/platform_profile)"
    echo "platform_profile_choices: $(val /sys/firmware/acpi/platform_profile_choices)"

    section "Loaded modules (qc71 / uniwill / tuxedo)"
    if [ -r /proc/modules ]; then
        grep -E '^(qc71|uniwill|tuxedo|clevo)' /proc/modules | awk '{print $1, $2}' || echo "(none)"
    fi

    section "modinfo"
    local m
    for m in qc71_laptop uniwill_laptop; do
        echo "### $m"
        if command -v modinfo >/dev/null 2>&1 && modinfo "$m" >/dev/null 2>&1; then
            modinfo "$m" | grep -E '^(filename|version|description|author|license|srcversion|vermagic):' || true
        else
            echo "(not available)"
        fi
        echo
    done

    section "WMI devices"
    local d
    if compgen -G "/sys/bus/wmi/devices/*" >/dev/null; then
        for d in /sys/bus/wmi/devices/*; do
            local drv="-"
            [ -L "$d/driver" ] && drv=$(basename "$(readlink "$d/driver")")
            printf '%-44s guid=%s driver=%s\n' "$(basename "$d")" "$(val "$d/guid")" "$drv"
        done
    else
        echo "(no WMI devices)"
    fi
    echo
    echo "Uniwill WMI GUIDs (ABBC0F6F..ABBC0F72) present:"
    grep -ril 'ABBC0F7[0-2]\|ABBC0F6F' /sys/bus/wmi/devices/*/guid 2>/dev/null | sed 's|/guid$||;s|.*/|  |' || echo "  (none)"

    section "qc71_laptop driver"
    local pdev=/sys/devices/platform/qc71_laptop
    if [ -d "$pdev" ]; then
        echo "project_id: $(val "$pdev/project_id")"
        echo "attributes:"
        for f in "$pdev"/*; do
            [ -f "$f" ] || continue
            case "$(basename "$f")" in uevent|modalias|driver_override) continue ;; esac
            if [ -r "$f" ]; then
                printf '  %-32s %s\n' "$(basename "$f")" "$(head -c 300 "$f" 2>/dev/null | tr '\n' ' ' || echo '(unreadable)')"
            else
                printf '  %-32s %s\n' "$(basename "$f")" "(write-only)"
            fi
        done
    else
        echo "(driver not loaded: $pdev missing)"
    fi

    section "Keyboard backlight and other LEDs"
    local l found=0
    for l in /sys/class/leds/*; do
        [ -e "$l" ] || continue
        case "$(basename "$l")" in
            *kbd_backlight*|*micmute*|*platform*|*uniwill*|*qc71*) ;;
            *) continue ;;
        esac
        found=1
        printf '%-36s brightness=%s max=%s\n' "$(basename "$l")" "$(val "$l/brightness")" "$(val "$l/max_brightness")"
    done
    [ "$found" = 1 ] || echo "(none)"

    section "Hardware monitoring (fan / temperature)"
    local h
    for h in /sys/class/hwmon/hwmon*; do
        [ -e "$h" ] || continue
        local name
        name=$(val "$h/name")
        case "$name" in qc71*|uniwill*|tuxedo*|coretemp|k10temp|acpitz) ;; *) continue ;; esac
        echo "### $name"
        for f in "$h"/fan*_input "$h"/pwm*_enable "$h"/temp1_input; do
            [ -r "$f" ] && printf '  %-16s %s\n' "$(basename "$f")" "$(val "$f")"
        done
    done

    section "Battery (no serial number)"
    local b
    for b in /sys/class/power_supply/*; do
        [ -e "$b" ] || continue
        [ "$(val "$b/type")" = "Battery" ] || continue
        echo "### $(basename "$b")"
        for f in manufacturer model_name technology cycle_count status \
                 energy_full_design energy_full charge_full_design charge_full \
                 charge_types charge_behaviour charge_control_end_threshold; do
            [ -e "$b/$f" ] && printf '  %-30s %s\n' "$f" "$(val "$b/$f")"
        done
    done

    section "UEFI variables (names only)"
    if [ -d /sys/firmware/efi/efivars ]; then
        local v found=0
        for v in /sys/firmware/efi/efivars/*-9f33f85c-13ca-4fd1-9c4a-96217722c593; do
            [ -e "$v" ] || continue
            found=1
            printf '%s (%s bytes)\n' "$(basename "$v")" "$(stat -c %s "$v" 2>/dev/null || echo '?')"
        done
        [ "$found" = 1 ] || echo "(no UniWillVariable)"
    else
        echo "(no efivars)"
    fi

    section "Recent kernel messages (qc71 / uniwill)"
    if dmesg >/dev/null 2>&1; then
        dmesg | grep -iE 'qc71|uniwill' | tail -n 30 || echo "(none)"
    elif command -v journalctl >/dev/null 2>&1; then
        journalctl -k -b -o cat --no-pager 2>/dev/null | grep -iE 'qc71|uniwill' | tail -n 30 || echo "(none or not readable without root)"
    else
        echo "(not readable)"
    fi
}

umask 077
report > "$out"
echo "Report written to: $out"
echo "Please read it before attaching it to an issue. Run with --ec for EC dump instructions."
