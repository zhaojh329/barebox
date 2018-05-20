/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2015 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2015 Felix Fietkau <nbd@nbd.name>
 *   Copyright (C) 2013-2015 Michael Lee <igvtee@gmail.com>
 */

#include <init.h>
#include <io.h>
#include <dma.h>
#include <errno.h>
#include <asm/barrier.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <mach/ralink_regs.h>

#include "mtk_eth_soc.h"
#include "mdio.h"

#define TX_DMA_DESP2_DEF	(TX_DMA_LS0 | TX_DMA_DONE)
#define TX_DMA_DESP4_DEF	(TX_DMA_QN(3) | TX_DMA_PN(1))
#define NEXT_TX_DESP_IDX(X)	(((X) + 1) & (ring->tx_ring_size - 1))
#define NEXT_RX_DESP_IDX(X)	(((X) + 1) & (ring->rx_ring_size - 1))


#define SYSC_REG_RSTCTRL	0x34

static const u16 fe_reg_table_default[FE_REG_COUNT] = {
	[FE_REG_PDMA_GLO_CFG] = FE_PDMA_GLO_CFG,
	[FE_REG_PDMA_RST_CFG] = FE_PDMA_RST_CFG,
	[FE_REG_DLY_INT_CFG] = FE_DLY_INT_CFG,
	[FE_REG_TX_BASE_PTR0] = FE_TX_BASE_PTR0,
	[FE_REG_TX_MAX_CNT0] = FE_TX_MAX_CNT0,
	[FE_REG_TX_CTX_IDX0] = FE_TX_CTX_IDX0,
	[FE_REG_TX_DTX_IDX0] = FE_TX_DTX_IDX0,
	[FE_REG_RX_BASE_PTR0] = FE_RX_BASE_PTR0,
	[FE_REG_RX_MAX_CNT0] = FE_RX_MAX_CNT0,
	[FE_REG_RX_CALC_IDX0] = FE_RX_CALC_IDX0,
	[FE_REG_RX_DRX_IDX0] = FE_RX_DRX_IDX0,
	[FE_REG_FE_INT_ENABLE] = FE_FE_INT_ENABLE,
	[FE_REG_FE_INT_STATUS] = FE_FE_INT_STATUS,
	[FE_REG_FE_DMA_VID_BASE] = FE_DMA_VID0,
	[FE_REG_FE_COUNTER_BASE] = FE_GDMA1_TX_GBCNT,
	[FE_REG_FE_RST_GL] = FE_FE_RST_GL,
};

static const u16 *fe_reg_table = fe_reg_table_default;

static void __iomem *fe_base;

void fe_w32(u32 val, unsigned reg)
{
	__raw_writel(val, fe_base + reg);
}

u32 fe_r32(unsigned reg)
{
	return __raw_readl(fe_base + reg);
}

void fe_reg_w32(u32 val, enum fe_reg reg)
{
	fe_w32(val, fe_reg_table[reg]);
}

u32 fe_reg_r32(enum fe_reg reg)
{
	return fe_r32(fe_reg_table[reg]);
}

void fe_m32(struct fe_priv *eth, u32 clear, u32 set, unsigned reg)
{
	u32 val;

	val = __raw_readl(fe_base + reg);
	val &= ~clear;
	val |= set;
	__raw_writel(val, fe_base + reg);
}

void fe_reset(u32 reset_bits)
{
	u32 t;

	t = rt_sysc_r32(SYSC_REG_RSTCTRL);
	t |= reset_bits;
	rt_sysc_w32(t, SYSC_REG_RSTCTRL);
	udelay(15);

	t &= ~reset_bits;
	rt_sysc_w32(t, SYSC_REG_RSTCTRL);
	udelay(15);
}

static inline void fe_int_disable(u32 mask)
{
	fe_reg_w32(fe_reg_r32(FE_REG_FE_INT_ENABLE) & ~mask,
		   FE_REG_FE_INT_ENABLE);
	/* flush write */
	fe_reg_r32(FE_REG_FE_INT_ENABLE);
}

