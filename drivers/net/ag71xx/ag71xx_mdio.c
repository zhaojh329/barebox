/*
 *  Atheros AR71xx built-in ethernet mac driver
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Based on Atheros' AG7100 driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include "ag71xx.h"


#define AG71XX_MDIO_RETRY	1000
#define AG71XX_MDIO_DELAY	5

static int ag71xx_mdio_wait_busy(struct ag71xx *ag)
{
	int i;

	for (i = 0; i < AG71XX_MDIO_RETRY; i++) {
		u32 busy;

		udelay(AG71XX_MDIO_DELAY);

		busy = ag71xx_rr(ag, AG71XX_REG_MII_IND);
		if (!busy)
			return 0;

		udelay(AG71XX_MDIO_DELAY);
	}

	dev_err(&ag->miibus.dev, "MDIO operation timed out\n");

	return -ETIMEDOUT;
}

int ag71xx_mdio_mii_read(struct mii_bus *bus, int addr, int reg)
{
	struct ag71xx *ag = bus->priv;
	int err;
	int ret;

	err = ag71xx_mdio_wait_busy(ag);
	if (err)
		return 0xffff;

	ag71xx_wr(ag, AG71XX_REG_MII_CMD, MII_CMD_WRITE);
	ag71xx_wr(ag, AG71XX_REG_MII_ADDR,
			((addr & 0xff) << MII_ADDR_SHIFT) | (reg & 0xff));
	ag71xx_wr(ag, AG71XX_REG_MII_CMD, MII_CMD_READ);

	err = ag71xx_mdio_wait_busy(ag);
	if (err)
		return 0xffff;

	ret = ag71xx_rr(ag, AG71XX_REG_MII_STATUS) & 0xffff;
	ag71xx_wr(ag, AG71XX_REG_MII_CMD, MII_CMD_WRITE);

	return ret;
}

int ag71xx_mdio_mii_write(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct ag71xx *ag = bus->priv;

	ag71xx_wr(ag, AG71XX_REG_MII_ADDR,
			((addr & 0xff) << MII_ADDR_SHIFT) | (reg & 0xff));
	ag71xx_wr(ag, AG71XX_REG_MII_CTRL, val);

	ag71xx_mdio_wait_busy(ag);

	return 0;
}

static int ag71xx_mdio_reset(struct mii_bus *bus)
{
	struct device_node *np = bus->dev.device_node;
	struct ag71xx *ag = bus->priv;
	bool builtin_switch;
	u32 t;

	builtin_switch = of_property_read_bool(np, "builtin-switch");

	if (builtin_switch)
		t = MII_CFG_CLK_DIV_10;
	else
		t = MII_CFG_CLK_DIV_28;

	ag71xx_wr(ag, AG71XX_REG_MII_CFG, t | MII_CFG_RESET);
	udelay(100);

	ag71xx_wr(ag, AG71XX_REG_MII_CFG, t);
	udelay(100);

	return 0;
}

int ag71xx_mdio_init(struct ag71xx *ag)
{
	bool builtin_switch;
	struct device_node *np;
	struct device_d *parent = ag->dev;
	struct mii_bus *miibus = &ag->miibus;;

	np = of_get_child_by_name(parent->device_node, "mdio-bus");
	if (!np)
		return -ENODEV;

	if (!of_device_is_available(np))
		return 0;

	builtin_switch = of_property_read_bool(np, "builtin-switch");

	if (builtin_switch) {
		miibus->read = ar7240sw_phy_read;
		miibus->write = ar7240sw_phy_write;
	} else {
		miibus->read = ag71xx_mdio_mii_read;
		miibus->write = ag71xx_mdio_mii_write;
	}
	miibus->reset = ag71xx_mdio_reset;
	miibus->priv = ag;
	miibus->parent = parent;

	if (!builtin_switch &&
	    of_property_read_u32(np, "phy-mask", &miibus->phy_mask))
		miibus->phy_mask = 0;

	mdiobus_register(miibus);

	if (builtin_switch)
		ag71xx_ar7240_init(ag, np);

	return 0;
}