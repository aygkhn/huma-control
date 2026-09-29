// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
/*
 * Uniwill Control Center features missing from qc71_laptop.
 *
 * Charging profile and charging priority, EC layout from TUXEDO's tuxedo-drivers
 * (uniwill_keyboard.h), which supports the same barebone (PH4TUX1 = InfinityBook
 * Pro 14 Gen6):
 *   0x078e bit 3  charging profile supported
 *   0x07a6 bits 5:4 charging profile: 0 high capacity, 1 balanced, 2 stationary
 *   0x0742 bit 5  charging priority supported
 *   0x07cc bit 7  charging priority: 0 charge battery, 1 performance
 *
 * PH4TUX1 only, measured against Monster Control Center on Windows
 * (docs/ec-register-map.md):
 *   0x0751        performance mode, full byte: 0x00 balanced, 0xa0 quiet (20 dB);
 *                 bit 6 is Fan Boost and is kept
 *   0x0783/0x0784 PL1/PL2 in W; 0x0731 is the EC's own balanced PL2
 *   0x07c5 bit 4  set for the 30 and 40 dB levels
 *   0x0767 bit 4  USB power while off/hibernating (with 0x0766 |= 0x03)
 *   0x07a6 bit 6  touchpad toggle key: 1 = disabled
 *   0x07a6 bit 2  microphone mute LED
 *   0x0f00-0x0f2f fan table (see fan_tables.h), used while 0x07c6 bit 2 is set
 *
 * The EC only accepts a charging profile while 0x0741 bit 0 ("application
 * present") is set; Control Center and upstream uniwill-laptop set it while
 * they run, so it is set here at load and cleared at unload. With it clear the
 * EC drops a new profile at once.
 *
 * The EC may drop settings when the charger is plugged in or out, across
 * suspend and after Windows, so the last requested values are written again on
 * the charger event (see events.c), on a mains power supply change and on
 * resume. Nothing is written at load unless a module parameter asks for it.
 *
 * Writes follow the model policy (models.c): nothing at all in read-only mode
 * (force=1 on an unknown model), and no performance profile (PL1/PL2) or fan
 * table on a BIOS that is not listed as verified for the model.
 */
#include "pr.h"

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#include "ec.h"
#include "fan_tables.h"
#include "features.h"
#include "misc.h"
#include "models.h"
#include "pdev.h"
#include "uniwill_ext.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0) && IS_REACHABLE(CONFIG_ACPI_PLATFORM_PROFILE)
#define HAVE_PLATFORM_PROFILE 1
#endif

#define PROFILE_SUPPORT_ADDR   ADDR(0x07, 0x8e)
#define PROFILE_SUPPORT_BIT    BIT(3)
#define PROFILE_ADDR           ADDR(0x07, 0xa6)
#define PROFILE_SHIFT          4
#define PROFILE_MASK           (0x03 << PROFILE_SHIFT)
#define PRIORITY_SUPPORT_ADDR  ADDR(0x07, 0x42)
#define PRIORITY_SUPPORT_BIT   BIT(5)
#define PRIORITY_ADDR          ADDR(0x07, 0xcc)
#define PRIORITY_BIT           BIT(7)
#define AP_ADDR                CTRL_1_ADDR
#define AP_BIT                 CTRL_1_MANUAL_MODE

#define MODE_ADDR              FAN_CTRL_ADDR
#define MODE_MASK              ((u8) ~FAN_CTRL_FAN_BOOST)
#define MODE_QUIET             (FAN_CTRL_SILENT_MODE | FAN_CTRL_AUTO)
#define FAN_SAFETY_ADDR        ADDR(0x07, 0xc5)
#define FAN_SAFETY_BIT         BIT(4)
#define BALANCED_PL1_DEF_ADDR  ADDR(0x07, 0x30)
#define BALANCED_PL2_DEF_ADDR  ADDR(0x07, 0x31)
#define QUIET_PL1_DEF_ADDR     ADDR(0x07, 0x34)
#define USB_SUPPORT_ADDR       ADDR(0x07, 0x66)
#define USB_SUPPORT_BITS       0x03
#define USB_POWER_ADDR         TRIGGER_1_ADDR
#define USB_POWER_BIT          TRIGGER_1_USB_CHARGING
#define TOUCHPAD_ADDR          ADDR(0x07, 0xa6)
#define TOUCHPAD_OFF_BIT       BIT(6)
#define MICMUTE_ADDR           ADDR(0x07, 0xa6)
#define MICMUTE_BIT            BIT(2)
#define MODE_BITS              (FAN_CTRL_SILENT_MODE | FAN_CTRL_AUTO | FAN_CTRL_TURBO)
#define FAN_TABLE_ADDR         ADDR(0x0f, 0x00)
#define FAN_TABLE_CTRL_ADDR    ADDR(0x07, 0xc6)
#define FAN_TABLE_ON_BIT       BIT(2)
#define FAN_SEPARATE_BIT       BIT(7)
#define FAN_DUTY_ADDR          ADDR(0x07, 0x5b)

static int charging_profile = -1;
module_param(charging_profile, int, 0444);
MODULE_PARM_DESC(charging_profile, "apply at load: 0 high capacity, 1 balanced, 2 stationary (default -1: leave as is)");

