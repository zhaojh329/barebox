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

#ifndef __AG71XX_H
#define __AG71XX_H

#include <common.h>
#include <driver.h>
#include <net.h>
#include <dma.h>
#include <init.h>
#include <io.h>
#include <linux/err.h>
#include <linux/phy.h>
#include <of_net.h>
#include <of_address.h>
#include <linux/reset.h>

#include <mach/ath79.h>

/* Register offsets */
#define AG71XX_REG_MAC_CFG1	0x0000
#define AG71XX_REG_MAC_CFG2	0x0004
#define AG71XX_REG_MAC_IPG	0x0008
#define AG71XX_REG_MAC_HDX	0x000c
#define AG71XX_REG_MAC_MFL	0x0010
#define AG71XX_REG_MII_CFG	0x0020
#define AG71XX_REG_MII_CMD	0x0024
#define AG71XX_REG_MII_ADDR	0x0028
#define AG71XX_REG_MII_CTRL	0x002c
#define AG71XX_REG_MII_STATUS	0x0030
#define AG71XX_REG_MII_IND	0x0034
#define AG71XX_REG_MAC_IFCTL	0x0038
#define AG71XX_REG_MAC_ADDR1	0x0040
#define AG71XX_REG_MAC_ADDR2	0x0044
#define AG71XX_REG_FIFO_CFG0	0x0048
#define AG71XX_REG_FIFO_CFG1	0x004c
#define AG71XX_REG_FIFO_CFG2	0x0050
#define AG71XX_REG_FIFO_CFG3	0x0054
#define AG71XX_REG_FIFO_CFG4	0x0058
#define AG71XX_REG_FIFO_CFG5	0x005c
#define AG71XX_REG_FIFO_RAM0	0x0060
#define AG71XX_REG_FIFO_RAM1	0x0064
#define AG71XX_REG_FIFO_RAM2	0x0068
#define AG71XX_REG_FIFO_RAM3	0x006c
#define AG71XX_REG_FIFO_RAM4	0x0070
#define AG71XX_REG_FIFO_RAM5	0x0074
#define AG71XX_REG_FIFO_RAM6	0x0078
#define AG71XX_REG_FIFO_RAM7	0x007c

#define AG71XX_REG_TX_CTRL	0x0180
#define AG71XX_REG_TX_DESC	0x0184
#define AG71XX_REG_TX_STATUS	0x0188
#define AG71XX_REG_RX_CTRL	0x018c
#define AG71XX_REG_RX_DESC	0x0190
#define AG71XX_REG_RX_STATUS	0x0194
#define AG71XX_REG_INT_ENABLE	0x0198
#define AG71XX_REG_INT_STATUS	0x019c

#define AG71XX_REG_FIFO_DEPTH	0x01a8
#define AG71XX_REG_RX_SM	0x01b0
#define AG71XX_REG_TX_SM	0x01b4

#define MAC_CFG1_TXE		BIT(0)	/* Tx Enable */
#define MAC_CFG1_STX		BIT(1)	/* Synchronize Tx Enable */
#define MAC_CFG1_RXE		BIT(2)	/* Rx Enable */
#define MAC_CFG1_SRX		BIT(3)	/* Synchronize Rx Enable */
#define MAC_CFG1_TFC		BIT(4)	/* Tx Flow Control Enable */
#define MAC_CFG1_RFC		BIT(5)	/* Rx Flow Control Enable */
#define MAC_CFG1_LB		BIT(8)	/* Loopback mode */
#define MAC_CFG1_TX_RST		BIT(18)	/* Tx Reset */
#define MAC_CFG1_RX_RST		BIT(19)	/* Rx Reset */
#define MAC_CFG1_SR		BIT(31)	/* Soft Reset */

#define MAC_CFG2_FDX		BIT(0)
#define MAC_CFG2_CRC_EN		BIT(1)
#define MAC_CFG2_PAD_CRC_EN	BIT(2)
#define MAC_CFG2_LEN_CHECK	BIT(4)
#define MAC_CFG2_HUGE_FRAME_EN	BIT(5)
#define MAC_CFG2_IF_1000	BIT(9)
#define MAC_CFG2_IF_10_100	BIT(8)

