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

static inline struct reset_control *reset_control_get_optional(
	struct device_d *dev, const char *id)
{
	struct reset_control *reset = reset_control_get(dev, id);

	if (IS_ERR(reset))
		return NULL;
	return reset;
}

static int ag71xx_ether_set_ethaddr(struct eth_device *edev,
				    const unsigned char *adr)
{
	return 0;
}

static int ag71xx_ether_get_ethaddr(struct eth_device *edev, unsigned char *adr)
{
	/* We have no eeprom */
	return -ENODEV;
}

static void ag71xx_ether_halt(struct eth_device *edev)
{
	struct ag71xx *ag = edev->priv;
	struct device_d *dev = ag->dev;
	uint64_t start;

	ag71xx_wr(ag, AG71XX_REG_RX_CTRL, 0);
	start = get_time_ns();
	while (ag71xx_rr(ag, AG71XX_REG_RX_CTRL)) {
		if (is_timeout_non_interruptible(start, 100 * USECOND)) {
			dev_err(dev, "error: failed to stop device!\n");
			break;
		}
	}
}

static int ag71xx_ether_rx(struct eth_device *edev)
{
	struct ag71xx *ag = edev->priv;
	ag7240_desc_t *f;
	unsigned int work_done;

	for (work_done = 0; work_done < NO_OF_RX_FIFOS; work_done++) {
		unsigned int pktlen;
		unsigned char *rx_pkt;

		f = &ag->fifo_rx[ag->next_rx];

		if (f->is_empty)
			break;

		pktlen = f->pkt_size;
		rx_pkt = ag->rx_pkt[ag->next_rx];

		/* invalidate */
		dma_sync_single_for_cpu((unsigned long)rx_pkt, pktlen,
						DMA_FROM_DEVICE);

		net_receive(edev, rx_pkt, pktlen - 4);

		f->is_empty = 1;
		ag->next_rx = (ag->next_rx + 1) % NO_OF_RX_FIFOS;
	}

	if (!(ag71xx_rr(ag, AG71XX_REG_RX_CTRL) & RX_CTRL_RXE)) {
		f = &ag->fifo_rx[ag->next_rx];
		ag71xx_wr(ag, AG71XX_REG_RX_DESC, virt_to_phys(f));
		ag71xx_wr(ag, AG71XX_REG_RX_CTRL, RX_CTRL_RXE);
	}

	return work_done;
}

static int ag71xx_ether_send(struct eth_device *edev, void *packet, int length)
{
	struct ag71xx *ag = edev->priv;
	struct device_d *dev = ag->dev;
	ag7240_desc_t *f = &ag->fifo_tx[ag->next_tx];
	uint64_t start;
	int ret = 0;

	/* flush */
	dma_sync_single_for_device((unsigned long)packet, length, DMA_TO_DEVICE);

	f->pkt_start_addr = virt_to_phys(packet);
	f->res1 = 0;
	f->pkt_size = length;
	f->is_empty = 0;
	ag71xx_wr(ag, AG71XX_REG_TX_DESC, virt_to_phys(f));
	ag71xx_wr(ag, AG71XX_REG_TX_CTRL, TX_CTRL_TXE);

	/* flush again?! */
	dma_sync_single_for_cpu((unsigned long)packet, length, DMA_TO_DEVICE);

	start = get_time_ns();
	while (!f->is_empty) {
		if (!is_timeout_non_interruptible(start, 100 * USECOND))
			continue;

		dev_err(dev, "error: tx timed out\n");
		ret = -ETIMEDOUT;
		break;
	}

	f->pkt_start_addr = 0;
	f->pkt_size = 0;

	ag->next_tx = (ag->next_tx + 1) % NO_OF_TX_FIFOS;

	return ret;
}

static int ag71xx_ether_open(struct eth_device *edev)
{
	struct ag71xx *ag = edev->priv;

	ag71xx_ar7240_start(ag);

	return 0;
}

