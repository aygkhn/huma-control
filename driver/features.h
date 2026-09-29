// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#ifndef QC71_FEATURES_H
#define QC71_FEATURES_H

#include <linux/init.h>
#include <linux/types.h>

struct qc71_features_struct {
	bool super_key_lock    : 1;
	bool lightbar          : 1;
	bool fan_boost         : 1;
	bool fn_lock           : 1;
	bool batt_charge_limit : 1;
	bool fan_extras        : 1; /* duty cycle reduction, always on mode */
	bool silent_mode       : 1; /* Slimbook silent mode: decreases fan rpm limit and tdp */
	bool turbo_mode        : 1; /* Slimbook turbo mode */
	bool kbd_backlight_rgb : 1;
	bool kbd_backlight_white : 1; /* single colour, brightness only */
	bool fn_lock_switch    : 1; /* 0x07a4 bit 3: only via the OEM string / Slimbook path */
	bool ph4tux1           : 1; /* Uniwill PH4TUX1 (Monster Huma H4, TUXEDO IBP 14 Gen6) */
	u8   kbd_white_max;         /* highest white backlight level, 0 = driver default */
};

/* ========================================================================== */

extern struct qc71_features_struct qc71_features;
extern uint32_t qc71_model;

/* ========================================================================== */

int __init qc71_check_features(void);

#endif /* QC71_FEATURES_H */