#define FIFO_CFG0_WTM		BIT(0)	/* Watermark Module */
#define FIFO_CFG0_RXS		BIT(1)	/* Rx System Module */
#define FIFO_CFG0_RXF		BIT(2)	/* Rx Fabric Module */
#define FIFO_CFG0_TXS		BIT(3)	/* Tx System Module */
#define FIFO_CFG0_TXF		BIT(4)	/* Tx Fabric Module */
#define FIFO_CFG0_ALL	(FIFO_CFG0_WTM | FIFO_CFG0_RXS | FIFO_CFG0_RXF \
			| FIFO_CFG0_TXS | FIFO_CFG0_TXF)

#define FIFO_CFG0_ENABLE_SHIFT	8

#define FIFO_CFG4_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG4_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG4_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG4_CE		BIT(3)	/* Code Error */
#define FIFO_CFG4_CR		BIT(4)	/* CRC error */
#define FIFO_CFG4_LM		BIT(5)	/* Length Mismatch */
#define FIFO_CFG4_LO		BIT(6)	/* Length out of range */
#define FIFO_CFG4_OK		BIT(7)	/* Packet is OK */
#define FIFO_CFG4_MC		BIT(8)	/* Multicast Packet */
#define FIFO_CFG4_BC		BIT(9)	/* Broadcast Packet */
#define FIFO_CFG4_DR		BIT(10)	/* Dribble */
#define FIFO_CFG4_LE		BIT(11)	/* Long Event */
#define FIFO_CFG4_CF		BIT(12)	/* Control Frame */
#define FIFO_CFG4_PF		BIT(13)	/* Pause Frame */
#define FIFO_CFG4_UO		BIT(14)	/* Unsupported Opcode */
#define FIFO_CFG4_VT		BIT(15)	/* VLAN tag detected */
#define FIFO_CFG4_FT		BIT(16)	/* Frame Truncated */
#define FIFO_CFG4_UC		BIT(17)	/* Unicast Packet */

#define FIFO_CFG5_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG5_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG5_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG5_CE		BIT(3)	/* Code Error */
#define FIFO_CFG5_LM		BIT(4)	/* Length Mismatch */
#define FIFO_CFG5_LO		BIT(5)	/* Length Out of Range */
#define FIFO_CFG5_OK		BIT(6)	/* Packet is OK */
#define FIFO_CFG5_MC		BIT(7)	/* Multicast Packet */
#define FIFO_CFG5_BC		BIT(8)	/* Broadcast Packet */
#define FIFO_CFG5_DR		BIT(9)	/* Dribble */
#define FIFO_CFG5_CF		BIT(10)	/* Control Frame */
#define FIFO_CFG5_PF		BIT(11)	/* Pause Frame */
#define FIFO_CFG5_UO		BIT(12)	/* Unsupported Opcode */
#define FIFO_CFG5_VT		BIT(13)	/* VLAN tag detected */
#define FIFO_CFG5_LE		BIT(14)	/* Long Event */
#define FIFO_CFG5_FT		BIT(15)	/* Frame Truncated */
#define FIFO_CFG5_16		BIT(16)	/* unknown */
#define FIFO_CFG5_17		BIT(17)	/* unknown */
#define FIFO_CFG5_SF		BIT(18)	/* Short Frame */
#define FIFO_CFG5_BM		BIT(19)	/* Byte Mode */

#define AG71XX_INT_TX_PS	BIT(0)
#define AG71XX_INT_TX_UR	BIT(1)
#define AG71XX_INT_TX_BE	BIT(3)
#define AG71XX_INT_RX_PR	BIT(4)
#define AG71XX_INT_RX_OF	BIT(6)
#define AG71XX_INT_RX_BE	BIT(7)

#define MAC_IFCTL_SPEED		BIT(16)

