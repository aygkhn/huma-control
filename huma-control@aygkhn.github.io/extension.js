// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
// Huma Control Center: performance mode and Fan Boost in Quick Settings, an on-screen
// display (OSD) when the mode changes, notifications for the system service's alerts
// and screen brightness from ambient light (there is no light sensor; the keyboard
// service measures it with the camera).
//
// EC reads go through WMI and are slow, so the driver files are read asynchronously;
// the driver sends a udev "change" event on changes (HUMA_CONTROL=performance|fan_boost).
// Writes go through huma-control-helper (pkexec; the polkit rule asks no password).
import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import GUdev from 'gi://GUdev';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import * as QuickSettings from 'resource:///org/gnome/shell/ui/quickSettings.js';
import {Extension, gettext as _, pgettext} from 'resource:///org/gnome/shell/extensions/extension.js';

const QC71 = '/sys/devices/platform/qc71_laptop';
const HELPER = '/usr/local/libexec/huma-control-helper';
const ALERTS = '/run/huma-control/alerts.json';
const LIGHT = '/run/huma-control/light.json';
const KBD_CONFIG = '/etc/huma-control/keyboard.json';
const KBD_EVENT = '/run/huma-control/keyboard-event.json';

// Basenames that mean "this file changed" for a directory monitor
function watchedNames(path) {
    return [GLib.path_get_basename(path)];
}

// GNOME's centered on-screen display (as used by the brightness keys); level 0-1
function showOsd(iconName, label, level = null) {
    Main.osdWindowManager.showAll(Gio.ThemedIcon.new(iconName), label, level, level === null ? null : 1);
}

// The root services write English texts plus stable codes; they are shown in the user's
// language. Unknown codes are shown as written.
// (Functions, not tables: translations are looked up once the extension is loaded.)
function fill(template, data) {
    return template.replace(/\{(\w+)\}/g, (m, key) => key in data ? String(data[key]) : m);
}

function decimal(value) {
    return (typeof value === 'number' ? value.toFixed(1) : String(value))
        .replace('.', pgettext('decimal separator', '.'));
}

function alertText(a) {
    const texts = {
        'overheat': [_('CPU is overheating'),
            _('CPU at {temp} °C. Make sure the air vents are not blocked; Fan Boost can be turned on from the Performance page.')],
        'fan-fault': [_('Fan failure'), _('The controller reports a fan failure (the fan is not spinning or is stuck).')],
        'fan-stuck': [_('Fan is not spinning'), _('The fan is stopped although the CPU is at {temp} °C.')],
        'battery-health': [_('Battery health has dropped'),
            _('When full, the battery holds {full_wh} Wh (design {design_wh} Wh, {percent}). "Balanced" or "Stationary" in Battery Care protects the battery.')],
    };
    const known = texts[a.code];
    const data = a.data;
    if (known && data && typeof data === 'object') {
        const values = {...data};
        for (const key of ['full_wh', 'design_wh']) {
            if (typeof values[key] === 'number')
                values[key] = decimal(values[key]);
        }
        if ('percent' in values)
            values.percent = fill(_('{value}%'), {value: values.percent});
        const [title, text] = known.map(t => fill(t, values));
        if (!/\{\w+\}/.test(text))
            return [title, text];
    }
    return [a.title ?? '', a.text ?? ''];
}

// The keyboard service's automatic change: level and reason
function keyboardEventText(e) {
    const levels = [pgettext('keyboard backlight', 'Off'), pgettext('keyboard backlight', 'Dim'),
        pgettext('keyboard backlight', 'Bright')];
    const reasons = {
        'very-dark': _('very dark'), 'dark': _('dark'), 'bright': _('bright'), 'daytime': _('daytime'),
    };
    const level = levels[e.level] ?? e.name;
    const reason = reasons[e.reason_code] ?? e.reason;
    return reason ? `${_('Keyboard light')}: ${level} (${reason})` : `${_('Keyboard light')}: ${level}`;
}

// Error texts written by the helpers (stderr), translated by exact match, line by line
function helperError(text) {
    const known = {
        'Another setting is being applied, try again later': _('Another setting is being applied, try again later'),
        'The qc71_laptop driver is not loaded': _('The qc71_laptop driver is not loaded'),
        'This feature is not supported on this device': _('This feature is not supported on this device'),
        'The controller did not accept the setting': _('The controller did not accept the setting'),
        'Turn off "Power on when plugged in" first': _('Turn off "Power on when plugged in" first'),
        'UEFI variable is not in the expected format; left untouched':
            _('UEFI variable is not in the expected format; left untouched'),
        'This device does not support power on with AC': _('This device does not support power on with AC'),
        'Turn off "USB power while off" first': _('Turn off "USB power while off" first'),
        'Could not verify the UEFI variable; the previous value was restored':
            _('Could not verify the UEFI variable; the previous value was restored'),
        'Power on with AC setting not found on this device': _('Power on with AC setting not found on this device'),
        'camera not found': _('camera not found'),
        'camera is open in another application': _('camera is open in another application'),
        'could not get an image from the camera': _('could not get an image from the camera'),
        'could not get an image from the IR camera': _('could not get an image from the IR camera'),
    };
    const prefixes = [['Invalid settings: ', _('Invalid settings: {error}')],
        ['measurement failed: ', _('measurement failed: {error}')],
        ['check failed: ', _('check failed: {error}')]];
    const one = line => {
        line = line.trim();
        if (Object.hasOwn(known, line))
            return known[line];
        for (const [prefix, template] of prefixes) {
            if (line.startsWith(prefix))
                return fill(template, {error: one(line.slice(prefix.length))});
        }
        return line;
    };
    return (text ?? '').trim().split('\n').map(one).join('\n');
}
const APP_ID = 'io.github.aygkhn.HumaControl.desktop';
// product name: not translated
const APP_NAME = 'Huma Control Center';
const DEFAULT = 'balanced-medium';

// Driver name, display name, icon
const PROFILES = [
    ['balanced-low', () => _('Balanced · Low'), 'power-profile-balanced-symbolic'],
    ['balanced-medium', () => _('Balanced · Medium'), 'power-profile-balanced-symbolic'],
    ['balanced-high', () => _('Balanced · High'), 'power-profile-performance-symbolic'],
    ['quiet-20db', () => _('Quiet · 20 dB'), 'power-profile-power-saver-symbolic'],
    ['quiet-30db', () => _('Quiet · 30 dB'), 'power-profile-power-saver-symbolic'],
    ['quiet-40db', () => _('Quiet · 40 dB'), 'power-profile-power-saver-symbolic'],
];

const decoder = new TextDecoder();

function readAsync(path) {
    return new Promise(resolve => {
        Gio.File.new_for_path(path).load_contents_async(null, (file, res) => {
            try {
                resolve(decoder.decode(file.load_contents_finish(res)[1]).trim());
            } catch {
                resolve(null);
            }
        });
    });
}

// JSON of a service file (null if missing or unreadable). Throws on a partially written file.
async function readJson(path) {
    const text = await readAsync(path);
    return text === null ? null : JSON.parse(text || 'null');
}

function runHelper(...args) {
    return new Promise(resolve => {
        try {
            const proc = Gio.Subprocess.new(['pkexec', HELPER, ...args],
                Gio.SubprocessFlags.STDOUT_SILENCE | Gio.SubprocessFlags.STDERR_PIPE);
            proc.communicate_utf8_async(null, null, (p, res) => {
                try {
                    const [, , stderr] = p.communicate_utf8_finish(res);
                    resolve(p.get_successful() ? null : (helperError(stderr) || _('Failed')));
                } catch (e) {
                    resolve(e.message);
                }
            });
        } catch (e) {
            resolve(e.message);
        }
    });
}

