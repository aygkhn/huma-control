// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
/*
 * Per-model table and write policy.
 *
 * The EC interface is shared by many Uniwill/Tongfang barebones, but the same
 * register can mean different things on different models and a wrong write can
 * leave a machine with a stopped fan or wrong power limits. So:
 *
 *  - a model in qc71_models[] whose EC project id matches: full support;
 *    power limit and fan table writes only on a BIOS listed as verified;
 *  - anything else: the module refuses to load, unless force=1 is given, and
 *    then loads read-only (every EC write fails with -EPERM);
 *  - force=1 allow_writes=1: writes allowed (also on an unverified BIOS),
 *    the kernel is tainted.
 *
 * qc71_ec_transaction() checks the policy on every write, so a write path
 * that forgets to check it still cannot reach the EC.
 */
#include "pr.h"

#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/panic.h>
#include <linux/string.h>

#include "ec.h"
#include "models.h"

/* ========================================================================== */

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "load on a model that is not known to work, read-only (default=false)");

static bool allow_writes;
module_param(allow_writes, bool, 0444);
MODULE_PARM_DESC(allow_writes, "with force=1: allow EC writes on an unknown model or unverified BIOS; taints the kernel (default=false)");

/* ========================================================================== */

static const char *const huma_h4_v41_bios[] = {
	"N.1.15MON06",
	NULL
};

enum qc71_model_index {
	MODEL_MONSTER_HUMA_H4_V41,
};

static const struct qc71_model qc71_models[] = {
	[MODEL_MONSTER_HUMA_H4_V41] = {
		.name          = "Monster Huma H4 V4.1 (Uniwill PH4TUX1)",
		.proj_id       = 0x13,
		.ph4tux1       = true,
		/*
		 * The "single colour keyboard" bit (0x078c bit 0) sometimes reads 0
		 * after a module reload, so the LED is not detected from the EC here.
		 */
		.kbd_white     = true,
		.kbd_white_max = 2,
		.no_battery    = true,
		.no_lightbar   = true,
		.verified_bios = huma_h4_v41_bios,
	},
};

/*
 * DMI_MATCH is a substring match, so the strings are matched exactly. The
 * module alias built from this table (svn/pn/rn, file2alias has no SKU field)
 * makes udev load the module only on these machines.
 */
static const struct dmi_system_id qc71_model_dmi_table[] = {
	{
		.ident = "Monster Huma H4 V4.1",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "MONSTER"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "HUMA H4 V4.1"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "HUMA H4 V4.1"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "H4V41PH4TUX1"),
		},
		.driver_data = (void *) &qc71_models[MODEL_MONSTER_HUMA_H4_V41],
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, qc71_model_dmi_table);

/* ========================================================================== */

const struct qc71_model *qc71_model_info;

/* nothing is written before qc71_model_detect() has run */
static bool writes_ok;
static bool power_writes_ok;

bool qc71_writes_allowed(void)
{
	return READ_ONCE(writes_ok);
}

bool qc71_power_writes_allowed(void)
{
	return READ_ONCE(writes_ok) && READ_ONCE(power_writes_ok);
}

bool qc71_is_power_addr(u16 addr)
{
	switch (addr) {
	case PL1_ADDR:
	case PL2_ADDR:
	case PL4_ADDR:
	case CTRL_7_ADDR:        /* custom TDP mode */
	case CTRL_5_ADDR:        /* fan safety / separate fan bits */
	case ADDR(0x07, 0xc6):   /* fan table enable */
		return true;
	}
	/* fan table */
	return addr >= ADDR(0x0f, 0x00) && addr < ADDR(0x0f, 0x30);
}

/* ========================================================================== */

static const char *dmi_str(int field)
{
	const char *s = dmi_get_system_info(field);

	return s ? s : "";
}

static bool __init bios_verified(const struct qc71_model *model)
{
	const char *bios = dmi_get_system_info(DMI_BIOS_VERSION);
	const char *const *v;

	if (!bios || !model->verified_bios)
		return false;
	for (v = model->verified_bios; *v; v++)
		if (!strcmp(*v, bios))
			return true;
	return false;
}

static void __init taint_for_writes(const char *why)
{
	pr_warn("**********************************************************\n");
	pr_warn("allow_writes=1: EC writes enabled on %s.\n", why);
	pr_warn("Register meanings are NOT verified here; wrong values can\n");
	pr_warn("stop the fans or set unsafe power limits. Tainting kernel.\n");
	pr_warn("**********************************************************\n");
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);
}

int __init qc71_model_detect(int proj_id)
{
	const struct dmi_system_id *id = dmi_first_match(qc71_model_dmi_table);
	const struct qc71_model *model = id ? id->driver_data : NULL;

	if (model && model->proj_id != proj_id) {
		pr_info("DMI says %s, but EC project id is 0x%02x (expected 0x%02x)\n",
			model->name, proj_id, model->proj_id);
		model = NULL;
	}

	if (model) {
		qc71_model_info = model;
		pr_info("model: %s\n", model->name);

		if (bios_verified(model)) {
			power_writes_ok = true;
		} else if (force && allow_writes) {
			taint_for_writes("an unverified BIOS");
			power_writes_ok = true;
		} else {
			pr_info("BIOS '%s' not verified for this model: power limits and fan tables are read-only\n",
				dmi_str(DMI_BIOS_VERSION));
		}
		WRITE_ONCE(writes_ok, true);
		return 0;
	}

	if (!force) {
		pr_info("unsupported model: vendor '%s', product '%s', SKU '%s', board '%s', BIOS '%s', EC project id 0x%02x\n",
			dmi_str(DMI_SYS_VENDOR), dmi_str(DMI_PRODUCT_NAME),
			dmi_str(DMI_PRODUCT_SKU), dmi_str(DMI_BOARD_NAME),
			dmi_str(DMI_BIOS_VERSION), proj_id);
		pr_info("nothing was written. To get this model supported, run tools/hw-report.sh from the driver's repository and send its output with a bug report.\n");
		pr_info("to load read-only anyway: modprobe qc71_laptop force=1\n");
		return -ENODEV;
	}

	pr_warn("force=1: loading on an unsupported model (EC project id 0x%02x)\n", proj_id);

	if (allow_writes) {
		taint_for_writes("an unsupported model");
		power_writes_ok = true;
		WRITE_ONCE(writes_ok, true);
	} else {
		pr_warn("read-only: every write returns -EPERM (allow_writes=1 to override)\n");
	}

	return 0;
}
