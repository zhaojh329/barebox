/*
 * (C) Copyright 2010
 * Vipin Kumar, ST Micoelectronics, vipin.kumar@st.com.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DESIGNWARE_ETH_H
#define __DESIGNWARE_ETH_H

#include <net.h>

struct dw_eth_dev {
	struct eth_device netdev;
	struct mii_bus miibus;

	void (*fix_mac_speed)(int speed);
	u8 macaddr[6];
	u32 tx_currdescnum;
	u32 rx_currdescnum;

	struct dmamacdescr *tx_mac_descrtable;
	struct dmamacdescr *rx_mac_descrtable;

	u8 *txbuffs;
	u8 *rxbuffs;

	struct eth_mac_regs *mac_regs_p;
	struct eth_dma_regs *dma_regs_p;
	int phy_addr;
	phy_interface_t interface;
	int enh_desc;

	struct reset_control	*rst;
};

struct dw_eth_drvdata {
	bool enh_desc;
};

struct dw_eth_dev *dwc_drv_probe(struct device_d *dev);

#define CONFIG_TX_DESCR_NUM	16
#define CONFIG_RX_DESCR_NUM	16
#define CONFIG_ETH_BUFSIZE	2048
#define TX_TOTAL_BUFSIZE	(CONFIG_ETH_BUFSIZE * CONFIG_TX_DESCR_NUM)
#define RX_TOTAL_BUFSIZE	(CONFIG_ETH_BUFSIZE * CONFIG_RX_DESCR_NUM)

struct eth_mac_regs {
	u32 conf;		/* 0x00 */
	u32 framefilt;		/* 0x04 */
	u32 hashtablehigh;	/* 0x08 */
	u32 hashtablelow;	/* 0x0c */
	u32 miiaddr;		/* 0x10 */
	u32 miidata;		/* 0x14 */
	u32 flowcontrol;	/* 0x18 */
	u32 vlantag;		/* 0x1c */
	u32 version;		/* 0x20 */
	u8 reserved_1[20];
	u32 intreg;		/* 0x38 */
	u32 intmask;		/* 0x3c */
	u32 macaddr0hi;		/* 0x40 */
	u32 macaddr0lo;		/* 0x44 */
};

/* MAC configuration register definitions */
#define FRAMEBURSTENABLE	(1 << 21)
#define MII_PORTSELECT		(1 << 15)
#define FES_100			(1 << 14)
#define DISABLERXOWN		(1 << 13)
#define FULLDPLXMODE		(1 << 11)
#define RXENABLE		(1 << 2)
#define TXENABLE		(1 << 3)

/* MII address register definitions */
#define MII_BUSY		(1 << 0)
#define MII_WRITE		(1 << 1)
#define MII_CLKRANGE_60_100M	(0)
#define MII_CLKRANGE_100_150M	(0x4)
#define MII_CLKRANGE_20_35M	(0x8)
#define MII_CLKRANGE_35_60M	(0xC)
#define MII_CLKRANGE_150_250M	(0x10)
#define MII_CLKRANGE_250_300M	(0x14)

#define MIIADDRSHIFT		(11)
#define MIIREGSHIFT		(6)
#define MII_REGMSK		(0x1F << 6)
#define MII_ADDRMSK		(0x1F << 11)


struct eth_dma_regs {
	u32 busmode;		/* 0x00 */
	u32 txpolldemand;	/* 0x04 */
	u32 rxpolldemand;	/* 0x08 */
	u32 rxdesclistaddr;	/* 0x0c */
	u32 txdesclistaddr;	/* 0x10 */
	u32 status;		/* 0x14 */
	u32 opmode;		/* 0x18 */
	u32 intenable;		/* 0x1c */
	u8 reserved[40];
	u32 currhosttxdesc;	/* 0x48 */
	u32 currhostrxdesc;	/* 0x4c */
	u32 currhosttxbuffaddr;	/* 0x50 */
	u32 currhostrxbuffaddr;	/* 0x54 */
};

#define DW_DMA_BASE_OFFSET	(0x1000)

/* Bus mode register definitions */
#define FIXEDBURST		(1 << 16)
#define PRIORXTX_41		(3 << 14)
#define PRIORXTX_31		(2 << 14)
#define PRIORXTX_21		(1 << 14)
#define PRIORXTX_11		(0 << 14)
#define BURST_1			(1 << 8)
#define BURST_2			(2 << 8)
#define BURST_4			(4 << 8)
#define BURST_8			(8 << 8)
#define BURST_16		(16 << 8)
#define BURST_32		(32 << 8)
#define RXHIGHPRIO		(1 << 1)
#define DMAMAC_SRST		(1 << 0)

/* Poll demand definitions */
#define POLL_DATA		(0xFFFFFFFF)

/* Operation mode definitions */
#define STOREFORWARD		(1 << 21)
#define FLUSHTXFIFO		(1 << 20)
#define TXSTART			(1 << 13)
#define TXSECONDFRAME		(1 << 2)
#define RXSTART			(1 << 1)

/* Descriptior related definitions */
#define MAC_MAX_FRAME_SZ	(1600)

