#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
# Huma Control Center installer: the qc71_laptop driver (DKMS) for the Monster Huma H4
# (Uniwill PH4TUX1), root helpers, services, the GTK application and the GNOME
# Shell extension; optionally the power-saving settings (battery profile, SD card reader).
# Usage: ./install.sh                  install / update (the safety warning must be
#                                      accepted on the first install)
#        ./install.sh --yes            install without asking (only if you have read
#                                      docs/SAFETY.md)
#        ./install.sh --uninstall      remove the driver, helpers, services and app
#        --no-power-saving             do not install the power-saving component
#                                      (huma-control-power, tuned profile)
#        --force                       developers only: skip the model check (the
#                                      driver still refuses to write; docs/SAFETY.md)
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"  # <repo>
VER="$(sed -n 's/^PACKAGE_VERSION=//p' "$DIR/driver/dkms.conf")"
APP_ID=io.github.aygkhn.HumaControl
EXT=huma-control@aygkhn.github.io

# Flags are matched as whole words anywhere on the command line (except the
# uninstall flag, which must come first)
has_flag() {
    local a
    for a in "${@:2}"; do
        case " $1 " in *" $a "*) return 0 ;; esac
    done
    return 1
}

LIBEXEC=/usr/local/libexec
POWER=$LIBEXEC/huma-control-power
SLEEP_RULE=/etc/udev/rules.d/90-huma-control-cardreader-pm.rules
OFF_RULE=/etc/udev/rules.d/89-huma-control-cardreader-off.rules
KB=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/huma-control/

# Removes one entry from a GSettings string list
gsettings_list_remove() {  # schema key entry
    local list
    list=$(gsettings get "$1" "$2")
    list=$(python3 -c 'import ast,sys; l=ast.literal_eval(sys.argv[1].replace("@as ","")); print(str([x for x in l if x != sys.argv[2]]))' "$list" "$3")
    gsettings set "$1" "$2" "$list"
}

# Adds one entry to a GSettings string list (if missing)
gsettings_list_add() {  # schema key entry
    local list
    list=$(gsettings get "$1" "$2")
    [[ "$list" == *"'$3'"* ]] && return 0
    list=$(python3 -c 'import ast,sys; l=ast.literal_eval(sys.argv[1].replace("@as ","")); l.append(sys.argv[2]); print(str(l))' "$list" "$3")
    gsettings set "$1" "$2" "$list"
}