#define MII_CFG_CLK_DIV_4	0
#define MII_CFG_CLK_DIV_6	2
#define MII_CFG_CLK_DIV_8	3
#define MII_CFG_CLK_DIV_10	4
#define MII_CFG_CLK_DIV_14	5
#define MII_CFG_CLK_DIV_20	6
#define MII_CFG_CLK_DIV_28	7
#define MII_CFG_CLK_DIV_34	8
#define MII_CFG_CLK_DIV_42	9
#define MII_CFG_CLK_DIV_50	10
#define MII_CFG_CLK_DIV_58	11
#define MII_CFG_CLK_DIV_66	12
#define MII_CFG_CLK_DIV_74	13
#define MII_CFG_CLK_DIV_82	14
#define MII_CFG_CLK_DIV_98	15
#define MII_CFG_RESET		BIT(31)

#define MII_CMD_WRITE		0x0
#define MII_CMD_READ		0x1
#define MII_ADDR_SHIFT		8
#define MII_IND_BUSY		BIT(0)
#define MII_IND_INVALID		BIT(2)

#define TX_CTRL_TXE		BIT(0)	/* Tx Enable */

#define TX_STATUS_PS		BIT(0)	/* Packet Sent */
#define TX_STATUS_UR		BIT(1)	/* Tx Underrun */
#define TX_STATUS_BE		BIT(3)	/* Bus Error */

#define RX_CTRL_RXE		BIT(0)	/* Rx Enable */

#define RX_STATUS_PR		BIT(0)	/* Packet Received */
#define RX_STATUS_OF		BIT(2)	/* Rx Overflow */
#define RX_STATUS_BE		BIT(3)	/* Bus Error */

/*
 * AR933X GMAC interface
 */
#define AR933X_GMAC_REG_ETH_CFG     0x00

#define AR933X_ETH_CFG_RGMII_GE0    BIT(0)
#define AR933X_ETH_CFG_MII_GE0      BIT(1)
#define AR933X_ETH_CFG_GMII_GE0     BIT(2)
#define AR933X_ETH_CFG_MII_GE0_MASTER   BIT(3)
#define AR933X_ETH_CFG_MII_GE0_SLAVE    BIT(4)
#define AR933X_ETH_CFG_MII_GE0_ERR_EN   BIT(5)
#define AR933X_ETH_CFG_SW_PHY_SWAP  BIT(7)
#define AR933X_ETH_CFG_SW_PHY_ADDR_SWAP BIT(8)
#define AR933X_ETH_CFG_RMII_GE0     BIT(9)
#define AR933X_ETH_CFG_RMII_GE0_SPD_10  0
#define AR933X_ETH_CFG_RMII_GE0_SPD_100 BIT(10)

/*
 * AR934X GMAC Interface
 */
#define AR934X_GMAC_REG_ETH_CFG     0x00

#define AR934X_ETH_CFG_RGMII_GMAC0  BIT(0)
#define AR934X_ETH_CFG_MII_GMAC0    BIT(1)
#define AR934X_ETH_CFG_GMII_GMAC0   BIT(2)
#define AR934X_ETH_CFG_MII_GMAC0_MASTER BIT(3)
#define AR934X_ETH_CFG_MII_GMAC0_SLAVE  BIT(4)
#define AR934X_ETH_CFG_MII_GMAC0_ERR_EN BIT(5)
#define AR934X_ETH_CFG_SW_ONLY_MODE BIT(6)
#define AR934X_ETH_CFG_SW_PHY_SWAP  BIT(7)
#define AR934X_ETH_CFG_SW_APB_ACCESS    BIT(9)
#define AR934X_ETH_CFG_RMII_GMAC0   BIT(10)
#define AR933X_ETH_CFG_MII_CNTL_SPEED   BIT(11)
#define AR934X_ETH_CFG_RMII_GMAC0_MASTER BIT(12)
#define AR933X_ETH_CFG_SW_ACC_MSB_FIRST BIT(13)
#define AR934X_ETH_CFG_RXD_DELAY        BIT(14)
#define AR934X_ETH_CFG_RXD_DELAY_MASK   0x3
#define AR934X_ETH_CFG_RXD_DELAY_SHIFT  14
#define AR934X_ETH_CFG_RDV_DELAY        BIT(16)
#define AR934X_ETH_CFG_RDV_DELAY_MASK   0x3
#define AR934X_ETH_CFG_RDV_DELAY_SHIFT  16