// Light settings (keyboard service, /etc/huma-control/keyboard.json): key -> setting label
const LIGHT_SWITCHES = [
    ['dark_turn_on', () => _('Keyboard light by ambient light'), 'keyboard-brightness-symbolic', 'purple'],
    ['off_in_daytime', () => _('Keyboard off in daytime'), 'weather-clear-symbolic', 'yellow'],
    ['presence', () => _('Lock when nobody is there'), 'system-lock-screen-symbolic', 'blue'],
    ['osd', () => _('Show automatic changes'), 'dialog-information-symbolic', 'teal'],
];
const LIGHT_DEFAULTS = {off_in_daytime: true, dark_turn_on: false, presence: false, osd: true};

// Performance menu: two modes as in Monster Control Center, three levels each;
// on the right, the CPU's sustained power limit (PL1)
const PROFILE_LEVELS = {balanced: ['low', 'medium', 'high'], quiet: ['20db', '30db', '40db']};
let EXT_PATH = '';

// Colored round badge, like the row icons in the Huma Control Center app
function badge(iconName, color) {
    return new St.Icon({
        gicon: iconName.startsWith('hc-') ? Gio.icon_new_for_string(`${EXT_PATH}/icons/${iconName}.svg`)
            : Gio.ThemedIcon.new(iconName),
        style_class: `hc-badge hc-${color}`, y_align: Clutter.ActorAlign.CENTER,
    });
}

const PROFILE_MENU = [
    [() => _('Balanced'), [['balanced-low', () => _('Low'), '30 W'], ['balanced-medium', () => _('Medium'), '32 W'],
        ['balanced-high', () => _('High'), '38 W']]],
    [() => _('Quiet'), [['quiet-20db', () => '20 dB', '15 W'], ['quiet-30db', () => '30 dB', '25 W'],
        ['quiet-40db', () => '40 dB', '35 W']]],
];

// Power mode (OS level, power-profiles / tuned-ppd): the driver follows it with the EC
// mode (Power saver -> Quiet 20 dB, Balanced -> Balanced Medium, Quiet 20 dB on battery
// with the power saving profile; Performance -> Balanced High). GNOME's separate Power
// Mode toggle is hidden so that this is the only place to change it.
const PPD = ['org.freedesktop.UPower.PowerProfiles', '/org/freedesktop/UPower/PowerProfiles'];
const PRESETS = [
    ['power-saver', () => _('Power saver'), 'power-profile-power-saver-symbolic'],
    ['balanced', () => _('Balanced'), 'power-profile-balanced-symbolic'],
    ['performance', () => _('Performance'), 'power-profile-performance-symbolic'],
];

function ppdCall(method, params, type = null) {
    return new Promise(resolve => {
        Gio.DBus.system.call(PPD[0], PPD[1], 'org.freedesktop.DBus.Properties', method, params,
            type, Gio.DBusCallFlags.NONE, -1, null, (conn, res) => {
                try {
                    resolve(conn.call_finish(res));
                } catch {
                    resolve(null);
                }
            });
    });
}

async function ppdGet() {
    const r = await ppdCall('Get', new GLib.Variant('(ss)', [PPD[0], 'ActiveProfile']), new GLib.VariantType('(v)'));
    return r ? r.deepUnpack()[0].deepUnpack() : null;
}

function ppdSet(profile) {
    return ppdCall('Set', new GLib.Variant('(ssv)', [PPD[0], 'ActiveProfile', new GLib.Variant('s', profile)]));
}

// Open Huma Control Center on the given page (switches to that page if already open)
function openControlCenter(page) {
    Main.overview.hide();
    Main.panel.closeQuickSettings?.();
    const path = GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'huma-control']);
    try {
        if (GLib.file_test(path, GLib.FileTest.IS_EXECUTABLE))
            Gio.Subprocess.new(page ? [path, '--page', page] : [path], Gio.SubprocessFlags.NONE);
        else
            Gio.DesktopAppInfo.new(APP_ID)?.launch([], null);
    } catch (e) {
        Main.notify(APP_NAME, e.message);
    }
}

function runHelperInput(input, ...args) {
    return new Promise(resolve => {
        try {
            const proc = Gio.Subprocess.new(['pkexec', HELPER, ...args],
                Gio.SubprocessFlags.STDIN_PIPE | Gio.SubprocessFlags.STDOUT_SILENCE | Gio.SubprocessFlags.STDERR_PIPE);
            proc.communicate_utf8_async(input, null, (p, res) => {
                try {
                    const [, , stderr] = p.communicate_utf8_finish(res);
                    resolve(p.get_successful() ? null : (helperError(stderr) || _('Failed')));
                } catch (e) {
                    resolve(e.message);
                }
            });
        } catch (e) {
            resolve(e.message);
        }
    });
}

function profileInfo(name) {
    return PROFILES.find(p => p[0] === name) ?? null;
}