static int charging_priority = -1;
module_param(charging_priority, int, 0444);
MODULE_PARM_DESC(charging_priority, "apply at load: 0 charge battery, 1 performance (default -1: leave as is)");

static int performance_profile = -1;
module_param(performance_profile, int, 0444);
MODULE_PARM_DESC(performance_profile, "apply at load: 0-2 balanced low/medium/high, 3-5 quiet 20/30/40 dB (default -1: leave as is)");

static int usb_powershare = -1;
module_param(usb_powershare, int, 0444);
MODULE_PARM_DESC(usb_powershare, "apply at load: USB power while off, 0 or 1 (default -1: leave as is)");

static int touchpad_toggle = -1;
module_param(touchpad_toggle, int, 0444);
MODULE_PARM_DESC(touchpad_toggle, "apply at load: touchpad toggle key, 0 or 1 (default -1: leave as is)");

static int fn_lock = -1;
module_param(fn_lock, int, 0444);
MODULE_PARM_DESC(fn_lock, "apply at load: Fn lock, 0 or 1 (default -1: leave as is)");

static bool fan_table = true;
module_param(fan_table, bool, 0444);
MODULE_PARM_DESC(fan_table, "write Control Center's fan table with each performance profile (default true)");

static char *fan_curve;
module_param(fan_curve, charp, 0444);
MODULE_PARM_DESC(fan_curve, "custom fan table: 48 numbers (see fan_curve in sysfs), default: Control Center's tables");

static bool mode_key = true;
module_param(mode_key, bool, 0444);
MODULE_PARM_DESC(mode_key, "the performance mode key switches balanced <-> quiet (default true)");

static const char *const profile_names[] = { "high_capacity", "balanced", "stationary" };
static const char *const priority_names[] = { "charge_battery", "performance" };
static const char *const perf_names[] = {
	"balanced-low", "balanced-medium", "balanced-high",
	"quiet-20db", "quiet-30db", "quiet-40db",
};

#define PERF_QUIET(p)  ((p) >= 3)
#define PERF_LEVEL(p)  ((p) % 3)

static bool has_profile, has_priority, has_h4, registered;
/* last requested values, -1 = never set: not written again */
static int last_profile = -1, last_priority = -1, last_perf = -1;
static int last_usb = -1, last_touchpad = -1, last_fn_lock = -1, last_fan_boost = -1;

static DEFINE_MUTEX(perf_lock);
/* mode bits of 0x0751 last written or seen, and each mode's last level (Control Center keeps both) */
static int known_mode = -1;
static int mode_level[2] = { 1, 0 };
static bool fan_table_active, micmute_registered;

#ifdef HAVE_PLATFORM_PROFILE
static struct device *ppdev;
#endif

/* ========================================================================== */

static int write_profile(int value)
{
	int err = qc71_ec_update_bits(PROFILE_ADDR, PROFILE_MASK, value << PROFILE_SHIFT);

	if (!err)
		last_profile = value;
	return err;
}

static int write_priority(int value)
{
	int err = qc71_ec_update_bits(PRIORITY_ADDR, PRIORITY_BIT, value ? PRIORITY_BIT : 0);

	if (!err)
		last_priority = value;
	return err;
}

static int write_usb(int on)
{
	int err = 0;

	/* Control Center sets these support bits whenever it starts or resumes */
	if (on)
		err = qc71_ec_update_bits(USB_SUPPORT_ADDR, USB_SUPPORT_BITS, USB_SUPPORT_BITS);
	if (!err)
		err = qc71_ec_update_bits(USB_POWER_ADDR, USB_POWER_BIT, on ? USB_POWER_BIT : 0);
	if (!err)
		last_usb = on;
	return err;
}

static int write_touchpad(int on)
{
	int err = qc71_ec_update_bits(TOUCHPAD_ADDR, TOUCHPAD_OFF_BIT, on ? 0 : TOUCHPAD_OFF_BIT);

	if (!err)
		last_touchpad = on;
	return err;
}

static int write_fn_lock(int on)
{
	int err = qc71_fn_lock_set_state(on);

	if (!err)
		last_fn_lock = on;
	return err;
}

static int write_fan_boost(int on)
{
	int err = qc71_ec_update_bits(MODE_ADDR, FAN_CTRL_FAN_BOOST, on ? FAN_CTRL_FAN_BOOST : 0);

	if (!err)
		last_fan_boost = on;
	return err;
}

/* ========================================================================== */

static bool on_mains(void)
{
	/* no power supply information: assume mains, as Control Center does */
	return power_supply_is_system_supplied() != 0;
}

/*
 * Control Center order: clear the table bit, write the table, set the bit again.
 * Every EC access is a slow WMI call (~30 ms here), so the driver remembers
 * which table it wrote and changes only the bytes that differ. After resume or
 * a charger change (verify) the EC's table is read back and compared first.
 */
#define CUSTOM_TABLE 6
static u8 custom_table[FAN_TABLE_LEN];
static bool custom_active;
/* what the driver last wrote to 0x0f00-0x0f2f, to change only differing bytes */
static u8 ec_table[FAN_TABLE_LEN];
static bool ec_table_valid;