static int mtk_eth_send(struct eth_device *edev, void *packet, int length)
{
	struct fe_priv *priv = edev->priv;
	struct device_d *dev = priv->dev;
	struct fe_tx_ring *ring = &priv->tx_ring;
	struct fe_tx_dma *txd = &ring->tx_dma[ring->tx_next_idx];
	uint64_t start;
	int ret = 0;

	/* flush */
	dma_sync_single_for_device((unsigned long)packet, length, DMA_TO_DEVICE);

	memset(txd, 0, sizeof(*txd));

	/* init tx descriptor */
	if (priv->soc->tx_dma)
		priv->soc->tx_dma(txd);
	else
		txd->txd4 = TX_DMA_DESP4_DEF;

	txd->txd1 = virt_to_phys(packet);
	txd->txd2 = TX_DMA_PLEN0(length);

	/* set last segment */
	txd->txd2 |= TX_DMA_LS0;
	ring->tx_next_idx = NEXT_TX_DESP_IDX(ring->tx_next_idx);

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();
	fe_reg_w32(ring->tx_next_idx, FE_REG_TX_CTX_IDX0);

	start = get_time_ns();
	while (!(txd->txd2 & TX_DMA_DONE)) {
		if (!is_timeout_non_interruptible(start, 100 * USECOND))
			continue;

		dev_err(dev, "error: tx timed out\n");
		ret = -ETIMEDOUT;
		break;
	}

	return ret;
}

static int mtk_eth_rx(struct eth_device *edev)
{
	struct fe_priv *priv = edev->priv;
	struct fe_rx_ring *ring = &priv->rx_ring;
	int idx = ring->rx_calc_idx;
	struct fe_rx_dma *rxd;
	unsigned int work_done;

	for (work_done = 0; work_done < ring->rx_ring_size; work_done++) {
		unsigned int pktlen;
		u8 *data;

		idx = NEXT_RX_DESP_IDX(idx);
		rxd = &ring->rx_dma[idx];
		data = ring->rx_data[idx];

		if (!(rxd->rxd2 & RX_DMA_DONE))
			break;

		pktlen = RX_DMA_GET_PLEN0(rxd->rxd2);

		/* invalidate */
		dma_sync_single_for_cpu((unsigned long)data, pktlen,
						DMA_FROM_DEVICE);

		net_receive(edev, data, pktlen);

		if (priv->flags & FE_FLAG_RX_SG_DMA)
			rxd->rxd2 = RX_DMA_PLEN0(ring->rx_buf_size);
		else
			rxd->rxd2 = RX_DMA_LSO;

		ring->rx_calc_idx = idx;

		/* make sure that all changes to the dma ring are flushed before
		 * we continue
		 */
		wmb();
		fe_reg_w32(ring->rx_calc_idx, FE_REG_RX_CALC_IDX0);
	}

	return 0;
}

static void fe_halt(struct eth_device *edev)
{
	int i;

	fe_reg_w32(fe_reg_r32(FE_REG_PDMA_GLO_CFG) &
		     ~(FE_TX_WB_DDONE | FE_RX_DMA_EN | FE_TX_DMA_EN),
		     FE_REG_PDMA_GLO_CFG);

	/* wait dma stop */
	for (i = 0; i < 10; i++) {
		if (fe_reg_r32(FE_REG_PDMA_GLO_CFG) &
				(FE_TX_DMA_BUSY | FE_RX_DMA_BUSY)) {
			mdelay(20);
			continue;
		}
		break;
	}
}

static int mtk_get_ethaddr(struct eth_device *edev, unsigned char *adr)
{
	return 0;
}

static int mtk_set_ethaddr(struct eth_device *edev, const unsigned char *adr)
{
	return 0;
}

static int fe_alloc_tx(struct fe_priv *priv)
{
	int i;
	struct fe_tx_ring *ring = &priv->tx_ring;

	ring->tx_free_idx = 0;
	ring->tx_next_idx = 0;

	ring->tx_dma = dma_alloc_coherent(ring->tx_ring_size * sizeof(*ring->tx_dma),
			&ring->tx_phys);
	if (!ring->tx_dma)
		goto no_tx_mem;

	for (i = 0; i < ring->tx_ring_size; i++) {
		if (priv->soc->tx_dma)
			priv->soc->tx_dma(&ring->tx_dma[i]);
		ring->tx_dma[i].txd2 = TX_DMA_DESP2_DEF;
	}

	fe_reg_w32(ring->tx_phys, FE_REG_TX_BASE_PTR0);
	fe_reg_w32(ring->tx_ring_size, FE_REG_TX_MAX_CNT0);
	fe_reg_w32(0, FE_REG_TX_CTX_IDX0);
	fe_reg_w32(FE_PST_DTX_IDX0, FE_REG_PDMA_RST_CFG);

	return 0;
	
no_tx_mem:
	return -ENOMEM;
}