const PerformanceToggle = GObject.registerClass(
class PerformanceToggle extends QuickSettings.QuickMenuToggle {
    _init() {
        super._init({
            title: _('Performance'),
            iconName: 'power-profile-balanced-symbolic',
            toggleMode: false,
        });
        this._profile = null;
        this._boost = false;
        this._lastOther = 'balanced-high';
        this._syncing = false;

        // Same layout as the Performance page in the app: colored mode banner, power mode,
        // Mode and Level for fine tuning, Fan Boost (the user sees the same thing in both places)
        const bannerItem = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, style_class: 'hc-banner-item'});
        this._banner = new St.BoxLayout({style_class: 'hc-banner hc-banner-unknown', x_expand: true});
        this._bannerIcon = new St.Icon({icon_name: 'power-profile-balanced-symbolic', style_class: 'hc-banner-icon',
            y_align: Clutter.ActorAlign.CENTER});
        const bannerText = new St.BoxLayout({vertical: true, y_align: Clutter.ActorAlign.CENTER});
        this._bannerTitle = new St.Label({style_class: 'hc-banner-title'});
        this._bannerSub = new St.Label({style_class: 'hc-banner-sub'});
        bannerText.add_child(this._bannerTitle);
        bannerText.add_child(this._bannerSub);
        this._banner.add_child(this._bannerIcon);
        this._banner.add_child(bannerText);
        bannerItem.add_child(this._banner);
        this.menu.addMenuItem(bannerItem);

        const segmented = (options, onClick) => {
            const box = new St.BoxLayout({style_class: 'hc-seg', y_align: Clutter.ActorAlign.CENTER});
            const buttons = new Map();
            for (const [name, label, icon] of options) {
                const content = new St.BoxLayout({style_class: 'hc-seg-content', x_align: Clutter.ActorAlign.CENTER});
                if (icon)
                    content.add_child(new St.Icon({icon_name: icon, style_class: 'hc-seg-icon'}));
                const text = new St.Label({text: label, y_align: Clutter.ActorAlign.CENTER});
                content.add_child(text);
                const button = new St.Button({child: content, style_class: 'hc-seg-button', can_focus: true, x_expand: true});
                button.text = text;
                button.connect('clicked', () => onClick(name));
                buttons.set(name, button);
                box.add_child(button);
            }
            box.buttons = buttons;
            box.select = name => buttons.forEach((b, n) =>
                b[n === name ? 'add_style_class_name' : 'remove_style_class_name']('hc-seg-active'));
            return box;
        };
        const row = (badgeActor, title, control, sub = null) => {
            const item = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, style_class: 'hc-row'});
            item.add_child(badgeActor);
            const text = new St.BoxLayout({vertical: true, x_expand: true, y_align: Clutter.ActorAlign.CENTER});
            text.add_child(new St.Label({text: title, style_class: 'hc-row-title'}));
            if (sub)
                text.add_child(sub);
            item.add_child(text);
            if (control)
                item.add_child(control);
            this.menu.addMenuItem(item);
            return item;
        };

        // Compact so it fits on screen when Quick Settings is crowded: no subheadings, level on one row
        this._presetSeg = segmented(PRESETS.map(([n, l]) => [n, l()]), name => this._setPreset(name));
        this._presetSeg.x_expand = true;
        const presetItem = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false, style_class: 'hc-row'});
        presetItem.add_child(this._presetSeg);
        this.menu.addMenuItem(presetItem);

        this._modeSeg = segmented([['balanced', PROFILE_MENU[0][0]()], ['quiet', PROFILE_MENU[1][0]()]],
            mode => this._set(`${mode}-${PROFILE_LEVELS[mode][this._levelIndex ?? 1]}`));
        row(badge('power-profile-balanced-symbolic', 'purple'), _('Mode'), this._modeSeg);
        this._levelSeg = segmented([0, 1, 2].map(i => [String(i), '']),
            i => this._set(`${this._mode ?? 'balanced'}-${PROFILE_LEVELS[this._mode ?? 'balanced'][Number(i)]}`));
        row(badge('power-profile-performance-symbolic', 'orange'), _('Level'), this._levelSeg);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._boostItem = new PopupMenu.PopupSwitchMenuItem(_('Fan Boost'), false);
        this._boostItem.insert_child_at_index(badge('hc-fan-symbolic', 'red'), 1);
        // GNOME 50: setToggleState also emits 'toggled'; ignore it while updating programmatically
        this._boostItem.connect('toggled', (_item, state) => {
            if (!this._syncing)
                this._setBoost(state);
        });
        this.menu.addMenuItem(this._boostItem);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addAction(_('Performance settings'), () => openControlCenter('performance'));

        // Clicking cycles through the power modes: Power saver -> Balanced -> Performance
        this.connect('clicked', () => {
            const names = PRESETS.map(p => p[0]);
            this._setPreset(names[(names.indexOf(this._preset) + 1) % names.length]);
        });
        // Update when the power mode changes elsewhere (GNOME Settings, charger plugged/unplugged)
        this._ppdSignal = Gio.DBus.system.signal_subscribe(PPD[0], 'org.freedesktop.DBus.Properties',
            'PropertiesChanged', PPD[1], null, Gio.DBusSignalFlags.NONE, (_c, _s, _p, _i, _n, params) => {
                if ('ActiveProfile' in params.deepUnpack()[1])
                    this.refresh();
            });
        this._gen = 0;
        this._presetTimer = 0;
        this.connect('destroy', () => {
            this._destroyed = true;
            Gio.DBus.system.signal_unsubscribe(this._ppdSignal);
            if (this._presetTimer)
                GLib.source_remove(this._presetTimer);
        });
        this.menu.connect('open-state-changed', (_m, open) => {
            if (open)
                this.refresh();
        });
    }

    async refresh(showOsd = false) {
        // with overlapping reads only the latest one is applied (an older one must not overwrite a newer one)
        const gen = ++this._gen;
        const [profile, boost, preset] = await Promise.all([
            readAsync(`${QC71}/performance_profile`),
            readAsync(`${QC71}/fan_boost`),
            ppdGet(),
        ]);
        if (gen !== this._gen || this._destroyed)
            return;
        this._preset = preset;
        this.visible = profile !== null;
        if (profile === null)
            return;
        this._profile = profile;
        this._boost = boost === '1';
        if (profile !== DEFAULT && profileInfo(profile))
            this._lastOther = profile;
        const info = profileInfo(profile);
        const label = info ? info[1]() : _('Custom');
        const presetInfo = PRESETS.find(p => p[0] === preset);
        this.iconName = presetInfo?.[2] ?? (info ? info[2] : 'power-profile-balanced-symbolic');
        this.checked = (preset !== null && preset !== 'balanced') || this._boost;
        this._presetSeg.select(preset);
        // fine tuning and banner: same as the Performance page in the app
        const [mode, level] = profile.split(/-(.*)/s);
        const levels = PROFILE_LEVELS[mode];
        const known = !!levels && levels.includes(level);
        this._mode = known ? mode : null;
        this._levelIndex = known ? levels.indexOf(level) : null;
        const menuLevels = PROFILE_MENU[mode === 'quiet' ? 1 : 0][1];
        menuLevels.forEach(([, levelLabel], i) => (this._levelSeg.buttons.get(String(i)).text.text = levelLabel()));
        this._modeSeg.select(this._mode);
        this._levelSeg.select(known ? String(this._levelIndex) : null);
        const watts = known ? menuLevels[this._levelIndex][2] : null;
        const kind = !known ? 'unknown' : mode === 'quiet' ? 'quiet' : this._levelIndex === 2 ? 'high' : 'balanced';
        for (const k of ['balanced', 'quiet', 'high', 'unknown'])
            this._banner[k === kind ? 'add_style_class_name' : 'remove_style_class_name'](`hc-banner-${k}`);
        this._bannerIcon.icon_name = {quiet: 'power-profile-power-saver-symbolic', high: 'power-profile-performance-symbolic'}[kind] ??
            'power-profile-balanced-symbolic';
        this._bannerTitle.text = known ? `${PROFILE_MENU[mode === 'quiet' ? 1 : 0][0]()} · ${menuLevels[this._levelIndex][1]()}` : _('Custom');

        this._syncing = true;
        this._boostItem.setToggleState(this._boost);
        this._syncing = false;
        this._bannerSub.text = [watts ? _('CPU power limit %s').format(watts) : null, this._boost ? _('Fan Boost on') : null]
            .filter(Boolean).join(' · ');
        // subtitle: power mode · level (e.g. "Power saver · 20 dB"; avoid a repeated "Balanced · Balanced")
        this.subtitle = [presetInfo?.[1](), known ? menuLevels[this._levelIndex][1]() : label,
            this._boost ? _('Fan Boost') : null].filter(Boolean).join(' · ');
        // OSD: if different from the last one shown (not missed whichever read arrives first)
        if (showOsd && profile !== this._announced)
            this._osd(this.iconName, label);
        this._announced = profile;
    }

    _osd(icon, label) {
        showOsd(icon, label);
    }

    async _set(name) {
        const error = await runHelper('performance', name);
        if (this._destroyed)
            return;
        if (error)
            Main.notify(APP_NAME, error);
        this.refresh(true);
    }

    async _setPreset(name) {
        if (!await ppdSet(name))
            Main.notify(APP_NAME, _('Power mode could not be changed'));
        if (this._destroyed)
            return;
        // the driver follows the power mode via platform_profile; read back a little later
        if (this._presetTimer)
            GLib.source_remove(this._presetTimer);
        this._presetTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 600, () => {
            this._presetTimer = 0;
            this.refresh(true);
            return GLib.SOURCE_REMOVE;
        });
    }

    async _setBoost(on) {
        const error = await runHelper('fan', on ? 'max' : 'auto');
        if (this._destroyed)
            return;
        if (error)
            Main.notify(APP_NAME, error);
        else
            this._osd('power-profile-performance-symbolic', on ? _('Fan Boost on') : _('Fan Boost off'));
        this.refresh(false);
    }
});

// Measured ambient light (on a 100 ms exposure scale; indoors 0-255, up to ~25500 by a
// window or in sunlight) -> perceptual level (indoors 0-1, outdoors above 1); the eye
// perceives light logarithmically
function lightLevel(value) {
    return Math.log1p(Math.max(value, 0)) / Math.log1p(255);
}

// In GNOME, screen = target + slider - 0.5 (clamped to 0-1): 1.5 gives 100% at any slider position
const FULL_TARGET = 1.5;
// a manual slider adjustment is kept at most this long within the same ambient stage (µs)
const HOLD_MAX = 20 * 60 * 1000000;

// Stage method: screen percentage for each ambient stage (set in the app);
// order: very dark, dark, bright, very bright, sunlight
const STAGE_PERCENTS = [15, 30, 60, 80, 100];
const STAGE_NAMES = [() => _('Very dark'), () => _('Dark'), () => _('Bright'), () => _('Very bright'), () => _('Sunlight')];

// Measurement values of the stages (keyboard thresholds, 255 where the camera saturates, and the sun threshold)
function stageAnchors(config) {
    const dark = Math.min(config.dark_threshold ?? 20, 150);
    const veryDark = Math.min(config.very_dark_threshold ?? 8, dark);
    return [veryDark, dark, Math.min(dark * 1.5, 254), 255, Math.max(config.sun_threshold ?? 1000, 256)];
}

// Stage nearest to the measurement (on the perceptual scale)
function nearestStage(value, anchors) {
    let best = 0;
    for (let i = 1; i < anchors.length; i++) {
        if (Math.abs(lightLevel(value) - lightLevel(anchors[i])) < Math.abs(lightLevel(value) - lightLevel(anchors[best])))
            best = i;
    }
    return best;
}