static const u8 *table_data(int id)
{
	return id == CUSTOM_TABLE ? custom_table : qc71_fan_tables[id];
}

static int table_id(int perf)
{
	return custom_active ? CUSTOM_TABLE : perf;
}

static int write_fan_table(int id, bool verify)
{
	const u8 *table = table_data(id);
	const u8 *known = NULL;
	bool same = true;
	int i, data, err;

	if (verify) {
		data = ec_read_byte(FAN_TABLE_CTRL_ADDR);
		if (data < 0)
			return data;
		for (i = 0; same && i < FAN_TABLE_LEN; i++)
			same = ec_read_byte(FAN_TABLE_ADDR + i) == table[i];
		if (same && (data & FAN_TABLE_ON_BIT)) {
			memcpy(ec_table, table, FAN_TABLE_LEN);
			ec_table_valid = fan_table_active = true;
			return 0;
		}
	} else if (ec_table_valid) {
		if (!memcmp(ec_table, table, FAN_TABLE_LEN))
			return 0;
		known = ec_table;
	}

	err = qc71_ec_update_bits(FAN_TABLE_CTRL_ADDR, FAN_TABLE_ON_BIT, 0);
	if (!err && !known)
		err = qc71_ec_update_bits(FAN_SAFETY_ADDR, FAN_SEPARATE_BIT, 0);
	for (i = 0; !err && i < FAN_TABLE_LEN; i++) {
		if (known && known[i] == table[i])
			continue;
		err = ec_write_byte(FAN_TABLE_ADDR + i, table[i]);
		if (!err && known)
			ec_table[i] = table[i];
	}
	if (err) {
		ec_table_valid = false;
		return err;
	}
	err = qc71_ec_update_bits(FAN_TABLE_CTRL_ADDR, FAN_TABLE_ON_BIT, FAN_TABLE_ON_BIT);
	if (!err) {
		memcpy(ec_table, table, FAN_TABLE_LEN);
		ec_table_valid = fan_table_active = true;
	}
	return err;
}

/*
 * A custom fan table must keep the fan safe: thresholds rise between 30 and
 * 85 degC, every falling threshold is below its rising one and the falling
 * thresholds rise too, the duty never drops as the temperature rises, the
 * fan runs at least 20 % from 70 degC on (first threshold at most 70, its
 * duty at least 20 %) and the last level runs at 100 %. The CPU's own thermal
 * throttling and shutdown stay in force whatever the table says.
 * Layout as fan_tables.h: 16 rising, 16 falling thresholds, 16 duties (% x 2).
 */
static int validate_table(const u8 *t)
{
	const u8 *up = t, *down = t + 16, *duty = t + 32;
	int n = 0, i;

	while (n < 15 && up[n] != 0xff)
		n++;
	if (n == 0)
		return -EINVAL;
	for (i = n; i < 16; i++)
		if (up[i] != 0xff)
			return -EINVAL;
	for (i = 0; i < n; i++)
		if (up[i] < 30 || up[i] > 85 || (i && up[i] <= up[i - 1]))
			return -EINVAL;
	if (down[0] != 0)
		return -EINVAL;
	for (i = 1; i <= n; i++)
		if (down[i] < 20 || down[i] >= up[i - 1] || (i > 1 && down[i] <= down[i - 1]))
			return -EINVAL;
	/* no "fan off until it is hot" curves */
	if (up[0] > 70 || duty[1] < 40)
		return -EINVAL;
	for (i = n + 1; i < 16; i++)
		if (down[i] != 0xff)
			return -EINVAL;
	for (i = 0; i < 16; i++) {
		if (duty[i] > 200)
			return -EINVAL;
		if (i && i <= n && duty[i] < duty[i - 1])
			return -EINVAL;
		if (i > n && duty[i] != duty[n])
			return -EINVAL;
	}
	return duty[n] == 200 ? 0 : -EINVAL;
}

static int parse_table(const char *buf, u8 *out)
{
	char *copy = kstrdup(buf, GFP_KERNEL), *p = copy, *tok;
	int n = 0, err = 0;

	if (!copy)
		return -ENOMEM;
	while ((tok = strsep(&p, " ,\t\n")) != NULL) {
		if (!*tok)
			continue;
		if (n >= FAN_TABLE_LEN || kstrtou8(tok, 10, &out[n])) {
			err = -EINVAL;
			break;
		}
		n++;
	}
	kfree(copy);
	if (!err && n != FAN_TABLE_LEN)
		err = -EINVAL;
	return err ?: validate_table(out);
}

/*
 * The table is written from a work item, as Control Center queues it on a
 * background thread: the mode and power limits apply at once, the ~20 table
 * writes (~1.5 s over WMI) follow.
 */
static DEFINE_MUTEX(table_lock);
static int pending_table = -1;
static bool pending_verify;

static void fan_table_work_fn(struct work_struct *work)
{
	int perf;
	bool verify;

	mutex_lock(&table_lock);
	perf = pending_table;
	verify = pending_verify;
	pending_table = -1;
	pending_verify = false;
	if (perf >= 0 && write_fan_table(perf, verify))
		pr_warn("failed to write the fan table\n");
	mutex_unlock(&table_lock);
}

