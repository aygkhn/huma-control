/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com> */
/*
 * Fan tables Monster Control Center 4.8.47.13 uses on PH4TUX1, in EC layout
 * (verified identical to the EC contents while the vendor application runs):
 *   0x0f00+i = CPU[i+1].UpT (0x0f0f = 0xff), 0x0f11+i = CPU[i].DownT,
 *   0x0f20+i = CPU[i].Duty * 2. Generated, order = performance_profile index:
 *   balanced low/medium/high = M1T1-3, quiet 20/30/40 dB = M2T1-3.
 */
#ifndef QC71_FAN_TABLES_H
#define QC71_FAN_TABLES_H

#include <linux/types.h>

#define FAN_TABLE_LEN 48

static const u8 qc71_fan_tables[6][FAN_TABLE_LEN] = {
	/* M1T1: PL1 30 W (30 on battery), PL2 DEFAULT, max duty 60% */
	{ 54, 60, 65, 70, 73, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 45, 57, 64, 66, 72, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 70, 100, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120 },
	/* M1T2: PL1 32 W (32 on battery), PL2 DEFAULT, max duty 85% */
	{ 54, 60, 65, 70, 73, 75, 77, 79, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 45, 57, 64, 66, 72, 74, 76, 78, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 70, 100, 120, 140, 160, 170, 170, 170, 170, 170, 170, 170, 170 },
	/* M1T3: PL1 38 W (35 on battery), PL2 DEFAULT, max duty 100% */
	{ 54, 60, 65, 70, 73, 75, 77, 79, 81, 255, 255, 255, 255, 255, 255, 255,
	  0, 45, 57, 64, 66, 72, 74, 76, 78, 80, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 70, 100, 120, 140, 160, 170, 200, 200, 200, 200, 200, 200, 200 },
	/* M2T1: PL1 15 W (15 on battery), PL2 35, max duty 30%. Stop threshold 45 -> 50 °C (Control
	 * Center: 45): during video playback the package stays at 45-49 °C at ~4 W and the
	 * fan never stopped at 25% */
	{ 54, 60, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 57, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60 },
	/* M2T2: PL1 25 W (25 on battery), PL2 35, max duty 60% */
	{ 54, 60, 65, 70, 73, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 45, 57, 64, 66, 72, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 70, 100, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120 },
	/* M2T3: PL1 35 W (35 on battery), PL2 35, max duty 85% */
	{ 54, 60, 65, 70, 73, 75, 77, 79, 255, 255, 255, 255, 255, 255, 255, 255,
	  0, 45, 57, 64, 66, 72, 74, 76, 78, 255, 255, 255, 255, 255, 255, 255,
	  0, 50, 60, 70, 100, 120, 140, 160, 170, 170, 170, 170, 170, 170, 170, 170 },
};

#endif /* QC71_FAN_TABLES_H */