// Linear transition between stages on the perceptual scale: the screen does not jump, it changes stage by stage
function stagePercent(value, anchors, percents) {
    if (value <= anchors[0])
        return percents[0];
    for (let i = 1; i < anchors.length; i++) {
        if (value <= anchors[i]) {
            const span = lightLevel(anchors[i]) - lightLevel(anchors[i - 1]);
            const t = span > 0 ? (lightLevel(value) - lightLevel(anchors[i - 1])) / span : 1;
            return percents[i - 1] + t * (percents[i] - percents[i - 1]);
        }
    }
    return percents[percents.length - 1];
}

// Sensitivity (setting 1-3): how strongly ambient changes affect the screen
const SENSITIVITY = [0.8, 1.4, 2.0];

// Feeds GNOME 50's own automatic brightness input (normally driven by a light sensor):
// screen = target + (slider - 0.5). The slider position counts as the user's preference
// for the ambient light at the moment it was set: if it gets darker than that the screen
// dims, if brighter it brightens, gradually (per the sensitivity setting); every slider
// change makes the current ambient light the new reference. In the dark the screen does
// not go below 30% of the preference.
// Last decision of the automatic brightness (shown by the app, useful when debugging)
const BRIGHTNESS_STATE = GLib.build_filenamev([GLib.get_user_runtime_dir(), 'huma-control-brightness.json']);
// "Measure Now" in the app: {value, time}; leaves the manual adjustment like "Measure now" in the menu
const BRIGHTNESS_RESUME = GLib.build_filenamev([GLib.get_user_runtime_dir(), 'huma-control-brightness-resume.json']);

// GNOME disables and re-enables extensions when the screen locks: learned state (manual
// adjustment, recent measurements) is kept at module level so it survives the lock
const KEPT = ['_light', '_reference', '_hold', '_holdSince', '_history', '_stamp', '_shown', '_pref', '_userUpdate'];
let brightnessState = null;

class AutoBrightness {
    constructor() {
        this._timer = 0;
        this._light = null;        // last measurement
        this._reference = null;    // ambient light when the slider was set
        this._hold = null;         // stage method: stage the user adjusted with the slider
        this._pref = null;         // continuous method: user's slider preference (even if we lowered it)
        this._ownSlider = 0;       // if we moved the slider (below), it does not count as a manual change
        Object.assign(this, brightnessState ?? {});
        // after unlocking the target may still be set: treat it as ours
        this._active = (Main.brightnessManager?.autoBrightnessTarget ?? -1) >= 0;
        Main.brightnessManager?.connectObject('user-update', () => {
            if (GLib.get_monotonic_time() - this._ownSlider < 500000)
                return;
            this._userUpdate = GLib.DateTime.new_now_local().format('%H:%M:%S');
            this._pref = Main.brightnessManager.globalScale?.value ?? null;
            if (this._light === null)
                return;
            this._reference = this._light;
            // a slider adjustment is kept until the ambient light moves to another stage (or HOLD_MAX passes)
            if (this._stages) {
                this._hold = nearestStage(this._light, this._anchors);
                this._holdSince = GLib.get_monotonic_time();
            }
            // GNOME shows target + slider - 0.5: with a low automatic target the slider at its
            // end gave e.g. 89%. While adjusting by hand the screen should be what the slider shows
            if (this._active) {
                this._stopAnimation();
                Main.brightnessManager.autoBrightnessTarget = 0.5;
                this._shown = Main.brightnessManager.globalScale?.value;
            }
        }, this);
        this._kbdSeen = Date.now() / 1000;
        this._kbdMonitor = Gio.File.new_for_path(KBD_EVENT).get_parent().monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, null);
        const eventNames = watchedNames(KBD_EVENT);
        this._kbdMonitor.connect('changed', (_m, f, other) => {
            if (eventNames.includes((other ?? f)?.get_basename()))
                this._keyboardEvent();
        });
        this._resumeMonitor = Gio.File.new_for_path(BRIGHTNESS_RESUME).get_parent().monitor_directory(
            Gio.FileMonitorFlags.WATCH_MOVES, null);
        const resumeNames = watchedNames(BRIGHTNESS_RESUME);
        this._resumeMonitor.connect('changed', async (_m, f, other) => {
            if (!resumeNames.includes((other ?? f)?.get_basename()))
                return;
            let r = null;
            try {
                r = await readJson(BRIGHTNESS_RESUME);
            } catch {
                return;
            }
            if (typeof r?.value === 'number' && Date.now() / 1000 - (r.time ?? 0) < 10 && !this._destroyed) {
                this.resume();
                this.apply(r.value);
            }
        });
        this._monitors = [LIGHT, KBD_CONFIG].map(path => {
            const m = Gio.File.new_for_path(path).get_parent().monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, null);
            const names = watchedNames(path);
            m.connect('changed', (_m, f, other) => {
                if (names.includes((other ?? f)?.get_basename()))
                    this.update();
            });
            return m;
        });
        this.update();
    }

    async update() {
        const manager = Main.brightnessManager;
        if (!manager)
            return;
        let config = {};
        let light = null;
        try {
            config = await readJson(KBD_CONFIG) ?? {};
            light = await readJson(LIGHT);
        } catch {
            // missing or partially written file: retried on the next change
        }
        if (this._destroyed)
            return;
        this._osdOn = config.osd !== false;
        if (!config.screen_auto || typeof light?.value !== 'number') {
            this._enabled = false;
            if (this._active) {
                this._stopAnimation();
                manager.autoBrightnessTarget = -1;
                this._active = false;
            }
            return;
        }
        this._enabled = true;
        this._gain = SENSITIVITY[(config.screen_sensitivity ?? 2) - 1] ?? 1.4;
        this._sun = config.sun_full !== false ? config.sun_threshold ?? 1000 : Infinity;
        this._stages = (config.screen_method ?? 'stages') === 'stages';
        this._anchors = stageAnchors(config);
        const percents = config.screen_stages;
        const changed = JSON.stringify(percents) !== JSON.stringify(this._percents);
        this._percents = Array.isArray(percents) && percents.length === 5 ? percents : STAGE_PERCENTS;
        if (changed)
            this._hold = null;     // apply the new percentages right away
        this.apply(light.value, light.time ?? '');
    }

    // "Measure now": leave the manual adjustment and follow the ambient light again
    resume() {
        this._hold = null;
    }

    // Apply a measured ambient light value (the service's measurement or "Measure now" from the menu)
    apply(value, stamp = null) {
        const manager = Main.brightnessManager;
        if (!manager || !this._enabled || this._destroyed)
            return;
        // the camera also sees movement and screen reflections, so readings fluctuate: median of the
        // last 3 (a manual "Measure now" applies immediately; the same reading is not counted
        // twice when a setting changes)
        if (stamp === null) {
            this._history = [value];
        } else if (stamp !== this._stamp) {
            this._stamp = stamp;
            this._history = [...this._history ?? [], value].slice(-3);
        }
        const sorted = [...this._history ?? [value]].sort((a, b) => a - b);
        value = sorted[Math.floor(sorted.length / 2)];
        this._force = stamp === null;
        this._light = value;
        if (this._reference === null)
            this._reference = value;
        const slider = manager.globalScale?.value ?? 0.5;
        // the result codes passed to _report() are read (and translated) by the app
        if (this._stages) {
            const stage = nearestStage(value, this._anchors);
            const screen = stagePercent(value, this._anchors, this._percents) / 100;
            if (this._hold === stage && GLib.get_monotonic_time() - this._holdSince < HOLD_MAX) {
                this._report(value, stage, screen, slider, 'manual-kept');
                return;
            }
            this._hold = null;
            this._report(value, stage, screen, slider,
                this._applyTarget(manager, screen + 0.5 - slider) ? 'applied' : 'small-difference');
            return;
        }
        // preference: the slider as the user last set it by hand (if we lowered the slider, its live
        // value is not the preference, otherwise the screen would dim a bit more on every measurement)
        const pref = this._pref ?? slider;
        let target = 0.5 + (this._gain ?? 1.4) * (lightLevel(value) - lightLevel(this._reference));
        // lower bound: screen = target + preference - 0.5 >= 30% of the preference (at least 5%)
        target = Math.max(target, Math.max(pref * 0.3, 0.05) - pref + 0.5);
        // sunlight stage: full brightness so the screen stays readable
        if (value >= this._sun)
            target = FULL_TARGET;
        // the desired screen follows the preference; if the slider is elsewhere now, convert the target
        this._applyTarget(manager, target + pref - slider);
    }

    // In GNOME, screen = target + slider - 0.5, and a negative target means "automatic off": with the
    // slider high, a low brightness that needed a negative target left the screen at the slider
    // (e.g. 100%). In that case lower the slider as far as needed and use a target of 0.
    _applyTarget(manager, target) {
        if (target < 0 && manager.globalScale) {
            this._pref ??= manager.globalScale.value;
            const screen = target + manager.globalScale.value - 0.5;
            this._ownSlider = GLib.get_monotonic_time();
            manager.globalScale.value = Math.min(1, Math.max(0, screen + 0.5));
            target = 0;
        }
        return this._animateTo(manager, Math.min(FULL_TARGET, target));
    }

    _animateTo(manager, target) {
        // don't move the screen for small differences (no flicker or needless OSD): a difference
        // below 10% is hardly visible
        if (this._active && !this._force && !this._timer && Math.abs(target - manager.autoBrightnessTarget) < 0.1)
            return false;
        this._stopAnimation();
        let current = this._active && manager.autoBrightnessTarget >= 0 ? manager.autoBrightnessTarget : target;
        this._active = true;
        const steps = 12;
        const delta = (target - current) / steps;
        let n = 0;
        manager.autoBrightnessTarget = current;
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 120, () => {
            current += delta;
            manager.autoBrightnessTarget = ++n >= steps ? target : current;
            if (n >= steps) {
                this._timer = 0;
                // report a noticeable change with the OSD (as the brightness keys do)
                const slider = manager.globalScale?.value ?? 0.5;
                const screen = Math.min(1, Math.max(0, target + slider - 0.5));
                if (this._osdOn && this._shown !== undefined && Math.abs(screen - this._shown) > 0.02)
                    showOsd('display-brightness-symbolic', _('Automatic brightness'), screen);
                this._shown = screen;
                return GLib.SOURCE_REMOVE;
            }
            return GLib.SOURCE_CONTINUE;
        });
        return true;
    }

    _report(value, stage, screen, slider, result) {
        const state = {
            time: GLib.DateTime.new_now_local().format('%H:%M:%S'),
            readings: this._history, median: Math.round(value), stage, target: Math.round(screen * 100),
            slider: Math.round(slider * 100), gnome_target: Math.round(Main.brightnessManager.autoBrightnessTarget * 100),
            held_stage: this._hold, last_manual: this._userUpdate ?? null, result,
        };
        try {
            GLib.file_set_contents(BRIGHTNESS_STATE, JSON.stringify(state));
        } catch {
            // informational only
        }
    }

    // The keyboard service changed the backlight for ambient light/daytime: show the OSD
    async _keyboardEvent() {
        let e = null;
        try {
            e = await readJson(KBD_EVENT);
        } catch {
            return;
        }
        if (!e || !(e.time > this._kbdSeen) || Date.now() / 1000 - e.time > 10)
            return;
        this._kbdSeen = e.time;
        if (!this._osdOn)
            return;
        showOsd('keyboard-brightness-symbolic', keyboardEventText(e), e.level / 2);
    }

    _stopAnimation() {
        if (this._timer)
            GLib.source_remove(this._timer);
        this._timer = 0;
    }

    destroy() {
        this._destroyed = true;
        this._stopAnimation();
        Main.brightnessManager?.disconnectObject(this);
        this._monitors.forEach(m => m.cancel());
        this._kbdMonitor?.cancel();
        this._resumeMonitor?.cancel();
        brightnessState = Object.fromEntries(KEPT.map(k => [k, this[k]]));
        // the extension is disabled while locking: the lock screen must not jump to the raw slider value
        if (this._active && Main.brightnessManager && !Main.sessionMode.isLocked)
            Main.brightnessManager.autoBrightnessTarget = -1;
    }
}