static DECLARE_WORK(fan_table_work, fan_table_work_fn);

static void queue_fan_table(int perf, bool verify)
{
	mutex_lock(&table_lock);
	pending_table = perf;
	pending_verify |= verify;
	mutex_unlock(&table_lock);
	schedule_work(&fan_table_work);
}

/* Back to the EC's own fan curve, as Control Center does when it stops */
static void clear_fan_table(void)
{
	int i;

	if (!fan_table_active)
		return;
	fan_table_active = false;
	ec_table_valid = false;
	if (qc71_ec_update_bits(FAN_TABLE_CTRL_ADDR, FAN_TABLE_ON_BIT, 0))
		return;
	for (i = 0; i < FAN_TABLE_LEN; i++)
		if (ec_write_byte(FAN_TABLE_ADDR + i, 0))
			break;
}

/* Control Center order: mode byte, fan safety bit, fan table, then PL1 and PL2 */
/*
 * PERF_RESTORE: write the last requested profile again (charger change, resume).
 * It is looked up under perf_lock: reading last_perf before taking the lock let a
 * slow restore overwrite the profile tuned had just set on unplug (battery ran at
 * Balanced Medium instead of Quiet 20 dB).
 */
#define PERF_RESTORE -2

static int write_perf(int perf, bool verify)
{
	static int balanced_pl2;
	static const u8 balanced_pl1[] = { 30, 32, 38 };
	static const u8 quiet_pl1[] = { 15, 25, 35 };
	int level, pl1, pl2, err;

	if (!qc71_power_writes_allowed())
		return -EPERM;

	mutex_lock(&perf_lock);
	if (perf == PERF_RESTORE)
		perf = last_perf;
	if (perf < 0) {
		mutex_unlock(&perf_lock);
		return 0;
	}
	level = PERF_LEVEL(perf);
	if (PERF_QUIET(perf)) {
		pl1 = quiet_pl1[level];
		pl2 = 35;
	} else {
		pl1 = balanced_pl1[level];
		/* High is 35 W on battery */
		if (level == 2 && !on_mains())
			pl1 = 35;
		/* the EC's own balanced PL2 does not change, read it once */
		if (balanced_pl2 <= 0)
			balanced_pl2 = ec_read_byte(BALANCED_PL2_DEF_ADDR);
		/* a garbage read must not become the power limit */
		pl2 = balanced_pl2 >= 30 && balanced_pl2 <= 65 ? balanced_pl2 : 60;
	}

	err = qc71_ec_update_bits(MODE_ADDR, MODE_MASK, PERF_QUIET(perf) ? MODE_QUIET : 0);
	if (err)
		goto out;
	msleep(10);
	known_mode = PERF_QUIET(perf) ? MODE_QUIET : 0;
	err = qc71_ec_update_bits(FAN_SAFETY_ADDR, FAN_SAFETY_BIT,
				  PERF_QUIET(perf) && level > 0 ? FAN_SAFETY_BIT : 0);
	if (err)
		goto out;
	msleep(10);
	if (fan_table)
		queue_fan_table(table_id(perf), verify);
	err = qc71_ec_update_bits(PL1_ADDR, 0xff, pl1);
	if (err)
		goto out;
	msleep(10);
	err = qc71_ec_update_bits(PL2_ADDR, 0xff, pl2);
	if (!err) {
		last_perf = perf;
		mode_level[PERF_QUIET(perf)] = level;
	}
out:
	mutex_unlock(&perf_lock);
	return err;
}

/* Current profile from the EC, -1 if it is none of ours (e.g. left by Windows) */
static int read_perf(void)
{
	int mode = ec_read_byte(MODE_ADDR), pl1 = ec_read_byte(PL1_ADDR);
	bool quiet;

	if (mode < 0)
		return mode;
	if (pl1 < 0)
		return pl1;

	if ((mode & MODE_MASK) == MODE_QUIET)
		quiet = true;
	else if ((mode & MODE_MASK) == 0)
		quiet = false;
	else
		return -1;

	/* 0 = the EC's own default for the mode */
	if (pl1 == 0)
		pl1 = ec_read_byte(quiet ? QUIET_PL1_DEF_ADDR : BALANCED_PL1_DEF_ADDR);

	if (quiet) {
		switch (pl1) {
		case 15: return 3;
		case 25: return 4;
		case 35: return 5;
		}
	} else {
		switch (pl1) {
		case 30: return 0;
		case 32: return 1;
		case 35:
		case 38: return 2;
		}
	}
	return -1;
}

/* For readers outside write_perf: do not see a half written profile */
static int read_perf_locked(void)
{
	int perf;

	mutex_lock(&perf_lock);
	perf = read_perf();
	mutex_unlock(&perf_lock);
	return perf;
}

/* udev "change" event, so the desktop (Huma Control Center extension) can react */
static void announce(const char *what)
{
	char env[48];
	char *envp[] = { env, NULL };

	snprintf(env, sizeof(env), "HUMA_CONTROL=%s", what);
	kobject_uevent_env(&qc71_platform_dev->dev.kobj, KOBJ_CHANGE, envp);
}

