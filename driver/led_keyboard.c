// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#include "pr.h"

#include <linux/init.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/fixp-arith.h>
#include <linux/bitfield.h>
#include <linux/suspend.h>

#include "util.h"
#include "ec.h"
#include "features.h"
#include "models.h"
#include "led_keyboard.h"
#include "pdev.h"

static bool keyboard_led_registered;
static bool keyboard_white_led_registered;

/* -1: detect from the EC, 0: never register, 1: always register */
static int kbd_white = -1;
module_param(kbd_white, int, 0444);
MODULE_PARM_DESC(kbd_white, "white keyboard backlight: -1 known models (default), 0 off, 1 force; ignored on RGB keyboards");

static uint kbd_white_max;
module_param(kbd_white_max, uint, 0444);
MODULE_PARM_DESC(kbd_white_max, "highest white keyboard backlight level (default 0: from the model table, 2 on PH4TUX1, else 4)");

static enum led_brightness qc71_keyboard_led_get_brightness(struct led_classdev *led_cdev)
{
    return led_cdev->brightness;
}

static int qc71_keyboard_led_set_brightness(struct led_classdev *led_cdev,
                                            enum led_brightness value)
{
    struct led_classdev_mc *mcled_cdev = lcdev_to_mccdev(led_cdev);

    if (!qc71_writes_allowed())
        return -EPERM;

    int red = mcled_cdev->subled_info[0].intensity ;
    int green = mcled_cdev->subled_info[1].intensity ;
    int blue =  mcled_cdev->subled_info[2].intensity ;

    ec_write_byte(KBD_BACKLIGHT_RGB_RED_SETUP_ADDR,fixp_linear_interpolate(0, 0, U8_MAX, 0x32, red) );
    ec_write_byte(KBD_BACKLIGHT_RGB_GREEN_SETUP_ADDR,fixp_linear_interpolate(0, 0, U8_MAX, 0x32, green) );
    ec_write_byte(KBD_BACKLIGHT_RGB_BLUE_SETUP_ADDR,fixp_linear_interpolate(0, 0, U8_MAX, 0x32, blue) );


    int err = qc71_ec_update_bits(TRIGGER_1_ADDR, 0x20, 0x20);

    if (!err)
        err = qc71_ec_update_bits(CTRL_2_ADDR, 0xf0, (value << 5) | CTRL_2_COLOR_KBD_TRIGGER);
    if (err)
        return err;

    led_cdev->brightness = value;

    return 0;
}

/* ========================================================================== */


static struct mc_subled qc71_subleds[3] = {
    {
        .color_index = LED_COLOR_ID_RED,
        .intensity = 0xff,
        .channel = 0
    },
    {
        .color_index = LED_COLOR_ID_GREEN,
        .intensity = 0xff,
        .channel = 0
    },
    {
        .color_index = LED_COLOR_ID_BLUE,
        .intensity = 0xff,
        .channel = 0
    }
};

static struct led_classdev_mc qc71_keyboard_led = {
    .led_cdev.name                    = "rgb:"LED_FUNCTION_KBD_BACKLIGHT,
    .led_cdev.max_brightness          = 4,
    .led_cdev.brightness_get          = qc71_keyboard_led_get_brightness,
    .led_cdev.brightness_set_blocking = qc71_keyboard_led_set_brightness,
    .led_cdev.flags                   = LED_BRIGHT_HW_CHANGED,
    .num_colors                       = 3,
    .subled_info                      = qc71_subleds
};

/* ========================================================================== */

/*
 * White keyboards keep the brightness in bits 7:5 of the status register.
 * The EC changes it itself only on the Fn backlight key (reported as an event)
 * and possibly across suspend, so it is read back then; ordinary reads return
 * the cached level: every EC read is a slow WMI call (~60 ms, dozens of EC
 * interrupts) and desktop services read the level often.
 */
static enum led_brightness qc71_white_led_read_hw(struct led_classdev *led_cdev)
{
    int data = ec_read_byte(CTRL_2_ADDR);

    if (data < 0)
        return led_cdev->brightness;

    /* bit 1 = backlight off (Control Center's power switch), the level is kept */
    if (data & CTRL_2_SINGLE_COLOR_KBD_BL_OFF)
        return 0;

    /* the Fn key may cycle past a kbd_white_max set too low */
    return min_t(int, FIELD_GET(CTRL_2_SINGLE_COLOR_KBD_BRIGHTNESS, data),
                 led_cdev->max_brightness);
}

static enum led_brightness qc71_white_led_get_brightness(struct led_classdev *led_cdev)
{
    return led_cdev->brightness;
}

static int qc71_white_led_set_brightness(struct led_classdev *led_cdev,
                                         enum led_brightness value)
{
    int data, err;

    if (!qc71_writes_allowed())
        return -EPERM;

    /*
     * Some white keyboards ignore a new level while the backlight is off
     * until the immediate brightness register is touched (TUXEDO's
     * uniwill_write_kbd_bl_brightness_white_workaround).
     */
    if (value) {
        data = ec_read_byte(KBD_BACKLIGHT_RGB_BLUE_ADDR);
        if (data == 0 && ec_write_byte(KBD_BACKLIGHT_RGB_BLUE_ADDR, 0x01))
            pr_debug("failed to wake the immediate brightness register\n");
    }