static int fe_alloc_rx(struct fe_priv *priv)
{
	struct fe_rx_ring *ring = &priv->rx_ring;
	int i;

	ring->rx_data = kcalloc(ring->rx_ring_size, sizeof(*ring->rx_data),
			GFP_KERNEL);
	if (!ring->rx_data)
		goto no_rx_mem;

	for (i = 0; i < ring->rx_ring_size; i++) {
		ring->rx_data[i] = xzalloc(ring->frag_size);
		if (!ring->rx_data[i])
			goto no_rx_mem;
	}

	ring->rx_dma = dma_alloc_coherent(ring->rx_ring_size * sizeof(*ring->rx_dma),
			&ring->rx_phys);
	if (!ring->rx_dma)
		goto no_rx_mem;

	for (i = 0; i < ring->rx_ring_size; i++) {
		ring->rx_dma[i].rxd1 = virt_to_phys(ring->rx_data[i]);

		if (priv->flags & FE_FLAG_RX_SG_DMA)
			ring->rx_dma[i].rxd2 = RX_DMA_PLEN0(ring->rx_buf_size);
		else
			ring->rx_dma[i].rxd2 = RX_DMA_LSO;
	}
	ring->rx_calc_idx = ring->rx_ring_size - 1;

	fe_reg_w32(ring->rx_phys, FE_REG_RX_BASE_PTR0);
	fe_reg_w32(ring->rx_ring_size, FE_REG_RX_MAX_CNT0);
	fe_reg_w32(ring->rx_calc_idx, FE_REG_RX_CALC_IDX0);
	fe_reg_w32(FE_PST_DRX_IDX0, FE_REG_PDMA_RST_CFG);

	return 0;

no_rx_mem:
	return -ENOMEM;
}

int fe_set_clock_cycle(struct fe_priv *priv)
{
	unsigned long sysclk = priv->sysclk;

	sysclk /= FE_US_CYC_CNT_DIVISOR;
	sysclk <<= FE_US_CYC_CNT_SHIFT;

	fe_w32((fe_r32(FE_FE_GLO_CFG) &
			~(FE_US_CYC_CNT_MASK << FE_US_CYC_CNT_SHIFT)) |
			sysclk,
			FE_FE_GLO_CFG);
	return 0;
}

void fe_fwd_config(struct fe_priv *priv)
{
	u32 fwd_cfg;

	fwd_cfg = fe_r32(FE_GDMA1_FWD_CFG);

	/* disable jumbo frame */
	if (priv->flags & FE_FLAG_JUMBO_FRAME)
		fwd_cfg &= ~FE_GDM1_JMB_EN;

	/* set unicast/multicast/broadcast frame to cpu */
	fwd_cfg &= ~0xffff;

	fe_w32(fwd_cfg, FE_GDMA1_FWD_CFG);
}

static void fe_rxcsum_config(bool enable)
{
	if (enable)
		fe_w32(fe_r32(FE_GDMA1_FWD_CFG) | (FE_GDM1_ICS_EN |
					FE_GDM1_TCS_EN | FE_GDM1_UCS_EN),
				FE_GDMA1_FWD_CFG);
	else
		fe_w32(fe_r32(FE_GDMA1_FWD_CFG) & ~(FE_GDM1_ICS_EN |
					FE_GDM1_TCS_EN | FE_GDM1_UCS_EN),
				FE_GDMA1_FWD_CFG);
}

static void fe_txcsum_config(bool enable)
{
	if (enable)
		fe_w32(fe_r32(FE_CDMA_CSG_CFG) | (FE_ICS_GEN_EN |
					FE_TCS_GEN_EN | FE_UCS_GEN_EN),
				FE_CDMA_CSG_CFG);
	else
		fe_w32(fe_r32(FE_CDMA_CSG_CFG) & ~(FE_ICS_GEN_EN |
					FE_TCS_GEN_EN | FE_UCS_GEN_EN),
				FE_CDMA_CSG_CFG);
}

void fe_csum_config(struct fe_priv *priv)
{
	fe_txcsum_config(false);
	fe_rxcsum_config(false);
}

static int fe_hw_init(struct eth_device *edev)
{
	struct fe_priv *priv = edev->priv;
	int i;

	/* disable delay interrupt */
	fe_reg_w32(0, FE_REG_DLY_INT_CFG);

	fe_int_disable(priv->soc->tx_int | priv->soc->rx_int);

	/* frame engine will push VLAN tag regarding to VIDX feild in Tx desc */
	if (fe_reg_table[FE_REG_FE_DMA_VID_BASE])
		for (i = 0; i < 16; i += 2)
			fe_w32(((i + 1) << 16) + i,
			       fe_reg_table[FE_REG_FE_DMA_VID_BASE] +
			       (i * 2));

	if (priv->soc->fwd_config(priv))
		dev_err(priv->dev, "unable to get clock\n");

	if (fe_reg_table[FE_REG_FE_RST_GL]) {
		fe_reg_w32(1, FE_REG_FE_RST_GL);
		fe_reg_w32(0, FE_REG_FE_RST_GL);
	}

	return 0;
}