static void notify_perf(bool from_platform_profile)
{
	sysfs_notify(&qc71_platform_dev->dev.kobj, NULL, "performance_profile");
	announce("performance");
#ifdef HAVE_PLATFORM_PROFILE
	if (!from_platform_profile && ppdev)
		platform_profile_notify(ppdev);
#endif
}

/* ========================================================================== */

static ssize_t show_choice(char *buf, const char *const *names, size_t n, int value)
{
	if (value < 0 || value >= n)
		return sysfs_emit(buf, "unknown\n");
	return sysfs_emit(buf, "%s\n", names[value]);
}

static ssize_t show_available(char *buf, const char *const *names, size_t n)
{
	ssize_t len = 0;
	size_t i;

	for (i = 0; i < n; i++)
		len += sysfs_emit_at(buf, len, "%s%s", names[i], i + 1 < n ? " " : "\n");
	return len;
}

static ssize_t show_bit(char *buf, u16 addr, u8 bit, bool inverted)
{
	int data = ec_read_byte(addr);

	if (data < 0)
		return data;
	return sysfs_emit(buf, "%d\n", !!(data & bit) != inverted);
}

static ssize_t store_bool(const char *buf, size_t count, int (*write)(int))
{
	bool value;
	int err;

	if (kstrtobool(buf, &value))
		return -EINVAL;
	err = write(value);
	return err ? err : count;
}

/* ========================================================================== */

static ssize_t charging_profile_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int data = ec_read_byte(PROFILE_ADDR);

	if (data < 0)
		return data;
	return show_choice(buf, profile_names, ARRAY_SIZE(profile_names),
			   (data & PROFILE_MASK) >> PROFILE_SHIFT);
}

static ssize_t charging_profile_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int value = sysfs_match_string(profile_names, buf);
	int err;

	if (value < 0)
		return -EINVAL;
	err = write_profile(value);
	return err ? err : count;
}

static ssize_t charging_profiles_available_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_available(buf, profile_names, ARRAY_SIZE(profile_names));
}

static ssize_t charging_priority_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int data = ec_read_byte(PRIORITY_ADDR);

	if (data < 0)
		return data;
	return show_choice(buf, priority_names, ARRAY_SIZE(priority_names), !!(data & PRIORITY_BIT));
}

static ssize_t charging_priority_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int value = sysfs_match_string(priority_names, buf);
	int err;

	if (value < 0)
		return -EINVAL;
	err = write_priority(value);
	return err ? err : count;
}

static ssize_t charging_priorities_available_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_available(buf, priority_names, ARRAY_SIZE(priority_names));
}

static ssize_t performance_profile_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int perf = read_perf_locked();

	if (perf < -1)
		return perf;
	return show_choice(buf, perf_names, ARRAY_SIZE(perf_names), perf);
}

static ssize_t performance_profile_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int value = sysfs_match_string(perf_names, buf);
	int err;

	if (value < 0)
		return -EINVAL;
	err = write_perf(value, false);
	if (err)
		return err;
	notify_perf(false);
	return count;
}

static ssize_t performance_profiles_available_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_available(buf, perf_names, ARRAY_SIZE(perf_names));
}

static ssize_t fan_boost_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_bit(buf, MODE_ADDR, FAN_CTRL_FAN_BOOST, false);
}

static ssize_t fan_boost_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return store_bool(buf, count, write_fan_boost);
}

static ssize_t usb_powershare_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_bit(buf, USB_POWER_ADDR, USB_POWER_BIT, false);
}

static ssize_t usb_powershare_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	return store_bool(buf, count, write_usb);
}

static ssize_t touchpad_toggle_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_bit(buf, TOUCHPAD_ADDR, TOUCHPAD_OFF_BIT, true);
}

static ssize_t touchpad_toggle_enable_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t count)
{
	return store_bool(buf, count, write_touchpad);
}

static ssize_t fan_duty_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int data = ec_read_byte(FAN_DUTY_ADDR);

	if (data < 0)
		return data;
	return sysfs_emit(buf, "%d\n", min(data / 2, 100));
}

static ssize_t project_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int data = ec_read_byte(PROJ_ID_ADDR);

	if (data < 0)
		return data;
	return sysfs_emit(buf, "0x%02x\n", data);
}

static DEVICE_ATTR_RO(fan_duty);
static ssize_t fan_fault_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return show_bit(buf, CTRL_1_ADDR, CTRL_1_FAN_ABNORMAL, false);
}

static ssize_t fan_curve_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i;

	if (!custom_active)
		return sysfs_emit(buf, "auto\n");
	for (i = 0; i < FAN_TABLE_LEN; i++)
		len += sysfs_emit_at(buf, len, "%u%c", custom_table[i], i + 1 < FAN_TABLE_LEN ? ' ' : '\n');
	return len;
}

