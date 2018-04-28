/*
 * AR71xx Reset Controller Driver
 * Author: Alban Bedel
 *
 * Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/err.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct ath79_reset {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	spinlock_t lock;
};

#define FULL_CHIP_RESET 24

static int ath79_reset_update(struct reset_controller_dev *rcdev,
			unsigned long id, bool assert)
{
	struct ath79_reset *ath79_reset =
		container_of(rcdev, struct ath79_reset, rcdev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ath79_reset->lock, flags);
	val = __raw_readl(ath79_reset->base);
	if (assert)
		val |= BIT(id);
	else
		val &= ~BIT(id);
	__raw_writel(val, ath79_reset->base);
	spin_unlock_irqrestore(&ath79_reset->lock, flags);

	return 0;
}

static int ath79_reset_assert(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	return ath79_reset_update(rcdev, id, true);
}

static int ath79_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return ath79_reset_update(rcdev, id, false);
}

static struct reset_control_ops ath79_reset_ops = {
	.assert = ath79_reset_assert,
	.deassert = ath79_reset_deassert,
};

static int ath79_reset_probe(struct device_d *dev)
{
	struct device_node *np = dev->device_node;
	struct ath79_reset *ath79_reset;

	ath79_reset = xzalloc(sizeof(*ath79_reset));
	if (!ath79_reset)
		return -ENOMEM;

	ath79_reset->base = dev_request_mem_region(dev, 0);
	if (IS_ERR(ath79_reset->base))
		return PTR_ERR(ath79_reset->base);

	spin_lock_init(&ath79_reset->lock);
	ath79_reset->rcdev.ops = &ath79_reset_ops;
	ath79_reset->rcdev.of_node = np;
	ath79_reset->rcdev.of_reset_n_cells = 1;
	ath79_reset->rcdev.nr_resets = 32;

	return reset_controller_register(&ath79_reset->rcdev);
}

static const struct of_device_id ath79_reset_dt_ids[] = {
	{ .compatible = "qca,ar7100-reset", },
	{ },
};

static struct driver_d ath79_reset_driver = {
	.name	= "ath79-resetc",
	.probe		= ath79_reset_probe,
	.of_compatible = DRV_OF_COMPAT(ath79_reset_dt_ids),
};

static int ath79_reset_init(void)
{
	return platform_driver_register(&ath79_reset_driver);
}

postcore_initcall(ath79_reset_init);