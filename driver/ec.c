// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Gokhan AY <aygkhn@gmail.com>
#include "pr.h"

#include <linux/acpi.h>
#include <linux/compiler_types.h>
#include <linux/error-injection.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/printk.h>
#include <linux/wmi.h>

#include "ec.h"
#include "models.h"
#include "wmi.h"

/* ========================================================================== */

static DECLARE_RWSEM(ec_lock);

/* ========================================================================== */

int __must_check qc71_ec_transaction(uint16_t addr, uint16_t data,
				     union qc71_ec_result *result, bool read)
{
	uint8_t buf[] = {
		addr & 0xFF,
		addr >> 8,
		data & 0xFF,
		data >> 8,
		0,
		read ? 1 : 0,
		0,
		0,
	};
	static_assert(ARRAY_SIZE(buf) == 8);

	/* the returned ACPI_TYPE_BUFFER is 40 bytes long for some reason ... */
	__aligned(__alignof__(union acpi_object)) uint8_t output_buf[sizeof(union acpi_object) + 40] = {0};

	struct acpi_buffer input = { sizeof(buf), buf },
			   output = { sizeof(output_buf), output_buf };
	union acpi_object *obj = NULL;
	acpi_status status = AE_OK;
	int err;

	/*
	 * Last line of defense: every write goes through here, so the model
	 * policy (models.c) holds even for a write path that does not check it.
	 */
	if (!read && !qc71_writes_allowed()) {
		pr_warn_once("EC write blocked (read-only mode): %#06x\n", (unsigned int) addr);
		return -EPERM;
	}
	if (!read && qc71_is_power_addr(addr) && !qc71_power_writes_allowed()) {
		pr_warn_once("EC write blocked (BIOS not verified for power limits and fan tables): %#06x\n",
			     (unsigned int) addr);
		return -EPERM;
	}

	if (read) err = down_read_killable(&ec_lock);
	else      err = down_write_killable(&ec_lock);

	if (err)
		goto out;

	status = wmi_evaluate_method(QC71_WMI_WMBC_GUID, 0,
				     QC71_WMBC_GETSETULONG_ID, &input, &output);

	if (read) up_read(&ec_lock);
	else      up_write(&ec_lock);

	if (ACPI_FAILURE(status)) {
		err = -EIO;
		goto out;
	}

	obj = output.pointer;

	if (result) {
		if (obj && obj->type == ACPI_TYPE_BUFFER && obj->buffer.length >= sizeof(*result)) {
			memcpy(result, obj->buffer.pointer, sizeof(*result));
		} else {
			err = -ENODATA;
			goto out;
		}
	}

out:
	pr_debug(
		"%s(addr=%#06x, data=%#06x, result=%c, read=%c)"
		": (%d) [%#010lx] %s"
		": [%*ph]\n",

		__func__, (unsigned int) addr, (unsigned int) data,
		result ? 'y' : 'n', read ? 'y' : 'n',
		err, (unsigned long) status, acpi_format_exception(status),
		(obj && obj->type == ACPI_TYPE_BUFFER) ?
			(int) min(sizeof(*result), (size_t) obj->buffer.length) : 0,
		(obj && obj->type == ACPI_TYPE_BUFFER) ?
			obj->buffer.pointer : NULL
	);

	return err;
}
ALLOW_ERROR_INJECTION(qc71_ec_transaction, ERRNO);

/* ========================================================================== */

static DEFINE_MUTEX(ec_rmw_lock);

int qc71_ec_update_bits(uint16_t addr, uint8_t mask, uint8_t val)
{
	int old, new, err;

	mutex_lock(&ec_rmw_lock);
	old = ec_read_byte(addr);
	if (old < 0) {
		err = old;
		goto out;
	}
	new = (old & ~mask) | (val & mask);
	err = new == old ? 0 : ec_write_byte(addr, new);
out:
	mutex_unlock(&ec_rmw_lock);
	return err;
}