static ssize_t fan_curve_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u8 table[FAN_TABLE_LEN];
	int err, perf;

	if (!qc71_power_writes_allowed())
		return -EPERM;

	if (sysfs_streq(buf, "auto")) {
		mutex_lock(&table_lock);
		custom_active = false;
		mutex_unlock(&table_lock);
	} else {
		err = parse_table(buf, table);
		if (err)
			return err;
		mutex_lock(&table_lock);
		memcpy(custom_table, table, FAN_TABLE_LEN);
		custom_active = true;
		mutex_unlock(&table_lock);
	}

	/* the custom table applies to every profile; back to auto needs the profile */
	perf = last_perf >= 0 ? last_perf : read_perf_locked();
	if (fan_table && (custom_active || perf >= 0))
		queue_fan_table(table_id(perf), false);
	sysfs_notify(&qc71_platform_dev->dev.kobj, NULL, "fan_curve");
	announce("fan_curve");
	return count;
}

static DEVICE_ATTR_RO(project_id);
static DEVICE_ATTR_RO(fan_fault);
static DEVICE_ATTR_RW(fan_curve);
static DEVICE_ATTR_RW(charging_profile);
static DEVICE_ATTR_RO(charging_profiles_available);
static DEVICE_ATTR_RW(charging_priority);
static DEVICE_ATTR_RO(charging_priorities_available);
static DEVICE_ATTR_RW(performance_profile);
static DEVICE_ATTR_RO(performance_profiles_available);
static DEVICE_ATTR_RW(fan_boost);
static DEVICE_ATTR_RW(usb_powershare);
static DEVICE_ATTR_RW(touchpad_toggle_enable);

static struct attribute *uniwill_ext_attrs[] = {
	&dev_attr_charging_profile.attr,
	&dev_attr_charging_profiles_available.attr,
	&dev_attr_charging_priority.attr,
	&dev_attr_charging_priorities_available.attr,
	&dev_attr_performance_profile.attr,
	&dev_attr_performance_profiles_available.attr,
	&dev_attr_fan_boost.attr,
	&dev_attr_usb_powershare.attr,
	&dev_attr_touchpad_toggle_enable.attr,
	&dev_attr_fan_duty.attr,
	&dev_attr_project_id.attr,
	&dev_attr_fan_fault.attr,
	&dev_attr_fan_curve.attr,
	NULL
};

static umode_t uniwill_ext_attr_mode(struct attribute *attr)
{
	if (attr == &dev_attr_charging_profile.attr || attr == &dev_attr_charging_profiles_available.attr)
		return has_profile ? attr->mode : 0;
	if (attr == &dev_attr_charging_priority.attr || attr == &dev_attr_charging_priorities_available.attr)
		return has_priority ? attr->mode : 0;
	if (attr == &dev_attr_project_id.attr)
		return attr->mode;
	return has_h4 ? attr->mode : 0;
}

/* read-only mode, or an unverified BIOS for the power attributes: no write bits */
static umode_t uniwill_ext_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	umode_t mode = uniwill_ext_attr_mode(attr);

	if (!qc71_writes_allowed() ||
	    (!qc71_power_writes_allowed() &&
	     (attr == &dev_attr_performance_profile.attr || attr == &dev_attr_fan_curve.attr)))
		mode &= ~0222;
	return mode;
}

static const struct attribute_group uniwill_ext_group = {
	.is_visible = uniwill_ext_attr_is_visible,
	.attrs = uniwill_ext_attrs,
};

/* ========================================================================== */

#ifdef HAVE_PLATFORM_PROFILE
/* low-power 20 dB, quiet 30 dB, balanced = balanced medium, performance = balanced high */
static const int pp_to_perf[PLATFORM_PROFILE_LAST] = {
	[0 ... PLATFORM_PROFILE_LAST - 1] = -1,
	[PLATFORM_PROFILE_LOW_POWER] = 3,
	[PLATFORM_PROFILE_QUIET] = 4,
	[PLATFORM_PROFILE_BALANCED] = 1,
	[PLATFORM_PROFILE_PERFORMANCE] = 2,
};

static int pp_probe(void *drvdata, unsigned long *choices)
{
	int i;

	for (i = 0; i < PLATFORM_PROFILE_LAST; i++)
		if (pp_to_perf[i] >= 0)
			set_bit(i, choices);
	return 0;
}

static int pp_get(struct device *dev, enum platform_profile_option *profile)
{
	int perf = read_perf_locked(), i;

	if (perf < -1)
		return perf;
	for (i = 0; i < PLATFORM_PROFILE_LAST; i++) {
		if (perf >= 0 && pp_to_perf[i] == perf) {
			*profile = i;
			return 0;
		}
	}
	*profile = PLATFORM_PROFILE_CUSTOM;
	return 0;
}

static int pp_set(struct device *dev, enum platform_profile_option profile)
{
	int err;

	if (!qc71_power_writes_allowed())
		return -EPERM;
	if (profile >= PLATFORM_PROFILE_LAST || pp_to_perf[profile] < 0)
		return -EOPNOTSUPP;
	err = write_perf(pp_to_perf[profile], false);
	if (!err)
		notify_perf(true);
	return err;
}

static const struct platform_profile_ops pp_ops = {
	.probe = pp_probe,
	.profile_get = pp_get,
	.profile_set = pp_set,
};
#endif

/* ========================================================================== */

