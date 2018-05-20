/*
 * Copyright (C) Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * Based on Openwrt/Kernel
 *
 * This file is part of barebox.
 * See file CREDITS for list of people who contributed to this project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <init.h>
#include <restart.h>
#include <linux/reset-controller.h>
#include <mach/ralink_regs.h>

/* Reset Control */
#define SYSC_REG_RESET_CTRL	0x034
  
#define RSTCTL_RESET_PCI	BIT(26)
#define RSTCTL_RESET_SYSTEM	BIT(0)

static void __noreturn ralink_restart_soc(struct restart_handler *rst)
{
	rt_sysc_w32(RSTCTL_RESET_SYSTEM, SYSC_REG_RESET_CTRL);

	hang();
	/*NOTREACHED*/
}

static int ralink_assert_device(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	u32 val;

	if (id < 8)
		return -1;

	val = rt_sysc_r32(SYSC_REG_RESET_CTRL);
	val |= BIT(id);
	rt_sysc_w32(val, SYSC_REG_RESET_CTRL);

	return 0;
}

static int ralink_deassert_device(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	u32 val;

	if (id < 8)
		return -1;

	val = rt_sysc_r32(SYSC_REG_RESET_CTRL);
	val &= ~BIT(id);
	rt_sysc_w32(val, SYSC_REG_RESET_CTRL);

	return 0;
}

static int ralink_reset_device(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	ralink_assert_device(rcdev, id);
	return ralink_deassert_device(rcdev, id);
}

static struct reset_control_ops reset_ops = {
	.reset = ralink_reset_device,
	.assert = ralink_assert_device,
	.deassert = ralink_deassert_device,
};

static struct reset_controller_dev reset_dev = {
	.ops			= &reset_ops,
	.nr_resets		= 32,
	.of_reset_n_cells	= 1,
};

static int ralink_rst_init(void)
{
	restart_handler_register_fn(ralink_restart_soc);

	reset_dev.of_node = of_find_compatible_node(NULL, NULL,
						"ralink,rt2880-reset");
	if (!reset_dev.of_node)
		pr_err("Failed to find reset controller node");
	else
		reset_controller_register(&reset_dev);

	return 0;
}

coredevice_initcall(ralink_rst_init);
