/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com> */
#ifndef QC71_UNIWILL_EXT_H
#define QC71_UNIWILL_EXT_H

#include <linux/init.h>
#include <linux/types.h>

int __init qc71_uniwill_ext_setup(void);
void qc71_uniwill_ext_cleanup(void);
void qc71_uniwill_ext_restore(void);
void qc71_uniwill_ext_fn_lock_changed(int state);
void qc71_uniwill_ext_fan_boost_changed(void);
void qc71_uniwill_ext_mode_key(void);
int qc71_uniwill_ext_set_fan_boost(bool on);

#endif /* QC71_UNIWILL_EXT_H */