// Adds to the menu of GNOME's brightness slider (the arrow next to the slider) an automatic
// brightness switch, the last measurement and "Measure now". GNOME keeps this menu enabled
// only with more than one monitor; here it is always enabled, and with a single monitor the
// per-monitor slider section is hidden.
class BrightnessMenu {
    constructor(auto) {
        this._auto = auto;
        this._item = Main.panel.statusArea.quickSettings._brightness?.quickSettingsItems?.[0];
        if (!this._item?.menu)
            return;
        this._syncing = false;
        this._separator = new PopupMenu.PopupSeparatorMenuItem();
        this._section = new PopupMenu.PopupMenuSection();
        this._switch = new PopupMenu.PopupSwitchMenuItem(_('Automatic brightness'), false);
        this._switch.insert_child_at_index(badge('display-brightness-symbolic', 'orange'), 1);
        this._switch.connect('toggled', (_i, state) => {
            if (!this._syncing)
                this._set(state);
        });
        this._info = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._info.label.add_style_class_name('dim-label');
        this._section.addMenuItem(this._switch);
        this._section.addMenuItem(this._info);
        this._section.addAction(_('Measure now (back to automatic)'), () => this._measure());
        this._rememberItem = this._section.addAction(_('Remember this brightness for this light'), () => this._remember());
        // the other light-related switches live here too (not in the performance menu)
        this._section.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._lightItems = new Map();
        for (const [key, label, icon, color] of LIGHT_SWITCHES) {
            const item = new PopupMenu.PopupSwitchMenuItem(label(), false);
            item.insert_child_at_index(badge(icon, color), 1);
            item.connect('toggled', (_i, state) => {
                if (!this._syncing)
                    this._setLight(key, state);
            });
            this._lightItems.set(key, item);
            this._section.addMenuItem(item);
        }
        this._section.addAction(_('Ambient light settings'), () => openControlCenter('ambient'));
        this._item.menu.addMenuItem(this._separator);
        this._item.menu.addMenuItem(this._section);
        // current screen percentage next to the slider (with automatic brightness the slider
        // alone does not show it); updated only while Quick Settings is open
        this._percent = new St.Label({y_align: Clutter.ActorAlign.CENTER, style_class: 'huma-brightness-percent'});
        this._item.get_first_child()?.insert_child_at_index(this._percent, 2);
        Main.panel.statusArea.quickSettings.menu.connectObject('open-state-changed', (_m, open) => {
            this._stopPercent();
            if (open) {
                this._showPercent();
                this._percentTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 500, () => {
                    this._showPercent();
                    return GLib.SOURCE_CONTINUE;
                });
            }
        }, this);
        this._showPercent();
        this._item.menu.connectObject('open-state-changed', (_m, open) => open && this.refresh(), this);
        // GNOME disables the menu in its own _sync on a single monitor; re-enable it afterwards
        Main.brightnessManager?.connectObject('changed', () => this._keepMenu(), this);
        this._keepMenu();
        this.refresh();
    }

    _keepMenu() {
        if (!this._item)
            return;
        this._item.menuEnabled = true;
        const monitors = this._item._monitorBrightnessSection;
        const several = (Main.brightnessManager?.scales?.length ?? 0) > 1;
        if (monitors?.actor)
            monitors.actor.visible = several;
    }

    async refresh() {
        let config = {}, light = null;
        try {
            config = await readJson(KBD_CONFIG) ?? {};
            light = await readJson(LIGHT);
        } catch {
            // partially written file
        }
        if (!this._item)   // destroyed in the meantime
            return;
        this._syncing = true;
        this._switch.setToggleState(!!config.screen_auto);
        for (const [key, item] of this._lightItems)
            item.setToggleState(config[key] ?? LIGHT_DEFAULTS[key]);
        this._syncing = false;
        this._config = config;
        this._rememberItem.visible = !!config.screen_auto && (config.screen_method ?? 'stages') === 'stages' &&
            typeof light?.value === 'number';
        this._info.label.text = typeof light?.value === 'number'
            ? `${_('Ambient light')}: ${Math.round(light.value)} (${(light.time ?? '').slice(11, 16)}) · ${_('measured by camera')}`
            : _('Not measured yet (measured by camera)');
    }

    async _setLight(key, on) {
        const error = await runHelperInput(JSON.stringify({[key]: on}), 'keyboard-set');
        if (error)
            Main.notify(APP_NAME, error);
        if (this._item)
            this.refresh();
    }

    async _set(on) {
        const error = await runHelperInput(JSON.stringify({screen_auto: on}), 'keyboard-set');
        if (error)
            Main.notify(APP_NAME, error);
        if (this._item)
            this.refresh();
    }

    // Store the current screen brightness for the stage of the last measurement
    async _remember() {
        const manager = Main.brightnessManager;
        const light = this._auto._light;
        if (!manager || light === null)
            return;
        const slider = manager.globalScale?.value ?? 0.5;
        const target = manager.autoBrightnessTarget >= 0 ? manager.autoBrightnessTarget : 0.5;
        const screen = Math.round(Math.min(1, Math.max(0.05, target + slider - 0.5)) * 100);
        const stage = nearestStage(light, stageAnchors(this._config ?? {}));
        const percents = [...(this._config?.screen_stages ?? STAGE_PERCENTS)];
        // keep it increasing from dark to bright: darker stages cannot be brighter than this,
        // brighter stages cannot be dimmer
        percents.forEach((v, i) => {
            percents[i] = i < stage ? Math.min(v, screen) : i > stage ? Math.max(v, screen) : screen;
        });
        const error = await runHelperInput(JSON.stringify({screen_stages: percents}), 'keyboard-set');
        if (error) {
            Main.notify(APP_NAME, error);
            return;
        }
        showOsd('display-brightness-symbolic', _('%s: %d%%').format(STAGE_NAMES[stage](), screen), screen / 100);
        if (this._item)
            this.refresh();
    }

    // Measure as the user (who has camera access); the result is applied immediately
    _measure() {
        this._info.label.text = _('Measuring…');
        try {
            const proc = Gio.Subprocess.new(['/usr/local/libexec/huma-control-light'],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE);
            proc.communicate_utf8_async(null, null, (p, res) => {
                try {
                    const [, out, err] = p.communicate_utf8_finish(res);
                    if (!this._item)   // destroyed in the meantime
                        return;
                    const value = parseFloat(out);
                    if (!p.get_successful() || Number.isNaN(value)) {
                        this._info.label.text = helperError(err) || _('Failed');
                        return;
                    }
                    this._info.label.text = `${_('Ambient light')}: ${Math.round(value)} · ${_('measured by camera')}`;
                    this._auto.resume();
                    this._auto.apply(value);
                } catch (e) {
                    this._info.label.text = e.message;
                }
            });
        } catch (e) {
            this._info.label.text = e.message;
        }
    }

    // actual panel brightness (same reading as the app); without a backlight GNOME's value:
    // automatic target + slider - 0.5, just the slider when automatic is off
    _showPercent() {
        if (!this._percent)
            return;
        let screen = null;
        try {
            this._backlight ??= Gio.File.new_for_path('/sys/class/backlight').enumerate_children(
                'standard::name', Gio.FileQueryInfoFlags.NONE, null).next_file(null)?.get_name() ?? '';
            if (this._backlight) {
                const read = f => Number(new TextDecoder().decode(
                    GLib.file_get_contents(`/sys/class/backlight/${this._backlight}/${f}`)[1]));
                screen = read('brightness') / read('max_brightness');
            }
        } catch {
            screen = null;
        }
        if (!Number.isFinite(screen)) {
            const manager = Main.brightnessManager;
            const slider = manager?.globalScale?.value;
            if (typeof slider !== 'number')
                return;
            const target = manager.autoBrightnessTarget ?? -1;
            screen = Math.min(1, Math.max(0, target >= 0 ? target + slider - 0.5 : slider));
        }
        this._percent.text = fill(_('{value}%'), {value: Math.round(screen * 100)});
    }

    _stopPercent() {
        if (this._percentTimer)
            GLib.source_remove(this._percentTimer);
        this._percentTimer = 0;
    }

    destroy() {
        if (!this._item)
            return;
        this._stopPercent();
        Main.panel.statusArea.quickSettings.menu.disconnectObject(this);
        this._percent?.destroy();
        this._percent = null;
        Main.brightnessManager?.disconnectObject(this);
        this._item.menu.disconnectObject(this);
        this._section.destroy();
        this._separator.destroy();
        // we hid the per-monitor sliders; GNOME does not show them again in _sync
        if (this._item._monitorBrightnessSection?.actor)
            this._item._monitorBrightnessSection.actor.visible = true;
        this._item._sync?.();   // back to GNOME's own menu state
        this._item = null;
    }
}

