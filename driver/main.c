// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
/* ========================================================================== */
/* https://www.intel.com/content/dam/support/us/en/documents/laptops/whitebook/QC71_PROD_SPEC.pdf
 *
 *
 * based on the following resources:
 *  - https://lwn.net/Articles/391230/
 *  - http://blog.nietrzeba.pl/2011/12/mof-decompilation.html
 *  - https://github.com/tuxedocomputers/tuxedo-cc-wmi/
 *  - https://github.com/tuxedocomputers/tuxedo-keyboard/
 *  - Control Center for Microsoft Windows
 *  - http://forum.notebookreview.com/threads/tongfang-gk7cn6s-gk7cp0s-gk7cp7s.825461/page-54
 */
/* ========================================================================== */
#include "pr.h"

#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/kconfig.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include "ec.h"
#include "features.h"
#include "models.h"
#include "wmi.h"
#include "util.h"

/* submodules */
#include "pdev.h"
#include "events.h"
#include "hwmon.h"
#include "battery.h"
#include "led_lightbar.h"
#include "led_keyboard.h"
#include "uniwill_ext.h"
#include "debugfs.h"

/* ========================================================================== */

#define SUBMODULE_ENTRY(_name, _req) { .name = #_name, .init = qc71_ ## _name ## _setup, .cleanup = qc71_ ## _name ## _cleanup, .required = _req }

static struct qc71_submodule {
	const char *name;

	bool required    : 1,
	     initialized : 1;

	int (*init)(void);
	void (*cleanup)(void);
} qc71_submodules[] __refdata = {
	SUBMODULE_ENTRY(pdev, true), /* must be first */
	SUBMODULE_ENTRY(wmi_events, false),
	SUBMODULE_ENTRY(hwmon, false),
	SUBMODULE_ENTRY(battery, false),
	SUBMODULE_ENTRY(led_lightbar, false),
	SUBMODULE_ENTRY(led_keyboard, false),
	SUBMODULE_ENTRY(uniwill_ext, false),
	SUBMODULE_ENTRY(debugfs, false),
};

#undef SUBMODULE_ENTRY

static void do_cleanup(void)
{
	int i;

	/*
	 * WMI event handlers call into the LED, hwmon and uniwill_ext
	 * submodules: stop the events before tearing those down.
	 */
	for (i = ARRAY_SIZE(qc71_submodules) - 1; i >= 0; i--) {
		struct qc71_submodule *sm = &qc71_submodules[i];

		if (sm->initialized && !strcmp(sm->name, "wmi_events")) {
			sm->cleanup();
			sm->initialized = false;
		}
	}

	for (i = ARRAY_SIZE(qc71_submodules) - 1; i >= 0; i--) {
		struct qc71_submodule *sm = &qc71_submodules[i];

		if (sm->initialized)
			sm->cleanup();
		sm->initialized = false;
	}
}

static int __init qc71_laptop_module_init(void)
{
	int err = 0, i, proj_id;

	if (!wmi_has_guid(QC71_WMI_WMBC_GUID)) {
		pr_err("WMI GUID not found\n");
		err = -ENODEV; goto out;
	}

	err = ec_read_byte(PROJ_ID_ADDR);
	if (err < 0) {
		pr_err("failed to query project id: %d\n", err);
		goto out;
	}

	pr_info("project id: %d\n", err);
	proj_id = err;

	err = ec_read_byte(PLATFORM_ID_ADDR);
	if (err < 0) {
		pr_err("failed to query platform id: %d\n", err);
		goto out;
	}

	pr_info("platform id: %d\n", err);

	/* refuses unknown models unless force=1; decides what may be written */
	err = qc71_model_detect(proj_id);
	if (err)
		goto out;

	err = qc71_check_features();
	if (err) {
		pr_err("cannot check system features: %d\n", err);
		goto out;
	}

	pr_info("supported features:");
	if (qc71_features.super_key_lock)    pr_cont(" super-key-lock");
	if (qc71_features.lightbar)          pr_cont(" lightbar");
	if (qc71_features.fan_boost)         pr_cont(" fan-boost");
	if (qc71_features.fn_lock)           pr_cont(" fn-lock");
	if (qc71_features.batt_charge_limit) pr_cont(" charge-limit");
	if (qc71_features.fan_extras)        pr_cont(" fan-extras");
	if (qc71_features.silent_mode)       pr_cont(" silent-mode");
	if (qc71_features.turbo_mode)        pr_cont(" turbo-mode");
	if (qc71_features.kbd_backlight_rgb) pr_cont(" kbd-backlight-rgb");
	if (qc71_features.kbd_backlight_white) pr_cont(" kbd-backlight-white");

	pr_cont("\n");

	if (!qc71_writes_allowed())
		pr_info("read-only mode: nothing will be written to the EC\n");

	for (i = 0; i < ARRAY_SIZE(qc71_submodules); i++) {
		struct qc71_submodule *sm = &qc71_submodules[i];

		err = sm->init();
		if (err) {
			pr_warn("failed to initialize %s submodule: %d\n", sm->name, err);
			if (sm->required)
				goto out;
		} else {
			sm->initialized = true;
		}
	}
	
	/* local change: no EC writes at load time (the RGB manual mode block was removed) */

	err = 0;

out:
	if (err)
		do_cleanup();
	else
		pr_info("module loaded\n");

	return err;
}

static void __exit qc71_laptop_module_cleanup(void)
{
	do_cleanup();
	pr_info("module unloaded\n");
}

/* ========================================================================== */

module_init(qc71_laptop_module_init);
module_exit(qc71_laptop_module_cleanup);

MODULE_LICENSE("GPL");
MODULE_VERSION(QC71_VERSION);
MODULE_AUTHOR("Barnabás Pőcze <pobrn@protonmail.com>");
MODULE_AUTHOR("Gokhan AY <aygkhn@gmail.com>");
MODULE_DESCRIPTION("QC71 laptop platform driver");
/*
 * No "wmi:" alias: the WMI GUID is on every Uniwill barebone, and the module
 * should only load by itself on the models in models.c (MODULE_DEVICE_TABLE
 * there). The GUID is still checked at init.
 */