static void restore_work_fn(struct work_struct *work)
{
	if (!qc71_writes_allowed())
		return;
	if (qc71_ec_update_bits(AP_ADDR, AP_BIT, AP_BIT))
		pr_warn("failed to set the application present flag\n");
	/* Control Center sets these support bits on every start and resume */
	if (has_h4)
		(void) qc71_ec_update_bits(USB_SUPPORT_ADDR, USB_SUPPORT_BITS, USB_SUPPORT_BITS);
	if (has_h4 && last_usb >= 0)
		(void) write_usb(last_usb);
	if (has_h4 && last_touchpad >= 0)
		(void) write_touchpad(last_touchpad);
	if (last_fn_lock >= 0)
		(void) write_fn_lock(last_fn_lock);
	if (has_profile && last_profile >= 0)
		(void) write_profile(last_profile);
	if (has_priority && last_priority >= 0)
		(void) write_priority(last_priority);
	if (has_h4 && qc71_power_writes_allowed())
		(void) write_perf(PERF_RESTORE, true);
	if (has_h4 && last_fan_boost >= 0)
		(void) write_fan_boost(last_fan_boost);
}

static DECLARE_DELAYED_WORK(restore_work, restore_work_fn);

static void schedule_restore(unsigned int ms)
{
	if (registered && qc71_writes_allowed())
		schedule_delayed_work(&restore_work, msecs_to_jiffies(ms));
}

/* Called from the charger plugged/unplugged WMI event; the EC is written later
 * from a work item, not from the event handler */
void qc71_uniwill_ext_restore(void)
{
	schedule_restore(100);
}

/* The Fn+Esc key changed the Fn lock: keep that state across resume */
void qc71_uniwill_ext_fn_lock_changed(int state)
{
	if (state >= 0)
		last_fn_lock = state;
}

/* Fan Boost changed by the EC (boost key, or forced off) */
void qc71_uniwill_ext_fan_boost_changed(void)
{
	int data;

	if (!registered || !has_h4)
		return;
	data = ec_read_byte(MODE_ADDR);
	if (data >= 0)
		last_fan_boost = !!(data & FAN_CTRL_FAN_BOOST);
	sysfs_notify(&qc71_platform_dev->dev.kobj, NULL, "fan_boost");
	announce("fan_boost");
}

/*
 * Performance mode key (EC event 0xb0). On Windows the Control Center service
 * switches balanced <-> quiet, keeping each mode's level. If the EC already
 * changed the mode by itself, only follow it.
 */
static void mode_key_work_fn(struct work_struct *work)
{
	int data = ec_read_byte(MODE_ADDR), perf;

	if (data < 0)
		return;
	if (known_mode >= 0 && (data & MODE_BITS) != known_mode) {
		pr_info("mode key: the EC changed the mode itself (0x%02x)\n", data);
		known_mode = data & MODE_BITS;
		perf = read_perf();
		if (perf >= 0)
			last_perf = perf;
	} else {
		bool quiet = !((data & MODE_BITS) == MODE_QUIET);

		perf = (quiet ? 3 : 0) + mode_level[quiet];
		pr_info("mode key: switching to %s\n", perf_names[perf]);
		if (write_perf(perf, false))
			return;
	}
	notify_perf(false);
}

static DECLARE_WORK(mode_key_work, mode_key_work_fn);

void qc71_uniwill_ext_mode_key(void)
{
	if (registered && has_h4 && mode_key && qc71_power_writes_allowed())
		schedule_work(&mode_key_work);
}

/* Microphone mute LED, driven by the audio-micmute trigger */
static int micmute_set(struct led_classdev *cdev, enum led_brightness value)
{
	return qc71_ec_update_bits(MICMUTE_ADDR, MICMUTE_BIT, value ? MICMUTE_BIT : 0);
}

static struct led_classdev micmute_led = {
	.name = "platform::micmute",
	.max_brightness = 1,
	.brightness_set_blocking = micmute_set,
	.default_trigger = "audio-micmute",
};

/* hwmon pwm1_enable on PH4TUX1: 0 = Fan Boost, 2 = automatic */
int qc71_uniwill_ext_set_fan_boost(bool on)
{
	return write_fan_boost(on);
}

static int pm_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	switch (action) {
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		schedule_restore(500);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block pm_nb = { .notifier_call = pm_notify };

/* May run in atomic context: only schedule the work */
static int psy_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	struct power_supply *psy = data;

	if (action == PSY_EVENT_PROP_CHANGED && psy->desc->type == POWER_SUPPLY_TYPE_MAINS)
		schedule_restore(100);
	return NOTIFY_DONE;
}

static struct notifier_block psy_nb = { .notifier_call = psy_notify };

/* ========================================================================== */

static bool param_ok(int value, int max)
{
	return value >= 0 && value <= max;
}