// last service alert seen: an alert that arrived while locked (e.g. overheating) is not lost
// when the extension is disabled and re-enabled around the lock
let alertsSeen = 0;

const Indicator = GObject.registerClass(
class Indicator extends QuickSettings.SystemIndicator {
    _init() {
        super._init();
        this._toggle = new PerformanceToggle();
        this.quickSettingsItems.push(this._toggle);
        // GNOME's Power Mode toggle: the same setting is at the top of our toggle, so don't show it
        // twice. Hidden only while ours is visible: if the driver fails to load (kernel update)
        // GNOME's toggle stays
        this._ppdToggle = Main.panel.statusArea.quickSettings._powerProfiles?.quickSettingsItems?.[0] ?? null;
        if (this._ppdToggle) {
            this._ppdToggle.connectObject('notify::visible', t => this._toggle.visible && t.visible && t.hide(), this);
            this._toggle.connectObject('notify::visible', () => this._syncPpdToggle(), this);
            this._syncPpdToggle();
        }
        this._toggle.refresh();

        // Changes coming from the driver (mode key, Fan Boost key, app, automatic profile)
        this._udev = new GUdev.Client({subsystems: ['platform']});
        this._udevId = this._udev.connect('uevent', (_client, action, device) => {
            if (action !== 'change' || device.get_name() !== 'qc71_laptop')
                return;
            const what = device.get_property('HUMA_CONTROL');
            if (what === 'performance' || what === 'fan_boost')
                this._toggle.refresh(what === 'performance');
        });

        // Service alerts -> notifications (old alerts from session start are not shown)
        this._readAlerts(alertsSeen === 0);
        const file = Gio.File.new_for_path(ALERTS);
        this._monitor = file.get_parent().monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, null);
        const alertNames = watchedNames(ALERTS);
        this._monitor.connect('changed', (_m, f, other) => {
            if (alertNames.includes((other ?? f)?.get_basename()))
                this._readAlerts(false);
        });
        // Show the resulting mode when the charger is plugged in/out (after the driver and power mode settle)
        this._onBattery = null;
        this._powerTimer = 0;
        this._upowerCall('Get', new GLib.Variant('(ss)', ['org.freedesktop.UPower', 'OnBattery']),
            r => (this._onBattery = r?.deepUnpack()[0].deepUnpack() ?? null));
        this._upowerSignal = Gio.DBus.system.signal_subscribe('org.freedesktop.UPower',
            'org.freedesktop.DBus.Properties', 'PropertiesChanged', '/org/freedesktop/UPower', null,
            Gio.DBusSignalFlags.NONE, (_c, _s, _p, _i, _n, params) => {
                const changed = params.deepUnpack()[1];
                if (changed.OnBattery)
                    this._powerSourceChanged(changed.OnBattery.deepUnpack());
            });
        // (no polling: the driver sends udev events, the power mode a D-Bus signal; also read when the menu opens)
    }

    _syncPpdToggle() {
        if (this._toggle.visible)
            this._ppdToggle.hide();
        else
            this._restorePpdToggle();
    }

    // GNOME shows its own toggle if the power profiles service is available
    _restorePpdToggle() {
        if (this._ppdToggle._sync)
            this._ppdToggle._sync();
        else
            this._ppdToggle.show();
    }

    _upowerCall(method, params, done) {
        Gio.DBus.system.call('org.freedesktop.UPower', '/org/freedesktop/UPower', 'org.freedesktop.DBus.Properties',
            method, params, new GLib.VariantType('(v)'), Gio.DBusCallFlags.NONE, -1, null, (conn, res) => {
                try {
                    done(conn.call_finish(res));
                } catch {
                    done(null);
                }
            });
    }

    _powerSourceChanged(onBattery) {
        if (onBattery === this._onBattery)
            return;
        const first = this._onBattery === null;
        this._onBattery = onBattery;
        if (first)
            return;
        if (this._powerTimer)
            GLib.source_remove(this._powerTimer);
        // wait a bit for tuned and the driver to change the mode, then show the new mode
        this._powerTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 2000, () => {
            this._powerTimer = 0;
            this._announcePowerSource(onBattery);
            return GLib.SOURCE_REMOVE;
        });
    }

    async _announcePowerSource(onBattery) {
        await this._toggle.refresh(false);
        let config = {};
        try {
            config = await readJson(KBD_CONFIG) ?? {};
        } catch {
            // defaults
        }
        if (this._destroyed || config.osd === false)
            return;
        const mode = this._toggle._bannerTitle?.text;
        const title = onBattery ? _('On battery') : _('Plugged in');
        // if power saving applies only on battery, show its state too (Huma Control Center -> Power saving)
        const saving = config.battery_only === false ? null : onBattery ? _('power saving on') : _('power saving paused');
        showOsd(onBattery ? 'battery-level-50-symbolic' : 'ac-adapter-symbolic',
            [title, mode, saving].filter(Boolean).join(' · '));
    }

    async _readAlerts(initial) {
        let items = [];
        try {
            items = (await readJson(ALERTS))?.alerts ?? [];
        } catch {
            return;
        }
        if (this._destroyed)
            return;
        for (const a of items) {
            if (a.id > alertsSeen && !initial)
                Main.notify(...alertText(a));
            alertsSeen = Math.max(alertsSeen, a.id);
        }
    }

    destroy() {
        this._destroyed = true;
        if (this._powerTimer)
            GLib.source_remove(this._powerTimer);
        this._powerTimer = 0;
        Gio.DBus.system.signal_unsubscribe(this._upowerSignal);
        this._monitor?.cancel();
        this._monitor = null;
        this._udev.disconnect(this._udevId);
        this._udev = null;
        // bring back GNOME's Power Mode toggle
        this._ppdToggle?.disconnectObject(this);
        this._toggle.disconnectObject(this);
        if (this._ppdToggle)
            this._restorePpdToggle();
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

// Is anyone at the computer? After being idle (presence_s) the camera takes one look:
// if nobody is there the screen is locked and turned off; if someone is (watching a video,
// reading) dimming is postponed and it looks again after presence_s. Touching the keyboard
// or touchpad ends the postponement. The camera light turns on only while idle, ~1 s per
// look; while someone keeps being found the interval grows (1, 2, 4, 8 times). No look is
// taken while the screen is locked or off.
const FACE_TOOL = '/usr/local/libexec/huma-control-presence';
// below this ambient light (100 ms exposure scale) the camera cannot reliably see a face
const PRESENCE_MIN_LIGHT = 8;
const IDLE_INHIBIT = 8;

class Presence {
    constructor() {
        this._idle = global.backend.get_core_idle_monitor();
        this._watch = 0;
        this._activeWatch = 0;
        this._timer = 0;
        this._cookie = null;
        this._seconds = 0;
        this._monitor = Gio.File.new_for_path(KBD_CONFIG).get_parent().monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, null);
        const configNames = watchedNames(KBD_CONFIG);
        this._monitor.connect('changed', (_m, f, other) => {
            if (configNames.includes((other ?? f)?.get_basename()))
                this.update();
        });
        this.update();
    }

    async update() {
        let config = {};
        try {
            config = await readJson(KBD_CONFIG) ?? {};
        } catch {
            return;
        }
        if (this._destroyed)
            return;
        this._camera = config.presence_camera ?? 'auto';
        const seconds = config.presence && GLib.file_test(FACE_TOOL, GLib.FileTest.IS_EXECUTABLE)
            ? config.presence_s ?? 60 : 0;
        if (seconds === this._seconds)
            return;
        this._seconds = seconds;
        if (this._watch)
            this._idle.remove_watch(this._watch);
        this._watch = seconds ? this._idle.add_idle_watch(seconds * 1000, () => this._check()) : 0;
        if (!seconds)
            this._release();
    }

    _check() {
        this._timer = 0;
        // don't look if the user is back or the screen is locked (no needless camera light);
        // don't look while a video plays (the player already inhibits screen dimming)
        if (!this._seconds || Main.screenShield?.locked || Main.sessionMode.isLocked || videoPaused ||
            this._idle.get_idletime() < this._seconds * 1000 - 1000) {
            this._misses = 0;
            return GLib.SOURCE_REMOVE;
        }
        try {
            const proc = Gio.Subprocess.new([FACE_TOOL, this._camera],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE);
            proc.communicate_utf8_async(null, null, (p, res) => {
                let present = null;
                try {
                    const [, out] = p.communicate_utf8_finish(res);
                    // 2: the camera is in use by a video call, assume someone is there
                    present = p.get_exit_status() === 2 ? true : p.get_successful() ? out.trim() === '1' : null;
                } catch {
                    // could not look: GNOME dims after its own timeout
                }
                this._decide(present).catch(logError);
            });
        } catch {
            // tool missing
        }
        return GLib.SOURCE_REMOVE;
    }

    async _decide(present) {
        // do nothing if the user came back or the setting was turned off while looking
        if (!this._seconds || this._idle.get_idletime() < this._seconds * 1000)
            return;
        // in a dark room the camera (IR included) cannot reliably see a face: don't treat it as
        // "nobody there", leave it to GNOME's normal idle dimming (no locking while someone is there)
        if (present === false) {
            let light = null;
            try {
                light = (await readJson(LIGHT))?.value ?? null;
            } catch {
                light = null;
            }
            if (light === null || light < PRESENCE_MIN_LIGHT)
                present = null;
        }
        if (present === true) {
            this._misses = 0;
            this._hold();
            // look less often while someone keeps being found (no frequent camera light during a video): 1, 2, 4, 8 times, at most 10 min
            const wait = Math.min(this._seconds * 2 ** Math.min(this._found ?? 0, 3), 600);
            this._found = (this._found ?? 0) + 1;
            this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, wait, () => this._check());
        } else if (present === false && !this._misses) {
            // to avoid locking by mistake (the user may have looked away) look again after 10 s
            this._misses = 1;
            this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 10, () => this._check());
        } else if (present === false && this._misses === 1) {
            // warn before locking: touching within 10 s cancels it (if you are there)
            this._misses = 2;
            const idleAtWarning = this._idle.get_idletime();
            this._watchActivity();
            showOsd('system-lock-screen-symbolic', _('Nobody seems to be here: locking in 10 seconds. Touch to cancel.'));
            this._timer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 10, () => {
                this._timer = 0;
                if (this._seconds && this._idle.get_idletime() >= idleAtWarning + 9000) {
                    this._release();
                    Main.screenShield?.lock(true);   // GNOME turns the screen off once locked
                } else {
                    this._release();
                }
                return GLib.SOURCE_REMOVE;
            });
        } else {
            this._release();
        }
    }

    // On touch (the user is back) drop the postponement and any pending lock
    _watchActivity() {
        if (!this._activeWatch)
            this._activeWatch = this._idle.add_user_active_watch(() => {
                this._activeWatch = 0;
                this._release();
            });
    }

    // Someone is there: postpone GNOME dimming and turning off the screen
    _hold() {
        this._watchActivity();
        if (this._cookie !== null)
            return;
        this._cookie = -1;
        Gio.DBus.session.call('org.gnome.SessionManager', '/org/gnome/SessionManager', 'org.gnome.SessionManager',
            'Inhibit', new GLib.Variant('(susu)', ['huma-control', 0, _('Someone is at the computer'), IDLE_INHIBIT]),
            new GLib.VariantType('(u)'), Gio.DBusCallFlags.NONE, -1, null, (conn, res) => {
                try {
                    const [cookie] = conn.call_finish(res).deepUnpack();
                    if (this._cookie === -1)
                        this._cookie = cookie;
                    else
                        this._uninhibit(cookie);   // released in the meantime
                } catch {
                    this._cookie = null;
                }
            });
    }

    _release() {
        this._misses = 0;
        this._found = 0;
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = 0;
        }
        if (this._cookie !== null && this._cookie !== -1)
            this._uninhibit(this._cookie);
        this._cookie = null;
    }

    _uninhibit(cookie) {
        Gio.DBus.session.call('org.gnome.SessionManager', '/org/gnome/SessionManager', 'org.gnome.SessionManager',
            'Uninhibit', new GLib.Variant('(u)', [cookie]), null, Gio.DBusCallFlags.NONE, -1, null, null);
    }

    destroy() {
        this._destroyed = true;
        this._seconds = 0;
        this._release();
        if (this._watch)
            this._idle.remove_watch(this._watch);
        if (this._activeWatch)
            this._idle.remove_watch(this._activeWatch);
        this._monitor.cancel();
    }
}