struct dmamacdescr {
	u32 txrx_status;
	u32 dmamac_cntl;
	void *dmamac_addr;
	struct dmamacdescr *dmamac_next;
};

/*
 * txrx_status definitions
 */

/* tx status bits definitions */
#define DESC_ENH_TXSTS_OWNBYDMA		(1 << 31)
#define DESC_ENH_TXSTS_TXINT		(1 << 30)
#define DESC_ENH_TXSTS_TXLAST		(1 << 29)
#define DESC_ENH_TXSTS_TXFIRST		(1 << 28)
#define DESC_ENH_TXSTS_TXCRCDIS		(1 << 27)

#define DESC_ENH_TXSTS_TXPADDIS		(1 << 26)
#define DESC_ENH_TXSTS_TXCHECKINSCTRL	(3 << 22)
#define DESC_ENH_TXSTS_TXRINGEND		(1 << 21)
#define DESC_ENH_TXSTS_TXCHAIN		(1 << 20)
#define DESC_ENH_TXSTS_MSK			(0x1FFFF << 0)

#define DESC_TXSTS_OWNBYDMA		(1 << 31)
#define DESC_TXSTS_MSK			(0x1FFFF << 0)

/* rx status bits definitions */
#define DESC_RXSTS_OWNBYDMA		(1 << 31)
#define DESC_RXSTS_DAFILTERFAIL		(1 << 30)
#define DESC_RXSTS_FRMLENMSK		(0x3FFF << 16)
#define DESC_RXSTS_FRMLENSHFT		(16)

#define DESC_RXSTS_ERROR		(1 << 15)
#define DESC_RXSTS_RXTRUNCATED		(1 << 14)
#define DESC_RXSTS_SAFILTERFAIL		(1 << 13)
#define DESC_RXSTS_RXIPC_GIANTFRAME	(1 << 12)
#define DESC_RXSTS_RXDAMAGED		(1 << 11)
#define DESC_RXSTS_RXVLANTAG		(1 << 10)
#define DESC_RXSTS_RXFIRST		(1 << 9)
#define DESC_RXSTS_RXLAST		(1 << 8)
#define DESC_RXSTS_RXIPC_GIANT		(1 << 7)
#define DESC_RXSTS_RXCOLLISION		(1 << 6)
#define DESC_RXSTS_RXFRAMEETHER		(1 << 5)
#define DESC_RXSTS_RXWATCHDOG		(1 << 4)
#define DESC_RXSTS_RXMIIERROR		(1 << 3)
#define DESC_RXSTS_RXDRIBBLING		(1 << 2)
#define DESC_RXSTS_RXCRC		(1 << 1)

/*
 * dmamac_cntl definitions
 */

/* tx control bits definitions */
#define DESC_ENH_TXCTRL_SIZE1MASK		(0x1FFF << 0)
#define DESC_ENH_TXCTRL_SIZE1SHFT		(0)
#define DESC_ENH_TXCTRL_SIZE2MASK		(0x1FFF << 16)
#define DESC_ENH_TXCTRL_SIZE2SHFT		(16)

#define DESC_TXCTRL_TXINT		(1 << 31)
#define DESC_TXCTRL_TXLAST		(1 << 30)
#define DESC_TXCTRL_TXFIRST		(1 << 29)
#define DESC_TXCTRL_TXCHECKINSCTRL	(3 << 27)
#define DESC_TXCTRL_TXCRCDIS		(1 << 26)
#define DESC_TXCTRL_TXRINGEND		(1 << 25)
#define DESC_TXCTRL_TXCHAIN		(1 << 24)

#define DESC_TXCTRL_SIZE1MASK		(0x7FF << 0)
#define DESC_TXCTRL_SIZE1SHFT		(0)
#define DESC_TXCTRL_SIZE2MASK		(0x7FF << 11)
#define DESC_TXCTRL_SIZE2SHFT		(11)

/* rx control bits definitions */
#define DESC_ENH_RXCTRL_RXINTDIS		(1 << 31)
#define DESC_ENH_RXCTRL_RXRINGEND		(1 << 15)
#define DESC_ENH_RXCTRL_RXCHAIN		(1 << 14)

#define DESC_ENH_RXCTRL_SIZE1MASK		(0x1FFF << 0)
#define DESC_ENH_RXCTRL_SIZE1SHFT		(0)
#define DESC_ENH_RXCTRL_SIZE2MASK		(0x1FFF << 16)
#define DESC_ENH_RXCTRL_SIZE2SHFT		(16)

#define DESC_RXCTRL_RXINTDIS		(1 << 31)
#define DESC_RXCTRL_RXRINGEND		(1 << 25)
#define DESC_RXCTRL_RXCHAIN		(1 << 24)

#define DESC_RXCTRL_SIZE1MASK		(0x7FF << 0)
#define DESC_RXCTRL_SIZE1SHFT		(0)
#define DESC_RXCTRL_SIZE2MASK		(0x7FF << 11)
#define DESC_RXCTRL_SIZE2SHFT		(11)

#endif