int __init qc71_uniwill_ext_setup(void)
{
	int data, err;

	data = ec_read_byte(PROFILE_SUPPORT_ADDR);
	has_profile = data >= 0 && (data & PROFILE_SUPPORT_BIT);
	data = ec_read_byte(PRIORITY_SUPPORT_ADDR);
	has_priority = data >= 0 && (data & PRIORITY_SUPPORT_BIT);
	has_h4 = qc71_features.ph4tux1;

	if (!has_profile && !has_priority && !has_h4)
		return -ENODEV;

	/* read-only mode: no application present flag, nothing applied at load */
	if (qc71_writes_allowed()) {
		err = qc71_ec_update_bits(AP_ADDR, AP_BIT, AP_BIT);
		if (err)
			pr_warn("failed to set the application present flag: %d\n", err);
	}

	if (has_h4) {
		data = ec_read_byte(MODE_ADDR);
		if (data >= 0)
			known_mode = data & MODE_BITS;
		data = read_perf();
		if (data >= 0)
			mode_level[PERF_QUIET(data)] = PERF_LEVEL(data);
		if (qc71_writes_allowed())
			(void) qc71_ec_update_bits(USB_SUPPORT_ADDR, USB_SUPPORT_BITS, USB_SUPPORT_BITS);
	}

	if (has_h4 && fan_curve && *fan_curve && !qc71_power_writes_allowed()) {
		pr_warn("fan_curve ignored: fan tables are read-only here\n");
	} else if (has_h4 && fan_curve && *fan_curve) {
		if (parse_table(fan_curve, custom_table)) {
			pr_warn("fan_curve rejected (unsafe or malformed), using Control Center's tables\n");
		} else {
			custom_active = true;
			if (fan_table)
				queue_fan_table(CUSTOM_TABLE, true);
		}
	}

	if (!qc71_writes_allowed())
		goto skip_params;
	if (has_profile && param_ok(charging_profile, ARRAY_SIZE(profile_names) - 1))
		(void) write_profile(charging_profile);
	if (has_priority && param_ok(charging_priority, ARRAY_SIZE(priority_names) - 1))
		(void) write_priority(charging_priority);
	if (has_h4 && param_ok(performance_profile, ARRAY_SIZE(perf_names) - 1))
		(void) write_perf(performance_profile, true);
	if (has_h4 && param_ok(usb_powershare, 1))
		(void) write_usb(usb_powershare);
	if (has_h4 && param_ok(touchpad_toggle, 1))
		(void) write_touchpad(touchpad_toggle);
	if (qc71_features.fn_lock && param_ok(fn_lock, 1))
		(void) write_fn_lock(fn_lock);
skip_params:

	err = sysfs_create_group(&qc71_platform_dev->dev.kobj, &uniwill_ext_group);
	if (err)
		goto clear_ap;

	err = register_pm_notifier(&pm_nb);
	if (err)
		goto remove_group;

	err = power_supply_reg_notifier(&psy_nb);
	if (err)
		goto unreg_pm;

	/* the LED cannot be read back: nothing to show in read-only mode */
	if (has_h4 && IS_ENABLED(CONFIG_LEDS_CLASS) && qc71_writes_allowed()) {
		if (led_classdev_register(&qc71_platform_dev->dev, &micmute_led))
			pr_warn("failed to register the microphone mute LED\n");
		else
			micmute_registered = true;
	}

#ifdef HAVE_PLATFORM_PROFILE
	/* power-profiles-daemon/tuned would only get -EPERM on an unverified BIOS */
	if (has_h4 && qc71_power_writes_allowed()) {
		ppdev = platform_profile_register(&qc71_platform_dev->dev, "qc71_laptop", NULL, &pp_ops);
		if (IS_ERR(ppdev)) {
			pr_warn("failed to register platform profile: %ld\n", PTR_ERR(ppdev));
			ppdev = NULL;
		}
	}
#endif

	registered = true;
	pr_info("charging profile %ssupported, charging priority %ssupported, PH4TUX1 features %s\n",
		has_profile ? "" : "not ", has_priority ? "" : "not ", has_h4 ? "on" : "off");
	return 0;

unreg_pm:
	unregister_pm_notifier(&pm_nb);
remove_group:
	sysfs_remove_group(&qc71_platform_dev->dev.kobj, &uniwill_ext_group);
clear_ap:
	/* the fan_curve parameter may have queued the table above */
	cancel_work_sync(&fan_table_work);
	if (qc71_writes_allowed())
		(void) qc71_ec_update_bits(AP_ADDR, AP_BIT, 0);
	return err;
}

void qc71_uniwill_ext_cleanup(void)
{
	if (registered) {
		registered = false;
#ifdef HAVE_PLATFORM_PROFILE
		if (ppdev)
			platform_profile_remove(ppdev);
		ppdev = NULL;
#endif
		/*
		 * Remove the writers first (sysfs waits for running stores), then
		 * disable the work items so nothing can queue them again before the
		 * module text goes away.
		 */
		sysfs_remove_group(&qc71_platform_dev->dev.kobj, &uniwill_ext_group);
		if (micmute_registered)
			led_classdev_unregister(&micmute_led);
		micmute_registered = false;
		power_supply_unreg_notifier(&psy_nb);
		unregister_pm_notifier(&pm_nb);
		disable_delayed_work_sync(&restore_work);
		disable_work_sync(&mode_key_work);
		disable_work_sync(&fan_table_work);
		mutex_lock(&table_lock);
		clear_fan_table();
		mutex_unlock(&table_lock);
		if (qc71_writes_allowed())
			(void) qc71_ec_update_bits(AP_ADDR, AP_BIT, 0);
	}
}