// No camera measurements during videos/fullscreen: while a fullscreen window exists or a
// player inhibits screen dimming (Firefox, video players) the file
// $XDG_RUNTIME_DIR/huma-control-pause exists; the keyboard service (root) then skips the
// ambient light measurement and the camera, and measures once when the file is gone. Updated
// only by events, no polling.
const PAUSE_FILE = GLib.build_filenamev([GLib.get_user_runtime_dir(), 'huma-control-pause']);
let videoPaused = false;

class VideoPause {
    constructor() {
        this._inhibited = false;
        this._fullscreen = false;
        global.display.connectObject('in-fullscreen-changed', () => this._sync(), this);
        this._signals = ['InhibitorAdded', 'InhibitorRemoved'].map(name =>
            Gio.DBus.session.signal_subscribe('org.gnome.SessionManager', 'org.gnome.SessionManager', name,
                '/org/gnome/SessionManager', null, Gio.DBusSignalFlags.NONE, () => this._checkInhibitors()));
        this._checkInhibitors();
        this._sync();
    }

    _call(path, iface, method, type) {
        return new Promise(resolve => {
            Gio.DBus.session.call('org.gnome.SessionManager', path, iface, method, null,
                new GLib.VariantType(type), Gio.DBusCallFlags.NONE, 2000, null, (conn, res) => {
                    try {
                        resolve(conn.call_finish(res).deepUnpack()[0]);
                    } catch {
                        resolve(null);
                    }
                });
        });
    }