    /*
     * Keep the low nibble, set the level and the apply bit. Turning on also
     * clears the off bit, which Control Center on Windows may have left set.
     */
    err = qc71_ec_update_bits(CTRL_2_ADDR, value ? 0xf0 | CTRL_2_SINGLE_COLOR_KBD_BL_OFF : 0xf0,
                              FIELD_PREP(CTRL_2_SINGLE_COLOR_KBD_BRIGHTNESS, value) |
                              CTRL_2_COLOR_KBD_TRIGGER);
    if (err)
        return err;

    led_cdev->brightness = value;

    return 0;
}

static struct led_classdev qc71_white_keyboard_led = {
    .name                    = "white:" LED_FUNCTION_KBD_BACKLIGHT,
    .brightness_get          = qc71_white_led_get_brightness,
    .brightness_set_blocking = qc71_white_led_set_brightness,
    /*
     * keep the level when the module unloads (the LED core would switch it
     * off); report Fn+F6 changes so the desktop can show them
     */
    .flags                   = LED_RETAIN_AT_SHUTDOWN | LED_BRIGHT_HW_CHANGED,
};

/* The EC changed the white backlight itself (Fn+F6): refresh the cached level */
void qc71_white_led_hw_changed(void)
{
    if (!keyboard_white_led_registered)
        return;
    qc71_white_keyboard_led.brightness = qc71_white_led_read_hw(&qc71_white_keyboard_led);
#if IS_ENABLED(CONFIG_LEDS_BRIGHTNESS_HW_CHANGED)
    led_classdev_notify_brightness_hw_changed(&qc71_white_keyboard_led,
                                              qc71_white_keyboard_led.brightness);
#endif
}

/* The EC may change the level across suspend: read it back once on resume */
static int qc71_white_led_pm_notify(struct notifier_block *nb, unsigned long action, void *data)
{
    switch (action) {
    case PM_POST_SUSPEND:
    case PM_POST_HIBERNATION:
    case PM_POST_RESTORE:
        qc71_white_led_hw_changed();
        break;
    }
    return NOTIFY_DONE;
}

static struct notifier_block qc71_white_led_pm_nb = { .notifier_call = qc71_white_led_pm_notify };
static bool qc71_white_led_pm_registered;

/*
 * Fn backlight events: report the new level on our own LED (the white one
 * reads it back from the EC; level -1 = unknown, keep the cached RGB level)
 */
void qc71_led_keyboard_hw_changed(int level)
{
#if IS_ENABLED(CONFIG_LEDS_BRIGHTNESS_HW_CHANGED)
    struct led_classdev *cdev = &qc71_keyboard_led.led_cdev;

    if (keyboard_white_led_registered) {
        qc71_white_led_hw_changed();
        return;
    }
    if (!keyboard_led_registered)
        return;
    if (level >= 0 && level <= cdev->max_brightness)
        cdev->brightness = level;
    led_classdev_notify_brightness_hw_changed(cdev, cdev->brightness);
#endif
}

static int __init qc71_white_led_keyboard_setup(void)
{
    int err;

    int data;
    bool detected = qc71_features.kbd_backlight_white;

    /*
     * PH4TUX1: 0x078c bit 0 = single colour keyboard present, 3 levels; a model
     * from models.c that lists a white keyboard is not asked
     */
    if (qc71_features.ph4tux1 && !qc71_model_info) {
        data = ec_read_byte(CTRL_2_ADDR);
        detected = data >= 0 && (data & CTRL_2_SINGLE_COLOR_KEYBOARD);
    }

    if (kbd_white == 0 || (kbd_white < 0 && !detected))
        return -ENODEV;

    qc71_white_keyboard_led.max_brightness =
        clamp_t(uint, kbd_white_max ?: qc71_features.kbd_white_max ?:
                (qc71_features.ph4tux1 ? 2 : 4), 1, 7);

    qc71_white_keyboard_led.brightness = qc71_white_led_read_hw(&qc71_white_keyboard_led);
    err = devm_led_classdev_register(&qc71_platform_dev->dev, &qc71_white_keyboard_led);

    if (!err) {
        keyboard_white_led_registered = true;
        qc71_white_led_pm_registered = !register_pm_notifier(&qc71_white_led_pm_nb);
    }

    return err;
}

/* ========================================================================== */

int __init qc71_led_keyboard_setup(void)
{
    int err;

    if (!qc71_features.kbd_backlight_rgb)
        return qc71_white_led_keyboard_setup();

    err = devm_led_classdev_multicolor_register_ext(&qc71_platform_dev->dev, &qc71_keyboard_led, NULL);

    if (!err)
        keyboard_led_registered = true;

    return err;
}

void qc71_led_keyboard_cleanup(void)
{
    if (qc71_white_led_pm_registered) {
        unregister_pm_notifier(&qc71_white_led_pm_nb);
        qc71_white_led_pm_registered = false;
    }
    if (keyboard_white_led_registered) {
        devm_led_classdev_unregister(&qc71_platform_dev->dev, &qc71_white_keyboard_led);
        keyboard_white_led_registered = false;
    }

    if (keyboard_led_registered) {
        keyboard_led_registered = false;
        devm_led_classdev_multicolor_unregister(&qc71_platform_dev->dev, &qc71_keyboard_led);
    }
}