static int mtk_eth_open(struct eth_device *edev)
{
	struct fe_priv *priv = edev->priv;
	u32 val;

	fe_alloc_tx(priv);
	fe_alloc_rx(priv);

	val = FE_TX_WB_DDONE | FE_RX_DMA_EN | FE_TX_DMA_EN;
	if (priv->flags & FE_FLAG_RX_2B_OFFSET)
		val |= FE_RX_2B_OFFSET;
	val |= priv->soc->pdma_glo_cfg;
	fe_reg_w32(val, FE_REG_PDMA_GLO_CFG);

	if (priv->phy)
		priv->phy->start(priv);

	mtk_switch_init();

	return 0;
}

static int mtk_eth_init(struct eth_device *edev)
{
	struct fe_priv *priv = edev->priv;
	struct device_node *port;
	int err;

	priv->soc->reset_fe();

	if (priv->soc->switch_init)
		if (priv->soc->switch_init(priv)) {
			dev_err(priv->dev, "failed to initialize switch core\n");
			return -ENODEV;
		}

	err = fe_mdio_init(priv);
	if (err)
		return err;

	if (priv->soc->port_init)
		for_each_child_of_node(priv->dev->device_node, port)
			if (of_device_is_compatible(port, "mediatek,eth-port") &&
			    of_device_is_available(port))
				priv->soc->port_init(priv, port);

	if (priv->phy) {
		err = priv->phy->connect(priv);
		if (err) {
			dev_err(priv->dev, "phy connect failed\n");
			return err;
		}
	}

	err = fe_hw_init(edev);
	if (err) {
		dev_err(priv->dev, "fe_hw_init failed\n");
		return err;
	}

	if ((priv->flags & FE_FLAG_HAS_SWITCH) && priv->soc->switch_config)
		priv->soc->switch_config(priv);

	return 0;
}

static int fe_probe(struct device_d *dev)
{
	struct fe_soc_data *soc;
	struct eth_device *edev;
	struct fe_priv *priv;
	struct clk *sysclk;
	int err;

	device_reset(dev);

	dev_get_drvdata(dev, (const void **)&soc);

	if (soc->reg_table)
		fe_reg_table = soc->reg_table;
	else
		soc->reg_table = fe_reg_table;

	fe_base = dev_request_mem_region(dev, 0);

	priv = xzalloc(sizeof(struct fe_priv));
	edev = &priv->edev;
	dev->priv = edev;
	edev->priv = priv;
	edev->parent = dev;
	priv->dev = dev;
	priv->soc = soc;

	if (soc->init_data)
		soc->init_data(soc, edev);

	sysclk = clk_get(dev, NULL);
	if (!IS_ERR(sysclk)) {
		priv->sysclk = clk_get_rate(sysclk);
	} else if ((priv->flags & FE_FLAG_CALIBRATE_CLK)) {
		dev_err(dev, "this soc needs a clk for calibration\n");
		err = -ENXIO;
		goto err_free_dev;
	}

	priv->switch_np = of_parse_phandle(dev->device_node, "mediatek,switch", 0);
	if ((priv->flags & FE_FLAG_HAS_SWITCH) && !priv->switch_np) {
		dev_err(dev, "failed to read switch phandle\n");
		err = -ENODEV;
		goto err_free_dev;
	}

	edev->init = mtk_eth_init;
	edev->open = mtk_eth_open;
	edev->send = mtk_eth_send;
	edev->recv = mtk_eth_rx;
	edev->halt = fe_halt;
	edev->set_ethaddr = mtk_set_ethaddr;
	edev->get_ethaddr = mtk_get_ethaddr;
	edev->parent = dev;

	priv->rx_ring.frag_size = 1824;
	priv->rx_ring.rx_buf_size = 1566;
	priv->tx_ring.tx_ring_size = NUM_DMA_DESC;
	priv->rx_ring.rx_ring_size = NUM_DMA_DESC;

	pr_info("%s probed\n", dev->of_id_entry->compatible);

	err = eth_register(edev);
	if (err) {
		dev_err(dev, "error bringing up device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	free(priv);
	return err;
}

static struct driver_d fe_driver = {
	.name  = "mtk_soc_eth",
	.probe = fe_probe,
	.of_compatible = DRV_OF_COMPAT(of_fe_match),
};
device_platform_driver(fe_driver);