/*
 * h/w descriptor
 */
typedef struct {
	uint32_t    pkt_start_addr;

	uint32_t    is_empty       :  1;
	uint32_t    res1           : 10;
	uint32_t    ftpp_override  :  5;
	uint32_t    res2           :  4;
	uint32_t    pkt_size       : 12;

	uint32_t    next_desc      ;
} ag7240_desc_t;

#define NO_OF_TX_FIFOS  8
#define NO_OF_RX_FIFOS  8
#define TX_RING_SZ (NO_OF_TX_FIFOS * sizeof(ag7240_desc_t))
#define MAX_RBUFF_SZ	0x600		/* 1518 rounded up */

#define MAX_WAIT        1000

struct ar7240sw {
	struct mii_bus	*mii_bus;
	const char *name;
	int num_ports;
	u8 ver;
};

struct ag71xx {
	struct device_d *dev;
	struct eth_device netdev;
	void __iomem *regs;
	struct mii_bus miibus;
	int	phy_if_mode;
	struct ar7240sw as;
	struct reset_control *mac_reset;
	struct reset_control *mdio_reset;
	struct reset_control *esw_reset;
	struct reset_control *esw_analog_reset;

	void *rx_buffer;

	unsigned char *rx_pkt[NO_OF_RX_FIFOS];
	ag7240_desc_t *fifo_tx;
	ag7240_desc_t *fifo_rx;
	dma_addr_t addr_tx;
	dma_addr_t addr_rx;

	int next_tx;
	int next_rx;
};

static inline void ag71xx_check_reg_offset(struct ag71xx *ag, int reg)
{
	switch (reg) {
	case AG71XX_REG_MAC_CFG1 ... AG71XX_REG_MAC_MFL:
	case AG71XX_REG_MAC_IFCTL ... AG71XX_REG_TX_SM:
	case AG71XX_REG_MII_CFG ... AG71XX_REG_MII_IND:
		break;

	default:
		BUG();
	}
}

static inline u32 ag71xx_rr(struct ag71xx *ag, int reg)
{
	ag71xx_check_reg_offset(ag, reg);

	return __raw_readl(ag->regs + reg);
}

static inline void ag71xx_wr(struct ag71xx *ag, int reg, u32 val)
{
	ag71xx_check_reg_offset(ag, reg);

	__raw_writel(val, ag->regs + reg);
	/* flush write */
	(void)__raw_readl(ag->regs + reg);
}

static inline void ag71xx_sb(struct ag71xx *ag, unsigned reg, u32 mask)
{
	void __iomem *r;

	r = ag->regs + reg;
	__raw_writel(__raw_readl(r) | mask, r);
	/* flush write */
	(void) __raw_readl(r);
}

static inline void ag71xx_cb(struct ag71xx *ag, unsigned reg, u32 mask)
{
	void __iomem *r;

	r = ag->regs + reg;
	__raw_writel(__raw_readl(r) & ~mask, r);
	/* flush write */
	(void) __raw_readl(r);
}

int ag71xx_mdio_init(struct ag71xx *ag);
int ag71xx_mdio_mii_read(struct mii_bus *bus, int addr, int reg);
int ag71xx_mdio_mii_write(struct mii_bus *bus, int addr, int reg, u16 val);

void ag71xx_ar7240_start(struct ag71xx *ag);
int ag71xx_ar7240_init(struct ag71xx *ag, struct device_node *np);
int ar7240sw_phy_write(struct mii_bus *mii, int phy_addr, int reg_addr,
		       u16 reg_val);
int ar7240sw_phy_read(struct mii_bus *mii, int phy_addr, int reg_addr);

#endif /* _AG71XX_H */