static int ag71xx_ether_init(struct eth_device *edev)
{
	struct ag71xx *ag = edev->priv;
	int i;
	void *rxbuf = ag->rx_buffer;

	ag->next_rx = 0;

	for (i = 0; i < NO_OF_RX_FIFOS; i++) {
		ag7240_desc_t *fr = &ag->fifo_rx[i];

		ag->rx_pkt[i] = rxbuf;
		fr->pkt_start_addr = virt_to_phys(rxbuf);
		fr->pkt_size = MAX_RBUFF_SZ;
		fr->is_empty = 1;
		fr->next_desc = virt_to_phys(&ag->fifo_rx[(i + 1) % NO_OF_RX_FIFOS]);

		/* invalidate */
		dma_sync_single_for_device((unsigned long)rxbuf, MAX_RBUFF_SZ,
					DMA_FROM_DEVICE);

		rxbuf += MAX_RBUFF_SZ;
	}

	/* Clean Tx BD's */
	memset(ag->fifo_tx, 0, TX_RING_SZ);

	ag71xx_wr(ag, AG71XX_REG_RX_DESC, virt_to_phys(ag->fifo_rx));
	ag71xx_wr(ag, AG71XX_REG_RX_CTRL, RX_CTRL_RXE);

	return 0;
}

#define MAC_CFG1_INIT	(MAC_CFG1_RXE | MAC_CFG1_TXE | \
			 MAC_CFG1_SRX | MAC_CFG1_STX)

#define FIFO_CFG0_INIT	(FIFO_CFG0_ALL << FIFO_CFG0_ENABLE_SHIFT)

#define FIFO_CFG4_INIT	(FIFO_CFG4_DE | FIFO_CFG4_DV | FIFO_CFG4_FC | \
			 FIFO_CFG4_CE | FIFO_CFG4_CR | FIFO_CFG4_LM | \
			 FIFO_CFG4_LO | FIFO_CFG4_OK | FIFO_CFG4_MC | \
			 FIFO_CFG4_BC | FIFO_CFG4_DR | FIFO_CFG4_LE | \
			 FIFO_CFG4_CF | FIFO_CFG4_PF | FIFO_CFG4_UO | \
			 FIFO_CFG4_VT)

#define FIFO_CFG5_INIT	(FIFO_CFG5_DE | FIFO_CFG5_DV | FIFO_CFG5_FC | \
			 FIFO_CFG5_CE | FIFO_CFG5_LO | FIFO_CFG5_OK | \
			 FIFO_CFG5_MC | FIFO_CFG5_BC | FIFO_CFG5_DR | \
			 FIFO_CFG5_CF | FIFO_CFG5_PF | FIFO_CFG5_VT | \
			 FIFO_CFG5_LE | FIFO_CFG5_FT | FIFO_CFG5_16 | \
			 FIFO_CFG5_17 | FIFO_CFG5_SF)

static void ag71xx_hw_setup(struct ag71xx *ag)
{
	u32 cfg2;

	/* setup MAC configuration registers */
	ag71xx_wr(ag, AG71XX_REG_MAC_CFG1, MAC_CFG1_INIT);

	ag71xx_sb(ag, AG71XX_REG_MAC_CFG2,
		  MAC_CFG2_PAD_CRC_EN | MAC_CFG2_LEN_CHECK);

	/* setup FIFO configuration registers */
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG0, FIFO_CFG0_INIT);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG1, 0x0010ffff);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG2, 0x015500aa);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG3, 0x01f00140);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG4, FIFO_CFG4_INIT);
	ag71xx_wr(ag, AG71XX_REG_FIFO_CFG5, FIFO_CFG5_INIT);

	cfg2 = ag71xx_rr(ag, AG71XX_REG_MAC_CFG2);
	cfg2 &= ~(MAC_CFG2_IF_1000 | MAC_CFG2_IF_10_100 | MAC_CFG2_FDX);
	cfg2 |= MAC_CFG2_FDX;
	cfg2 |= MAC_CFG2_IF_1000;

	ag71xx_wr(ag, AG71XX_REG_MAC_CFG2, cfg2);
}

static void ag71xx_hw_init(struct ag71xx *ag)
{
	if (ag->esw_reset) {
		reset_control_assert(ag->esw_reset);
		mdelay(50);
		reset_control_deassert(ag->esw_reset);
		mdelay(200);
	}

	if (ag->esw_analog_reset) {
		reset_control_assert(ag->esw_analog_reset);
		mdelay(50);
		reset_control_deassert(ag->esw_analog_reset);
		mdelay(200);
	}

	ag71xx_sb(ag, AG71XX_REG_MAC_CFG1, MAC_CFG1_SR);
	udelay(20);

	reset_control_assert(ag->mac_reset);
	reset_control_assert(ag->mdio_reset);
	mdelay(100);
	reset_control_deassert(ag->mac_reset);
	reset_control_deassert(ag->mdio_reset);
	mdelay(200);

	ag71xx_hw_setup(ag);
}

