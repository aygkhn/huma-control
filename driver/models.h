// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#ifndef QC71_MODELS_H
#define QC71_MODELS_H

#include <linux/init.h>
#include <linux/types.h>

/*
 * Models this driver has been verified on. Anything else is refused unless
 * force=1 is given, and then loads read-only (see models.c).
 */
struct qc71_model {
	const char *name;
	int proj_id;                  /* expected EC project id (0x0740) */
	bool ph4tux1     : 1;         /* Uniwill PH4TUX1 extensions (uniwill_ext.c) */
	bool kbd_white   : 1;         /* single colour keyboard backlight */
	bool no_battery  : 1;         /* no EC battery charge limit (battery.c) */
	bool no_lightbar : 1;         /* no lightbar, whatever the EC reports */
	u8 kbd_white_max;             /* highest white backlight level, 0 = default */
	/* BIOS versions power limit and fan table writes were verified on */
	const char *const *verified_bios;
};

/* the matched model, NULL if none (only possible with force=1) */
extern const struct qc71_model *qc71_model_info;

int __init qc71_model_detect(int proj_id);

/* any EC write; false until qc71_model_detect() decided otherwise */
bool qc71_writes_allowed(void);

/* PL1/PL2 and fan table writes: also needs a verified BIOS */
bool qc71_power_writes_allowed(void);

/* EC addresses that only qc71_power_writes_allowed() may write */
bool qc71_is_power_addr(u16 addr);

#endif /* QC71_MODELS_H */
