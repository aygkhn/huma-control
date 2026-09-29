// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#include "pr.h"

#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "fan.h"
#include "features.h"
#include "hwmon_pwm.h"
#include "models.h"
#include "pdev.h"

/* ========================================================================== */

static struct device *qc71_hwmon_pwm_dev;

/* ========================================================================== */

static umode_t qc71_hwmon_pwm_is_visible(const void *data, enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	umode_t rw = qc71_writes_allowed() ? 0644 : 0444;

	if (type != hwmon_pwm)
		return 0;
	/* PH4TUX1: raw fan speed (0x1804/0x1809) can stop the fan; mode selection only */
	if (qc71_features.ph4tux1)
		return (attr == hwmon_pwm_enable && channel == 0) ? rw : 0;
	return rw;
}

static int qc71_hwmon_pwm_read(struct device *device, enum hwmon_sensor_types type,
			       u32 attr, int channel, long *value)
{
	int err;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			err = qc71_fan_get_mode();
			if (err < 0)
				return err;

			*value = err;
			break;
		case hwmon_pwm_input:
			err = qc71_fan_get_pwm(channel);
			if (err < 0)
				return err;

			*value = err;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qc71_hwmon_pwm_write(struct device *device, enum hwmon_sensor_types type,
			    u32 attr, int channel, long value)
{
	if (!qc71_writes_allowed())
		return -EPERM;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			return qc71_fan_set_mode(value);
		case hwmon_pwm_input:
			return qc71_fan_set_pwm(channel, value);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_channel_info *qc71_hwmon_pwm_ch_info[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_ENABLE),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT, HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_ops qc71_hwmon_pwm_ops = {
	.is_visible  = qc71_hwmon_pwm_is_visible,
	.read        = qc71_hwmon_pwm_read,
	.write       = qc71_hwmon_pwm_write,
};

static const struct hwmon_chip_info qc71_hwmon_pwm_chip_info = {
	.ops  = &qc71_hwmon_pwm_ops,
	.info =  qc71_hwmon_pwm_ch_info,
};

/* ========================================================================== */

int __init qc71_hwmon_pwm_setup(void)
{
	if (!qc71_features.fan_boost)
		return -ENODEV;

	qc71_hwmon_pwm_dev = hwmon_device_register_with_info(
		&qc71_platform_dev->dev, KBUILD_MODNAME ".hwmon.pwm", NULL,
		&qc71_hwmon_pwm_chip_info, NULL);

	if (IS_ERR(qc71_hwmon_pwm_dev))
		return PTR_ERR(qc71_hwmon_pwm_dev);

	return 0;
}

void qc71_hwmon_pwm_cleanup(void)
{
	if (!IS_ERR_OR_NULL(qc71_hwmon_pwm_dev))
		hwmon_device_unregister(qc71_hwmon_pwm_dev);
}