    // Is another application (not our own presence check) inhibiting idle?
    async _checkInhibitors() {
        const paths = await this._call('/org/gnome/SessionManager', 'org.gnome.SessionManager', 'GetInhibitors', '(ao)') ?? [];
        let inhibited = false;
        for (const path of paths) {
            const iface = 'org.gnome.SessionManager.Inhibitor';
            const [app, flags] = await Promise.all([this._call(path, iface, 'GetAppId', '(s)'),
                this._call(path, iface, 'GetFlags', '(u)')]);
            if (app !== 'huma-control' && (flags ?? 0) & IDLE_INHIBIT)
                inhibited = true;
        }
        if (this._destroyed)
            return;
        this._inhibited = inhibited;
        this._sync();
    }

    _sync() {
        this._fullscreen = Main.layoutManager.monitors.some(m => m.inFullscreen);
        const paused = this._inhibited || this._fullscreen;
        if (paused === videoPaused)
            return;
        videoPaused = paused;
        try {
            if (paused)
                GLib.file_set_contents(PAUSE_FILE, '1');
            else
                Gio.File.new_for_path(PAUSE_FILE).delete(null);
        } catch {
            // only a hint
        }
    }

    destroy() {
        this._destroyed = true;
        global.display.disconnectObject(this);
        this._signals.forEach(id => Gio.DBus.session.signal_unsubscribe(id));
        if (videoPaused) {
            videoPaused = false;
            try {
                Gio.File.new_for_path(PAUSE_FILE).delete(null);
            } catch {
                // already gone
            }
        }
    }
}

// Quick access to Huma Control Center from the top bar
const ControlCenterButton = GObject.registerClass(
class ControlCenterButton extends PanelMenu.Button {
    _init() {
        super._init(0.5, APP_NAME, true);
        // show the name on hover (like app names in the Dock)
        this._tip = new St.Label({text: APP_NAME, style_class: 'dash-label hc-tooltip', visible: false});
        Main.uiGroup.add_child(this._tip);
        this._tipTimer = 0;
        this.connect('notify::hover', () => {
            if (this._tipTimer)
                GLib.source_remove(this._tipTimer);
            this._tipTimer = 0;
            if (!this.hover) {
                this._tip.hide();
                return;
            }
            this._tipTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 400, () => {
                this._tipTimer = 0;
                const [x, y] = this.get_transformed_position();
                const [w, h] = this.get_transformed_size();
                this._tip.show();
                const monitor = Main.layoutManager.primaryMonitor;
                const tipW = this._tip.get_width();
                this._tip.set_position(
                    Math.min(Math.max(monitor.x, Math.round(x + w / 2 - tipW / 2)), monitor.x + monitor.width - tipW - 4),
                    Math.round(y + h + 6));
                return GLib.SOURCE_REMOVE;
            });
        });
        this.connect('destroy', () => {
            if (this._tipTimer)
                GLib.source_remove(this._tipTimer);
            this._tip.destroy();
        });
        this.add_child(new St.Icon({
            gicon: Gio.icon_new_for_string(`${EXT_PATH}/icons/hc-fan-symbolic.svg`),
            style_class: 'system-status-icon',
        }));
    }

    vfunc_event(event) {
        const type = event.type();
        // only a click that started on this button opens it (not a drag released here)
        if (type === Clutter.EventType.BUTTON_PRESS || type === Clutter.EventType.TOUCH_BEGIN) {
            this._pressed = true;
            return Clutter.EVENT_STOP;
        }
        if (type === Clutter.EventType.BUTTON_RELEASE || type === Clutter.EventType.TOUCH_END) {
            if (this._pressed)
                this._activate();
            this._pressed = false;
            return Clutter.EVENT_STOP;
        }
        if (type === Clutter.EventType.LEAVE)
            this._pressed = false;
        return Clutter.EVENT_PROPAGATE;
    }

    vfunc_key_press_event(event) {
        const key = event.get_key_symbol();
        if (key === Clutter.KEY_Return || key === Clutter.KEY_KP_Enter || key === Clutter.KEY_space) {
            this._activate();
            return Clutter.EVENT_STOP;
        }
        return super.vfunc_key_press_event(event);
    }

    _activate() {
        this._tip.hide();
        openControlCenter(null);
    }
});

export default class HumaControlExtension extends Extension {
    enable() {
        EXT_PATH = this.path;
        this._button = new ControlCenterButton();
        Main.panel.addToStatusArea('huma-control-button', this._button, 0, 'right');
        this._indicator = new Indicator();
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
        this._brightness = new AutoBrightness();
        this._brightnessMenu = new BrightnessMenu(this._brightness);
        this._presence = new Presence();
        this._videoPause = new VideoPause();
    }

    disable() {
        this._button?.destroy();
        this._button = null;
        this._videoPause?.destroy();
        this._videoPause = null;
        this._presence?.destroy();
        this._presence = null;
        this._brightnessMenu?.destroy();
        this._brightnessMenu = null;
        this._brightness?.destroy();
        this._brightness = null;
        this._indicator?.destroy();
        this._indicator = null;
    }
}