if [ "${1:-}" = --uninstall ]; then
    echo "==> Removing Huma Control Center (sudo password will be requested)"
    sudo systemctl disable --now huma-control-keyboard.service huma-control-service.service 2>/dev/null || true
    # power saving: back to the defaults (tuned-ppd mapping, card reader on and authorized)
    if [ -x "$POWER" ]; then
        for f in battery-profile card-reader-off card-reader-sleep deep-sleep; do
            sudo "$POWER" "$f" off || true
        done
    fi
    sudo rm -rf /etc/tuned/profiles/huma-control-battery
    sudo rm -f "$POWER" "$SLEEP_RULE" "$OFF_RULE"
    sudo udevadm control --reload || true
    sudo rm -f /etc/udev/rules.d/71-huma-control-keyboard.rules \
        /etc/systemd/system/huma-control-keyboard.service "$LIBEXEC/huma-control-keyboard" \
        /etc/systemd/system/huma-control-service.service "$LIBEXEC/huma-control-service" \
        "$LIBEXEC/huma-control-light" "$LIBEXEC/huma-control-presence" \
        "$LIBEXEC/huma-control-login-boost" /etc/systemd/system/huma-control-login-boost@.service \
        /etc/systemd/system/user@.service.d/huma-control-login-boost.conf \
        /etc/modprobe.d/qc71_laptop-fan-curve.conf /run/huma-control-keyboard.daytime
    # UEFI backups (UniWillVariable.*) are kept on purpose
    sudo rm -rf /var/lib/huma-control/history.csv /var/lib/huma-control/history.tmp \
        /var/lib/huma-control/battery-usage.json /var/lib/huma-control/battery-usage.tmp \
        /run/huma-control /usr/local/lib/huma-control /usr/local/share/huma-control
    rm -f "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"/huma-control-{pause,brightness.json}
    rm -rf "$HOME/.local/share/gnome-shell/extensions/$EXT" "$HOME/.config/huma-control"
    gsettings_list_remove org.gnome.shell enabled-extensions "$EXT"
    # keyboard.json, automation.json and the *.bak copies
    sudo rm -rf /etc/huma-control
    sudo systemctl daemon-reload
    sudo modprobe -r qc71_laptop 2>/dev/null || true
    for old in $(dkms status qc71_laptop 2>/dev/null | sed -n 's|^qc71_laptop/\([^,]*\),.*|\1|p' | sort -u); do
        sudo dkms remove -m qc71_laptop -v "$old" --all || true
        sudo rm -rf "/usr/src/qc71_laptop-$old"
    done
    # sources never added to DKMS (interrupted install)
    if [ -z "$(dkms status qc71_laptop 2>/dev/null)" ]; then
        sudo rm -rf /usr/src/qc71_laptop-*
    fi
    sudo rm -f /etc/modprobe.d/qc71_laptop.conf /etc/modprobe.d/qc71_laptop-huma-control.conf \
        "$LIBEXEC/huma-control-helper" "$LIBEXEC/huma-control-uefi" \
        /etc/polkit-1/rules.d/50-huma-control.rules
    rm -f "$HOME/.local/bin/huma-control" "$HOME/.local/share/applications/$APP_ID.desktop"
    rm -f "$HOME"/.local/share/locale/*/LC_MESSAGES/huma-control.mo
    find "$HOME/.local/share/icons/hicolor" \( -name "$APP_ID*" -o -name "hc-*" \) -delete 2>/dev/null || true
    update-desktop-database -q "$HOME/.local/share/applications" 2>/dev/null || true
    gsettings_list_remove org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$KB"
    gsettings reset-recursively "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB" 2>/dev/null || true
    echo "Removed. UEFI variable backups remain under /var/lib/huma-control."
    exit 0
fi

# Model check: install only on a tested model (the driver does not load on other models either)
MODEL="$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null) $(cat /sys/class/dmi/id/product_name 2>/dev/null)"
if [ "$MODEL" != "MONSTER HUMA H4 V4.1" ] && ! has_flag "$*" --force; then
    echo "This computer is not supported: $MODEL" >&2
    echo "Only the Monster Huma H4 V4.1 (Uniwill PH4TUX1) has been tested; see docs/tested-hardware.md." >&2
    echo "To get your model added, open a 'New model' issue with the output of tools/hw-report.sh." >&2
    echo "(--force is for developers only: the driver still does not load on this model, and with force=1 it only reads; docs/SAFETY.md)" >&2
    exit 1
fi

# Safety warning: must be read and accepted on the first install (docs/SAFETY.md)
SAFETY_MARKER=/var/lib/huma-control/safety-accepted
if [ ! -e "$SAFETY_MARKER" ] && ! has_flag "$*" --yes; then
    cat <<'END_OF_WARNING'

  WARNING - Huma Control Center is unofficial software that changes low-level
  settings of this laptop: it loads a kernel driver that writes to the embedded
  controller (fan, power limits, charging), installs root helpers and can optionally
  change a UEFI variable. There is NO WARRANTY; the authors are not liable for damage
  to hardware, software or data, or for loss of warranty. Details: docs/SAFETY.md

END_OF_WARNING
    read -r -p "I understand and accept the risk [yes]: " answer
    case "$answer" in
        yes|Yes|YES) ;;
        *) echo "Installation cancelled."; exit 1 ;;
    esac
fi

echo "==> Installing the qc71_laptop driver with DKMS (sudo password will be requested)"
# kernel-devel-matched: headers arrive together with new kernels, so DKMS can rebuild
if ! sudo dnf install -y gettext dkms kernel-devel-matched "kernel-devel-$(uname -r)"; then
    echo "ERROR: could not install the header packages for the running kernel ($(uname -r))." >&2
    echo "Update the system and reboot first: sudo dnf upgrade --refresh" >&2
    exit 1
fi
sudo rm -rf "/usr/src/qc71_laptop-$VER"
sudo cp -r "$DIR/driver" "/usr/src/qc71_laptop-$VER"
# Remove previous versions
for old in $(dkms status qc71_laptop 2>/dev/null | sed -n 's|^qc71_laptop/\([^,]*\),.*|\1|p' | sort -u); do
    sudo dkms remove -m qc71_laptop -v "$old" --all >/dev/null 2>&1 || true
    [ "$old" != "$VER" ] && sudo rm -rf "/usr/src/qc71_laptop-$old"
done
sudo dkms install -m qc71_laptop -v "$VER"
# model settings live in the driver's model table (driver/models.c): no module options file
sudo rm -f /etc/modprobe.d/qc71_laptop.conf

# With Secure Boot on, DKMS signs the module with its own key (/var/lib/dkms/mok.key), but
# the signed module will not load until that key has been enrolled in UEFI once
MOK_PENDING=0
if mokutil --sb-state 2>/dev/null | grep -q "SecureBoot enabled"; then
    if ! mokutil --test-key /var/lib/dkms/mok.pub 2>/dev/null | grep -q "is already enrolled"; then
        cat <<'MSG'

==> Secure Boot is enabled: the driver's signing key will be enrolled in UEFI.
    Choose a one-time password now (asked twice). After rebooting, on the blue
    "MOK Management" screen: Enroll MOK -> Continue -> Yes -> this password -> Reboot.
MSG
        sudo mokutil --import /var/lib/dkms/mok.pub
        MOK_PENDING=1
    fi
fi
sudo modprobe -r qc71_laptop 2>/dev/null || true
if [ "$MOK_PENDING" = 1 ]; then
    echo "   The driver will load after you reboot and enroll the key; continuing the installation."
else
    sudo modprobe qc71_laptop || echo "WARNING: the driver could not be loaded (check dmesg | tail); continuing the installation." >&2
    # the reloaded driver reads the EC's current mode; let tuned re-apply the power profile
    systemctl is-active --quiet tuned && sudo systemctl restart tuned
fi

echo "==> Installing the root helpers"
sudo install -d -m 755 /var/lib/huma-control && sudo touch "$SAFETY_MARKER"
sudo install -D -m 755 "$DIR/system/huma-control-helper" "$LIBEXEC/huma-control-helper"
sudo install -D -m 755 "$DIR/system/huma-control-uefi" "$LIBEXEC/huma-control-uefi"

echo "==> Keyboard backlight service (off when idle, off at boot in daytime; also on the login screen)"
sudo install -D -m 755 "$DIR/system/huma-control-keyboard" "$LIBEXEC/huma-control-keyboard"
# ambient light is measured with the camera (there is no light sensor): exposure via v4l2-ctl, frames via ffmpeg
sudo dnf install -y -q v4l-utils ffmpeg-free >/dev/null 2>&1 || sudo dnf install -y -q v4l-utils || true
sudo install -D -m 755 "$DIR/system/huma-control-light" "$LIBEXEC/huma-control-light"
# presence check (turn the screen off when nobody is there): OpenCV for face detection in a
# separate Python venv (the distro's python3-opencv pulls ~800 MB of desktop deps; this is ~230 MB)
FACE_VENV=/usr/local/lib/huma-control/presence
if [ ! -x "$FACE_VENV/bin/python" ] || ! "$FACE_VENV/bin/python" -c 'import cv2' 2>/dev/null; then
    sudo python3 -m venv "$FACE_VENV" && sudo "$FACE_VENV/bin/pip" install -q --disable-pip-version-check opencv-python-headless ||
        echo "   (face detection could not be installed: the presence check stays disabled)"
fi
sudo install -D -m 644 "$DIR/system/yunet.onnx" /usr/local/share/huma-control/yunet.onnx
sudo install -D -m 755 "$DIR/system/huma-control-presence" "$LIBEXEC/huma-control-presence"
sudo install -m 644 "$DIR/system/huma-control-keyboard.service" /etc/systemd/system/
# start the service once the keyboard backlight appears (when the driver loads)
sudo install -m 644 "$DIR/system/71-huma-control-keyboard.rules" /etc/udev/rules.d/
if [ ! -f /etc/huma-control/keyboard.json ]; then
    # first install: default settings with GNOME's location (Night Light) for the daytime rule
    location=$(gsettings get org.gnome.settings-daemon.plugins.color night-light-last-coordinates 2>/dev/null |
        python3 -c 'import sys; a=sys.stdin.read().strip("()\n ").split(","); lat,lon=float(a[0]),float(a[1]); print(f"{lat:.4f} {lon:.4f}" if (lat or lon) and -90<=lat<=90 and -180<=lon<=180 else "")' 2>/dev/null || true)
    # (on a fresh install GNOME stores the placeholder 91,181: an invalid location is not passed on)
    # shellcheck disable=SC2086
    sudo "$LIBEXEC/huma-control-helper" keyboard 15 1 $location ||
        echo "WARNING: could not write the keyboard settings" >&2
fi
echo "==> Huma Control Center service (automatic profiles, history, alerts)"
sudo install -D -m 755 "$DIR/system/huma-control-service" "$LIBEXEC/huma-control-service"
sudo install -m 644 "$DIR/system/huma-control-service.service" /etc/systemd/system/
# faster login on battery (turbo for 20 s while GNOME starts)
sudo install -D -m 755 "$DIR/system/huma-control-login-boost" "$LIBEXEC/huma-control-login-boost"
sudo install -m 644 "$DIR/system/huma-control-login-boost@.service" /etc/systemd/system/
sudo install -D -m 644 "$DIR/system/user@.service.d/huma-control-login-boost.conf" \
    /etc/systemd/system/user@.service.d/huma-control-login-boost.conf
sudo systemctl daemon-reload
sudo udevadm control --reload
sudo systemctl enable huma-control-keyboard.service huma-control-service.service
sudo systemctl restart huma-control-keyboard.service huma-control-service.service
sudo install -m 644 "$DIR/system/50-huma-control.rules" /etc/polkit-1/rules.d/

if has_flag "$*" --no-power-saving; then
    echo "==> Power-saving component skipped (--no-power-saving)"
else
    echo "==> Power saving: battery profile and SD card reader (Huma Control Center -> Power Saving)"
    first_install=0
    [ -d /etc/tuned/profiles/huma-control-battery ] || first_install=1
    sudo install -D -m 755 "$DIR/system/huma-control-power" "$POWER"
    sudo install -D -m 644 "$DIR/system/tuned/huma-control-battery/tuned.conf" \
        /etc/tuned/profiles/huma-control-battery/tuned.conf
    reader_off=0 reader_sleep=0
    [ -e "$OFF_RULE" ] && reader_off=1
    [ -e "$SLEEP_RULE" ] && reader_sleep=1
    # first install: the card reader sleeps when idle and "Balanced" uses the power-saving
    # profile on battery; both can be changed in Huma Control Center -> Power Saving
    if [ "$first_install" = 1 ]; then
        reader_sleep=1
        sudo "$POWER" battery-profile on || echo "   (tuned-ppd not found: the battery profile stays off)"
    fi
    # rules that are on are (re)written from the helper, so they match this version
    if [ "$reader_off" = 1 ]; then
        if sudo "$POWER" card-reader-off on; then echo "   SD card reader: off"
        else echo "WARNING: could not apply the SD card reader setting (off)" >&2; fi
    fi
    if [ "$reader_sleep" = 1 ]; then
        if sudo "$POWER" card-reader-sleep on; then echo "   SD card reader: sleeps when idle"
        else echo "WARNING: could not apply the SD card reader setting (sleep)" >&2; fi
    fi
fi

echo "==> Installing the application"
install -D -m 755 "$DIR/app/huma-control" "$HOME/.local/bin/huma-control"
# UI translations (source strings are English; shown in Turkish when the system language is Turkish)
for po in "$DIR"/app/po/*.po; do
    lang_code=$(basename "$po" .po)
    mkdir -p "$HOME/.local/share/locale/$lang_code/LC_MESSAGES"
    msgfmt -o "$HOME/.local/share/locale/$lang_code/LC_MESSAGES/huma-control.mo" "$po"
done
# the launcher's translated names come from the same .po files
mkdir -p "$HOME/.local/share/applications"
msgfmt --desktop --template="$DIR/app/$APP_ID.desktop" -d "$DIR/app/po" -o - |
    sed "s|^Exec=huma-control|Exec=$HOME/.local/bin/huma-control|" \
    > "$HOME/.local/share/applications/$APP_ID.desktop"
mkdir -p "$HOME/.local/share/icons"
cp -r "$DIR/app/icons/hicolor" "$HOME/.local/share/icons/"
gtk-update-icon-cache -q -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
update-desktop-database -q "$HOME/.local/share/applications" 2>/dev/null || true

echo "==> Quick Settings extension (performance mode, Fan Boost, mode indicator, alerts)"
EXT_DIR="$HOME/.local/share/gnome-shell/extensions"
mkdir -p "$EXT_DIR"
rm -rf "${EXT_DIR:?}/$EXT"
cp -r "$DIR/$EXT" "$EXT_DIR/"
# UI translations (source strings are English)
for po in "$DIR"/po/*.po; do
    lang_code=$(basename "$po" .po)
    mkdir -p "$EXT_DIR/$EXT/locale/$lang_code/LC_MESSAGES"
    msgfmt -o "$EXT_DIR/$EXT/locale/$lang_code/LC_MESSAGES/$EXT.mo" "$po"
done
# on Wayland a new extension is not loaded until the next login: add it to the list directly
gsettings_list_add org.gnome.shell enabled-extensions "$EXT"

echo "==> Make the keyboard's Control Center key (XF86Launch1) open Huma Control Center"
gsettings_list_add org.gnome.settings-daemon.plugins.media-keys custom-keybindings "$KB"
SCHEMA=org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:$KB
gsettings set "$SCHEMA" name 'Huma Control Center'
gsettings set "$SCHEMA" command "$HOME/.local/bin/huma-control"
gsettings set "$SCHEMA" binding 'Launch1'

echo
echo "Done. Look for \"Huma Control Center\" in the applications menu."
echo "Log out and back in so GNOME loads the updated extension (on Wayland it is not reloaded while logged in)."