static void ag71xx_of_bit(struct device_node *np, const char *prop,
			  u32 *reg, u32 mask)
{
	u32 val;

	if (of_property_read_u32(np, prop, &val))
		return;

	if (val)
		*reg |= mask;
	else
		*reg &= ~mask;
}

static void ag71xx_setup_gmac_933x(struct device_node *np, void __iomem *base)
{
	u32 val = __raw_readl(base + AR933X_GMAC_REG_ETH_CFG);

	ag71xx_of_bit(np, "switch-phy-swap", &val, AR933X_ETH_CFG_SW_PHY_SWAP);
	ag71xx_of_bit(np, "switch-phy-addr-swap", &val,
		AR933X_ETH_CFG_SW_PHY_ADDR_SWAP);

	__raw_writel(val, base + AR933X_GMAC_REG_ETH_CFG);
}

static int ag71xx_setup_gmac(struct device_node *np)
{

	struct device_node *np_dev;
	void __iomem *base;
	int err = 0;

	np = of_get_child_by_name(np, "gmac-config");
	if (!np)
		return 0;

	np_dev = of_parse_phandle(np, "device", 0);
	if (!np_dev)
		return err;

	base = of_iomap(np_dev, 0);
	if (!base) {
		pr_err("%pOF: can't map GMAC registers\n", np_dev);
		return -ENOMEM;
	}

	if (of_device_is_compatible(np_dev, "qca,ar9330-gmac"))
		ag71xx_setup_gmac_933x(np, base);

	return err;
}

static int ag71xx_probe(struct device_d *dev)
{
	struct device_node *np = dev->device_node;
	struct eth_device *edev;
	struct ag71xx *ag;
	int err;

	ag = xzalloc(sizeof(struct ag71xx));

	edev = &ag->netdev;
	dev->priv = edev;
	edev->priv = ag;
	edev->parent = dev;
	ag->dev = dev;

	ag->regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(ag->regs))
		return PTR_ERR(ag->regs);

	err = ag71xx_setup_gmac(np);
	if (err)
		return err;

	ag->mdio_reset = reset_control_get(dev, "mdio");
	if (IS_ERR(ag->mdio_reset)) {
		dev_err(dev, "missing mdio reset\n");
		return PTR_ERR(ag->mdio_reset);
	}

	ag->mac_reset = reset_control_get(dev, "mac");
	if (IS_ERR(ag->mdio_reset)) {
		dev_err(dev, "missing mac reset\n");
		return PTR_ERR(ag->mdio_reset);
	}

	ag->esw_reset = reset_control_get_optional(dev, "esw");
	ag->esw_analog_reset = reset_control_get_optional(dev, "esw_analog");

	ag->phy_if_mode = of_get_phy_mode(np);
	if (ag->phy_if_mode < 0) {
		dev_err(dev, "missing phy-mode property in DT\n");
		return ag->phy_if_mode;
	}

	edev->init = ag71xx_ether_init;
	edev->open = ag71xx_ether_open;
	edev->send = ag71xx_ether_send;
	edev->recv = ag71xx_ether_rx;
	edev->halt = ag71xx_ether_halt;
	edev->get_ethaddr = ag71xx_ether_get_ethaddr;
	edev->set_ethaddr = ag71xx_ether_set_ethaddr;

	ag->rx_buffer = xmemalign(PAGE_SIZE, NO_OF_RX_FIFOS * MAX_RBUFF_SZ);
	ag->fifo_tx = dma_alloc_coherent(NO_OF_TX_FIFOS * sizeof(ag7240_desc_t),
					   &ag->addr_tx);
	ag->fifo_rx = dma_alloc_coherent(NO_OF_RX_FIFOS * sizeof(ag7240_desc_t),
					   &ag->addr_rx);

	ag71xx_wr(ag, AG71XX_REG_MAC_CFG1, 0);
	ag71xx_hw_init(ag);
	ag71xx_mdio_init(ag);

	eth_register(edev);

	dev_info(dev, "network device registered\n");

	return 0;
}

static void ag71xx_remove(struct device_d *dev)
{
	struct eth_device *edev = dev->priv;

	ag71xx_ether_halt(edev);
}

static __maybe_unused struct of_device_id ag71xx_dt_ids[] = {
	{ .compatible = "qca,ar9330-eth", },
	{ /* sentinel */ }
};

static struct driver_d ag71xx_driver = {
	.name	= "ag71xx-gmac",
	.probe		= ag71xx_probe,
	.remove		= ag71xx_remove,
	.of_compatible = DRV_OF_COMPAT(ag71xx_dt_ids),
};
device_platform_driver(ag71xx_driver);
