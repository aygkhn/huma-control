// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#ifndef QC71_LED_KEYBOARD_H
#define QC71_LED_KEYBOARD_H

#if IS_ENABLED(CONFIG_LEDS_CLASS)

#include <linux/init.h>

int  __init qc71_led_keyboard_setup(void);
void        qc71_led_keyboard_cleanup(void);
void        qc71_white_led_hw_changed(void);
void        qc71_led_keyboard_hw_changed(int level);

#else

static inline int qc71_led_keyboard_setup(void)
{
    return 0;
}

static inline void qc71_led_keyboard_cleanup(void)
{

}

static inline void qc71_white_led_hw_changed(void)
{
}

static inline void qc71_led_keyboard_hw_changed(int level)
{
}

#endif

#endif /* QC71_LED_KEYBOARD_H */
