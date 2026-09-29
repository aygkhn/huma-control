// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#include "pr.h"

#include <linux/ctype.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ec.h"
#include "features.h"
#include "models.h"

/* ========================================================================== */

struct oem_string_walker_data {
	char *value;
	int index;
};

static bool show_charge_limit;
module_param(show_charge_limit, bool, 0444);
MODULE_PARM_DESC(show_charge_limit, "expose battery charge limit (default=false)");

/* ========================================================================== */

static bool is_slimbook __initdata;

static int __init slimbook_dmi_cb(const struct dmi_system_id *id)
{
	is_slimbook = true;
	qc71_features.fn_lock           = true;
	qc71_features.fn_lock_switch    = true;
	qc71_features.silent_mode       = true;
	qc71_features.turbo_mode        = true;
	qc71_features.batt_charge_limit = show_charge_limit;

	return 1;
}

static const struct dmi_system_id qc71_dmi_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "LAPQC71"),
			{ }
		}
	},
	{
		/* https://avell.com.br/avell-a60-muv-295765 */
		.matches = {
			DMI_EXACT_MATCH(DMI_CHASSIS_VENDOR, "Avell High Performance"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "A60 MUV"),
			{ }
		}
	},
	{
		/* Slimbook */
		.callback = slimbook_dmi_cb,
		.matches = {
				DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "SLIMBOOK"), 
			{ }
		}
	},
	{ }
};

/* ========================================================================== */

struct qc71_features_struct qc71_features;
uint32_t qc71_model;

/* ========================================================================== */

static void __init oem_string_walker(const struct dmi_header *dm, void *ptr)
{
	int i, count;
	const uint8_t *s;
	struct oem_string_walker_data *data = ptr;

	if (dm->type != 11 || dm->length < 5 || !IS_ERR_OR_NULL(data->value))
		return;

	count = *(uint8_t *)(dm + 1);

	if (data->index >= count)
		return;

	i = 0;
	s = ((uint8_t *)dm) + dm->length;

	while (i++ < data->index && *s)
		s += strlen(s) + 1;

	data->value = kstrdup(s, GFP_KERNEL);

	if (!data->value)
		data->value = ERR_PTR(-ENOMEM);
}

static char * __init read_oem_string(int index)
{
	struct oem_string_walker_data d = {.value = ERR_PTR(-ENOENT),
					   .index = index};
	int err = dmi_walk(oem_string_walker, &d);

	if (err) {
		if (!IS_ERR_OR_NULL(d.value))
			kfree(d.value);
		return ERR_PTR(err);
	}

	return d.value;
}

/* QCCFL357.0062.2020.0313.1530 -> 62 */
static int __pure __init parse_bios_version(const char *str)
{
	const char *p = strchr(str, '.'), *p2;
	int bios_version;

	if (!p)
		return -EINVAL;

	p2 = strchr(p + 1, '.');

	if (!p2)
		return -EINVAL;

	p += 1;

	bios_version = 0;

	while (p != p2) {
		if (!isdigit(*p))
			return -EINVAL;

		bios_version = 10 * bios_version + *p - '0';
		p += 1;
	}

	return bios_version;
}

static int __init check_features_ec(void)
{
	int err = ec_read_byte(SUPPORT_1_ADDR);

	if (err >= 0) {
		qc71_features.super_key_lock = !!(err & SUPPORT_1_SUPER_KEY_LOCK);
		qc71_features.lightbar       = !!(err & SUPPORT_1_LIGHTBAR);
		qc71_features.fan_boost      = !!(err & SUPPORT_1_FAN_BOOST);
	} else {
		pr_warn("failed to query support_1 byte: %d\n", err);
	}

	err = ec_read_byte(SUPPORT_2_ADDR);

	if (err >= 0) {
		qc71_features.kbd_backlight_rgb = !!(err & SUPPORT_2_SINGLE_ZONE_KBD);
	} else {
		pr_warn("failed to query support_2 byte: %d\n", err);
	}

	/*
	 * White keyboards keep their level in bits 7:5 of the keyboard backlight
	 * status register, as the single zone RGB ones do, but the EC has no bit
	 * that tells them apart: bit 0 of that register follows the backlight
	 * being on. So they are listed by EC project id.
	 */
	err = ec_read_byte(PROJ_ID_ADDR);

	if (err >= 0) {
		qc71_features.kbd_backlight_white = !qc71_features.kbd_backlight_rgb &&
						    err == PROJ_ID_SLIMBOOK_EXECUTIVE_14;
	} else {
		pr_warn("failed to query project id: %d\n", err);
	}

	return 0;
}

static int __init check_features_bios(void)
{
	const char *bios_version_str;
	int bios_version;

	if (!dmi_check_system(qc71_dmi_table)) {
		pr_warn("no DMI match\n");
		return -ENODEV;
	}

	bios_version_str = dmi_get_system_info(DMI_BIOS_VERSION);

	if (!bios_version_str) {
		pr_warn("failed to get BIOS version DMI string\n");
		return -ENOENT;
	}

	pr_info("BIOS version string: '%s'\n", bios_version_str);

	bios_version = parse_bios_version(bios_version_str);

	if (bios_version < 0) {
		pr_warn("cannot parse BIOS version\n");
		return -EINVAL;
	}

	pr_info("BIOS version: %04d\n", bios_version);

	if (bios_version >= 114) {
		const char *s = read_oem_string(18);
		size_t s_len;

		if (IS_ERR(s))
			return PTR_ERR(s);

		s_len = strlen(s);

		pr_info("OEM_STRING(18) = '%s'\n", s);

		/* if it is entirely spaces */
		if (strspn(s, " ") == s_len) {
			qc71_features.fn_lock           = true;
			qc71_features.fn_lock_switch    = true;
			qc71_features.batt_charge_limit = true;
			qc71_features.fan_extras        = true;
		} else if (s_len > 0) {
			/* TODO */
			pr_warn("cannot extract supported features");
		}

		kfree(s);
	}

	return 0;
}

int __init qc71_check_features(void)
{
	const struct qc71_model *model = qc71_model_info;

	(void) check_features_ec();

	/*
	 * A model from models.c brings its own feature set; the DMI/OEM string
	 * detection below is only for unknown models loaded with force=1.
	 */
	if (!model)
		(void) check_features_bios();

	/*
	 * Uniwill PH4TUX1 (Monster Huma H4, TUXEDO IBP 14 Gen6): Fn lock at 0x074e
	 * bit 4. Without a model match (force=1) it is still recognised by its
	 * project id, which is only useful read-only or with allow_writes=1.
	 */
	if (model ? model->ph4tux1 : ec_read_byte(PROJ_ID_ADDR) == 0x13) {
		qc71_features.ph4tux1 = true;
		qc71_features.fn_lock = true;
	}

	/*
	 * The project ids above are Slimbook ones; elsewhere the LED has to come
	 * from the model table or be asked for with kbd_white=1.
	 */
	if (!is_slimbook)
		qc71_features.kbd_backlight_white = false;

	if (model) {
		if (model->kbd_white) {
			qc71_features.kbd_backlight_rgb = false;
			qc71_features.kbd_backlight_white = true;
		}
		qc71_features.kbd_white_max = model->kbd_white_max;
		if (model->no_battery)
			qc71_features.batt_charge_limit = false;
		if (model->no_lightbar)
			qc71_features.lightbar = false;
	}

	return 0;
}
