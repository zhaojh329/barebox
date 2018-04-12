/*
 * Copyright 2007,2010 Freescale Semiconductor, Inc
 * Andy Fleming
 *
 * Based vaguely on the pxa mmc code:
 * (C) Copyright 2003
 * Kyle Harris, Nexus Technologies, Inc. kharris@nexus-tech.net
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
#include <config.h>
#include <common.h>
#include <dma.h>
#include <driver.h>
#include <init.h>
#include <of.h>
#include <malloc.h>
#include <mci.h>
#include <clock.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <mach/generic.h>
#include <mach/esdhc.h>
#include <gpio.h>
#include <of_device.h>

#include "sdhci.h"
#include "imx-esdhc.h"

/*
 * The CMDTYPE of the CMD register (offset 0xE) should be set to
 * "11" when the STOP CMD12 is issued on imx53 to abort one
 * open ended multi-blk IO. Otherwise the TC INT wouldn't
 * be generated.
 * In exact block transfer, the controller doesn't complete the
 * operations automatically as required at the end of the
 * transfer and remains on hold if the abort command is not sent.
 * As a result, the TC flag is not asserted and SW  received timeout
 * exeception. Bit1 of Vendor Spec registor is used to fix it.
 */
#define ESDHC_FLAG_MULTIBLK_NO_INT	BIT(1)
/*
 * The flag enables the workaround for ESDHC errata ENGcm07207 which
 * affects i.MX25 and i.MX35.
 */
#define ESDHC_FLAG_ENGCM07207		BIT(2)
/*
 * The flag tells that the ESDHC controller is an USDHC block that is
 * integrated on the i.MX6 series.
 */
#define ESDHC_FLAG_USDHC		BIT(3)
/* The IP supports manual tuning process */
#define ESDHC_FLAG_MAN_TUNING		BIT(4)
/* The IP supports standard tuning process */
#define ESDHC_FLAG_STD_TUNING		BIT(5)
/* The IP has SDHCI_CAPABILITIES_1 register */
#define ESDHC_FLAG_HAVE_CAP1		BIT(6)
/*
 * The IP has errata ERR004536
 * uSDHC: ADMA Length Mismatch Error occurs if the AHB read access is slow,
 * when reading data from the card
 */
#define ESDHC_FLAG_ERR004536		BIT(7)
/* The IP supports HS200 mode */
#define ESDHC_FLAG_HS200		BIT(8)
/* The IP supports HS400 mode */
#define ESDHC_FLAG_HS400		BIT(9)


#define IMX_SDHCI_WML		0x44
#define IMX_SDHCI_MIXCTRL	0x48
#define IMX_SDHCI_DLL_CTRL	0x60
#define IMX_SDHCI_MIX_CTRL_FBCLK_SEL	(BIT(25))

struct esdhc_soc_data {
	u32 flags;
};

struct fsl_esdhc_host {
	struct mci_host		mci;
	void __iomem		*regs;
	struct device_d		*dev;
	struct clk		*clk;
	const struct esdhc_soc_data *socdata;
};

#define to_fsl_esdhc(mci)	container_of(mci, struct fsl_esdhc_host, mci)

#define  SDHCI_CMD_ABORTCMD (0xC0 << 16)

static inline int esdhc_is_usdhc(struct fsl_esdhc_host *data)
{
	return !!(data->socdata->flags & ESDHC_FLAG_USDHC);
}


/* Return the XFERTYP flags for a given command and data packet */
static u32 esdhc_xfertyp(struct fsl_esdhc_host *host,
			 struct mci_cmd *cmd, struct mci_data *data)
{
	u32 xfertyp = 0;

	if (data) {
		xfertyp |= COMMAND_DPSEL;

		if (!IS_ENABLED(CONFIG_MCI_IMX_ESDHC_PIO))
			xfertyp |= TRANSFER_MODE_DMAEN;

		if (data->blocks > 1) {
			xfertyp |= TRANSFER_MODE_MSBSEL;
			xfertyp |= TRANSFER_MODE_BCEN;
		}

		if (data->flags & MMC_DATA_READ)
			xfertyp |= TRANSFER_MODE_DTDSEL;
	}

	if (cmd->resp_type & MMC_RSP_CRC)
		xfertyp |= COMMAND_CCCEN;
	if (cmd->resp_type & MMC_RSP_OPCODE)
		xfertyp |= COMMAND_CICEN;
	if (cmd->resp_type & MMC_RSP_136)
		xfertyp |= COMMAND_RSPTYP_136;
	else if (cmd->resp_type & MMC_RSP_BUSY)
		xfertyp |= COMMAND_RSPTYP_48_BUSY;
	else if (cmd->resp_type & MMC_RSP_PRESENT)
		xfertyp |= COMMAND_RSPTYP_48;
	if ((host->socdata->flags & ESDHC_FLAG_MULTIBLK_NO_INT) &&
	    (cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION))
		xfertyp |= SDHCI_CMD_ABORTCMD;

	return COMMAND_CMD(cmd->cmdidx) | xfertyp;
}

/*
 * PIO Read/Write Mode reduce the performace as DMA is not used in this mode.
 */
static int
esdhc_pio_read_write(struct mci_host *mci, struct mci_data *data)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	u32 blocks;
	char *buffer;
	u32 databuf;
	u32 size;
	u32 irqstat;
	u32 timeout;

	if (data->flags & MMC_DATA_READ) {
		blocks = data->blocks;
		buffer = data->dest;
		while (blocks) {
			timeout = PIO_TIMEOUT;
			size = data->blocksize;
			irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);
			while (!(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_BREN)
				&& --timeout);
			if (timeout <= 0) {
				dev_err(host->dev, "Data Read Failed\n");
				return -ETIMEDOUT;
			}
			while (size && (!(irqstat & IRQSTAT_TC))) {
				udelay(100); /* Wait before last byte transfer complete */
				irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);
				databuf = esdhc_read32(regs + SDHCI_BUFFER);
				*((u32 *)buffer) = databuf;
				buffer += 4;
				size -= 4;
			}
			blocks--;
		}
	} else {
		blocks = data->blocks;
		buffer = (char *)data->src;
		while (blocks) {
			timeout = PIO_TIMEOUT;
			size = data->blocksize;
			irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);
			while (!(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_BWEN)
				&& --timeout);
			if (timeout <= 0) {
				dev_err(host->dev, "Data Write Failed\n");
				return -ETIMEDOUT;
			}
			while (size && (!(irqstat & IRQSTAT_TC))) {
				udelay(100); /* Wait before last byte transfer complete */
				databuf = *((u32 *)buffer);
				buffer += 4;
				size -= 4;
				irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);
				esdhc_write32(regs+ SDHCI_BUFFER, databuf);
			}
			blocks--;
		}
	}

	return 0;
}

static int esdhc_setup_data(struct mci_host *mci, struct mci_data *data)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	u32 wml_value;

	if (IS_ENABLED(CONFIG_MCI_IMX_ESDHC_PIO)) {
		if (!(data->flags & MMC_DATA_READ))
			esdhc_write32(regs + SDHCI_DMA_ADDRESS, (u32)data->src);
		else
			esdhc_write32(regs + SDHCI_DMA_ADDRESS, (u32)data->dest);
	} else {
		wml_value = data->blocksize/4;

		if (data->flags & MMC_DATA_READ) {
			if (wml_value > 0x10)
				wml_value = 0x10;

			esdhc_clrsetbits32(regs + IMX_SDHCI_WML, WML_RD_WML_MASK, wml_value);
			esdhc_write32(regs + SDHCI_DMA_ADDRESS, (u32)data->dest);
		} else {
			if (wml_value > 0x80)
				wml_value = 0x80;

			esdhc_clrsetbits32(regs + IMX_SDHCI_WML, WML_WR_WML_MASK,
						wml_value << 16);
			esdhc_write32(regs + SDHCI_DMA_ADDRESS, (u32)data->src);
		}
	}

	esdhc_write32(regs + SDHCI_BLOCK_SIZE__BLOCK_COUNT, data->blocks << 16 | data->blocksize);

	return 0;
}

static int esdhc_do_data(struct mci_host *mci, struct mci_data *data)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	unsigned int num_bytes = data->blocks * data->blocksize;
	u32 irqstat;

	if (IS_ENABLED(CONFIG_MCI_IMX_ESDHC_PIO))
		return esdhc_pio_read_write(mci, data);

	do {
		irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);

		if (irqstat & DATA_ERR)
			return -EIO;

		if (irqstat & IRQSTAT_DTOE)
			return -ETIMEDOUT;
	} while (!(irqstat & IRQSTAT_TC) &&
		(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_DLA));

	if (data->flags & MMC_DATA_WRITE)
		dma_sync_single_for_cpu((unsigned long)data->src,
					num_bytes, DMA_TO_DEVICE);
	else
		dma_sync_single_for_cpu((unsigned long)data->dest,
					num_bytes, DMA_FROM_DEVICE);

	return 0;
}

/*
 * Sends a command out on the bus.  Takes the mci pointer,
 * a command pointer, and an optional data pointer.
 */
static int
esdhc_send_cmd(struct mci_host *mci, struct mci_cmd *cmd, struct mci_data *data)
{
	u32	xfertyp, mixctrl;
	u32	irqstat;
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	unsigned int num_bytes = 0;
	int ret;

	esdhc_write32(regs + SDHCI_INT_STATUS, -1);

	/* Wait at least 8 SD clock cycles before the next command */
	udelay(1);

	/* Set up for a data transfer if we have one */
	if (data) {
		int err;

		err = esdhc_setup_data(mci, data);
		if(err)
			return err;

		num_bytes = data->blocks * data->blocksize;

		if (data->flags & MMC_DATA_WRITE)
			dma_sync_single_for_device((unsigned long)data->src,
						   num_bytes, DMA_TO_DEVICE);
		else
			dma_sync_single_for_device((unsigned long)data->dest,
						   num_bytes, DMA_FROM_DEVICE);

	}

	/* Figure out the transfer arguments */
	xfertyp = esdhc_xfertyp(host, cmd, data);

	/* Send the command */
	esdhc_write32(regs + SDHCI_ARGUMENT, cmd->cmdarg);

	if (esdhc_is_usdhc(host)) {
		/* write lower-half of xfertyp to mixctrl */
		mixctrl = xfertyp & 0xFFFF;
		/* Keep the bits 22-25 of the register as is */
		mixctrl |= (esdhc_read32(regs + IMX_SDHCI_MIXCTRL) & (0xF << 22));
		esdhc_write32(regs + IMX_SDHCI_MIXCTRL, mixctrl);
	}

	esdhc_write32(regs + SDHCI_TRANSFER_MODE__COMMAND, xfertyp);

	/* Wait for the command to complete */
	ret = wait_on_timeout(100 * MSECOND,
			esdhc_read32(regs + SDHCI_INT_STATUS) & IRQSTAT_CC);
	if (ret) {
		dev_dbg(host->dev, "timeout 1\n");
		return -ETIMEDOUT;
	}

	irqstat = esdhc_read32(regs + SDHCI_INT_STATUS);
	esdhc_write32(regs + SDHCI_INT_STATUS, irqstat);

	if (irqstat & CMD_ERR)
		return -EIO;

	if (irqstat & IRQSTAT_CTOE)
		return -ETIMEDOUT;

	/* Workaround for ESDHC errata ENGcm03648 / ENGcm12360 */
	if (!data && (cmd->resp_type & MMC_RSP_BUSY)) {
		/*
		 * Poll on DATA0 line for cmd with busy signal for
		 * timout / 10 usec since DLA polling can be insecure.
		 */
		ret = wait_on_timeout(2500 * MSECOND,
			(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_DAT0));

		if (ret) {
			dev_err(host->dev, "timeout PRSSTAT_DAT0\n");
			return -ETIMEDOUT;
		}
	}

	/* Copy the response to the response buffer */
	if (cmd->resp_type & MMC_RSP_136) {
		u32 cmdrsp3, cmdrsp2, cmdrsp1, cmdrsp0;

		cmdrsp3 = esdhc_read32(regs + SDHCI_RESPONSE_3);
		cmdrsp2 = esdhc_read32(regs + SDHCI_RESPONSE_2);
		cmdrsp1 = esdhc_read32(regs + SDHCI_RESPONSE_1);
		cmdrsp0 = esdhc_read32(regs + SDHCI_RESPONSE_0);
		cmd->response[0] = (cmdrsp3 << 8) | (cmdrsp2 >> 24);
		cmd->response[1] = (cmdrsp2 << 8) | (cmdrsp1 >> 24);
		cmd->response[2] = (cmdrsp1 << 8) | (cmdrsp0 >> 24);
		cmd->response[3] = (cmdrsp0 << 8);
	} else
		cmd->response[0] = esdhc_read32(regs + SDHCI_RESPONSE_0);

	/* Wait until all of the blocks are transferred */
	if (data) {
		ret = esdhc_do_data(mci, data);
		if (ret)
			return ret;
	}

	esdhc_write32(regs + SDHCI_INT_STATUS, -1);

	/* Wait for the bus to be idle */
	ret = wait_on_timeout(SECOND,
			!(esdhc_read32(regs + SDHCI_PRESENT_STATE) &
				(PRSSTAT_CICHB | PRSSTAT_CIDHB)));
	if (ret) {
		dev_err(host->dev, "timeout 2\n");
		return -ETIMEDOUT;
	}

	ret = wait_on_timeout(100 * MSECOND,
			!(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_DLA));
	if (ret) {
		dev_err(host->dev, "timeout 3\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void set_sysctl(struct mci_host *mci, u32 clock)
{
	int div, pre_div;
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	int sdhc_clk = clk_get_rate(host->clk);
	u32 clk;
	unsigned long  cur_clock;

	/*
	 * With eMMC and imx53 (sdhc_clk=200MHz) a pre_div of 1 results in
	 *	pre_div=1,div=4 (=50MHz)
	 * which is valid and should work, but somehow doesn't.
	 * Starting with pre_div=2 gives
	 *	pre_div=2, div=2 (=50MHz)
	 * and works fine.
	 */
	pre_div = 2;

	if (sdhc_clk == clock)
		pre_div = 1;
	else if (sdhc_clk / 16 > clock)
		for (; pre_div < 256; pre_div *= 2)
			if ((sdhc_clk / pre_div) <= (clock * 16))
				break;

	for (div = 1; div <= 16; div++)
		if ((sdhc_clk / (div * pre_div)) <= clock)
			break;

	cur_clock = sdhc_clk / pre_div / div;

	dev_dbg(host->dev, "set clock: wanted: %d got: %ld\n", clock, cur_clock);
	dev_dbg(host->dev, "pre_div: %d div: %d\n", pre_div, div);

	/* the register values start with 0 */
	div -= 1;
	pre_div >>= 1;

	clk = (pre_div << 8) | (div << 4);

	esdhc_clrbits32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_CKEN);

	esdhc_clrsetbits32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_CLOCK_MASK, clk);

	wait_on_timeout(10 * MSECOND,
			esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_SDSTB);

	clk = SYSCTL_PEREN | SYSCTL_CKEN;

	esdhc_setbits32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			clk);
}

static void esdhc_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;

	/* Set the clock speed */
	set_sysctl(mci, ios->clock);

	/* Set the bus width */
	esdhc_clrbits32(regs + SDHCI_HOST_CONTROL__POWER_CONTROL__BLOCK_GAP_CONTROL,
			PROCTL_DTW_4 | PROCTL_DTW_8);

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_4:
		esdhc_setbits32(regs + SDHCI_HOST_CONTROL__POWER_CONTROL__BLOCK_GAP_CONTROL,
				PROCTL_DTW_4);
		break;
	case MMC_BUS_WIDTH_8:
		esdhc_setbits32(regs + SDHCI_HOST_CONTROL__POWER_CONTROL__BLOCK_GAP_CONTROL,
				PROCTL_DTW_8);
		break;
	case MMC_BUS_WIDTH_1:
		break;
	default:
		return;
	}

}

static int esdhc_card_present(struct mci_host *mci)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	struct esdhc_platform_data *pdata = host->dev->platform_data;
	int ret;

	if (!pdata)
		return 1;

	switch (pdata->cd_type) {
	case ESDHC_CD_NONE:
	case ESDHC_CD_PERMANENT:
		return 1;
	case ESDHC_CD_CONTROLLER:
		return !(esdhc_read32(regs + SDHCI_PRESENT_STATE) & PRSSTAT_WPSPL);
	case ESDHC_CD_GPIO:
		ret = gpio_direction_input(pdata->cd_gpio);
		if (ret)
			return ret;
		return gpio_get_value(pdata->cd_gpio) ? 0 : 1;
	}

	return 0;
}

static int esdhc_init(struct mci_host *mci, struct device_d *dev)
{
	struct fsl_esdhc_host *host = to_fsl_esdhc(mci);
	void __iomem *regs = host->regs;
	int timeout = 1000;
	int ret = 0;

	/* Reset the entire host controller */
	esdhc_write32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_RSTA);

	/* Wait until the controller is available */
	while ((esdhc_read32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET)
				& SYSCTL_RSTA) && --timeout)
		udelay(1000);

	esdhc_write32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_HCKEN | SYSCTL_IPGEN);

	/* RSTA doesn't reset MMC_BOOT register, so manually reset it */
	esdhc_write32(regs + SDHCI_MMC_BOOT, 0);

	/* Set the initial clock speed */
	set_sysctl(mci, 400000);

	writel(IRQSTATEN_CC | IRQSTATEN_TC | IRQSTATEN_CINT | IRQSTATEN_CTOE |
			IRQSTATEN_CCE | IRQSTATEN_CEBE | IRQSTATEN_CIE | IRQSTATEN_DTOE |
			IRQSTATEN_DCE | IRQSTATEN_DEBE | IRQSTATEN_DINT, regs + SDHCI_INT_ENABLE);

	/* Put the PROCTL reg back to the default */
	esdhc_write32(regs + SDHCI_HOST_CONTROL__POWER_CONTROL__BLOCK_GAP_CONTROL,
			PROCTL_INIT);

	/* Set timout to the maximum value */
	esdhc_clrsetbits32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_TIMEOUT_MASK, 14 << 16);

	return ret;
}

static int esdhc_reset(struct fsl_esdhc_host *host)
{
	void __iomem *regs = host->regs;
	uint64_t start;
	int val;

	/* reset the controller */
	esdhc_write32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET,
			SYSCTL_RSTA);

	/* extra register reset for i.MX6 Solo/DualLite */
	if (esdhc_is_usdhc(host)) {
		/* reset bit FBCLK_SEL */
		val = esdhc_read32(regs + IMX_SDHCI_MIXCTRL);
		val &= ~IMX_SDHCI_MIX_CTRL_FBCLK_SEL;
		esdhc_write32(regs + IMX_SDHCI_MIXCTRL, val);

		/* reset delay line settings in IMX_SDHCI_DLL_CTRL */
		esdhc_write32(regs + IMX_SDHCI_DLL_CTRL, 0x0);
	}

	start = get_time_ns();
	/* hardware clears the bit when it is done */
	while (1) {
		if (!(esdhc_read32(regs + SDHCI_CLOCK_CONTROL__TIMEOUT_CONTROL__SOFTWARE_RESET)
					& SYSCTL_RSTA))
			break;
		if (is_timeout(start, 100 * MSECOND)) {
			dev_err(host->dev, "Reset never completed.\n");
			return -EIO;
		}
	}

	return 0;
}

static int fsl_esdhc_detect(struct device_d *dev)
{
	struct fsl_esdhc_host *host = dev->priv;

	return mci_detect_card(&host->mci);
}

static struct esdhc_soc_data esdhc_imx25_data;
static struct esdhc_soc_data esdhc_imx53_data;

static int fsl_esdhc_probe(struct device_d *dev)
{
	struct resource *iores;
	struct fsl_esdhc_host *host;
	struct mci_host *mci;
	u32 caps;
	int ret;
	unsigned long rate;
	struct esdhc_platform_data *pdata = dev->platform_data;

	host = xzalloc(sizeof(*host));
	mci = &host->mci;

	if (IS_ENABLED(CONFIG_OFDEVICE)) {
		host->socdata = of_device_get_match_data(dev);
		if (!host->socdata)
			return -EINVAL;
	} else {
		if (cpu_is_mx50() || cpu_is_mx51() || cpu_is_mx53())
			host->socdata = &esdhc_imx53_data;
		else
			host->socdata = &esdhc_imx25_data;
	}

	host->clk = clk_get(dev, "per");
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	ret = clk_enable(host->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock: %s\n",
			strerror(ret));
		return ret;
	}

	host->dev = dev;
	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	host->regs = IOMEM(iores->start);

	/* First reset the eSDHC controller */
	ret = esdhc_reset(host);
	if (ret) {
		free(host);
		return ret;
	}

	caps = esdhc_read32(host->regs + SDHCI_CAPABILITIES);

	if (caps & ESDHC_HOSTCAPBLT_VS18)
		mci->voltages |= MMC_VDD_165_195;
	if (caps & ESDHC_HOSTCAPBLT_VS30)
		mci->voltages |= MMC_VDD_29_30 | MMC_VDD_30_31;
	if (caps & ESDHC_HOSTCAPBLT_VS33)
		mci->voltages |= MMC_VDD_32_33 | MMC_VDD_33_34;

	if (pdata) {
		mci->host_caps = pdata->caps;
		if (pdata->devname)
			mci->devname = pdata->devname;
	} else if (dev->device_node) {
		const char *alias = of_alias_get(dev->device_node);
		if (alias)
			mci->devname = xstrdup(alias);
	}

	if (caps & ESDHC_HOSTCAPBLT_HSS)
		mci->host_caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;

	host->mci.send_cmd = esdhc_send_cmd;
	host->mci.set_ios = esdhc_set_ios;
	host->mci.init = esdhc_init;
	host->mci.card_present = esdhc_card_present;
	host->mci.hw_dev = dev;

	dev->detect = fsl_esdhc_detect,

	rate = clk_get_rate(host->clk);
	host->mci.f_min = rate >> 12;
	if (host->mci.f_min < 200000)
		host->mci.f_min = 200000;
	host->mci.f_max = rate;
	if (pdata) {
		host->mci.use_dsr = pdata->use_dsr;
		host->mci.dsr_val = pdata->dsr_val;
	}

	mci_of_parse(&host->mci);

	dev->priv = host;

	return mci_register(&host->mci);
}

static struct esdhc_soc_data esdhc_imx25_data = {
	.flags = ESDHC_FLAG_ENGCM07207,
};

static struct esdhc_soc_data esdhc_imx53_data = {
	.flags = ESDHC_FLAG_MULTIBLK_NO_INT,
};

static struct esdhc_soc_data usdhc_imx6q_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_MAN_TUNING,
};

static struct esdhc_soc_data usdhc_imx6sl_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
	       | ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_ERR004536
	       | ESDHC_FLAG_HS200,
};

static struct esdhc_soc_data usdhc_imx6sx_data = {
	.flags = ESDHC_FLAG_USDHC | ESDHC_FLAG_STD_TUNING
	       | ESDHC_FLAG_HAVE_CAP1 | ESDHC_FLAG_HS200,
};

static __maybe_unused struct of_device_id fsl_esdhc_compatible[] = {
	{ .compatible = "fsl,imx25-esdhc",  .data = &esdhc_imx25_data  },
	{ .compatible = "fsl,imx50-esdhc",  .data = &esdhc_imx53_data  },
	{ .compatible = "fsl,imx51-esdhc",  .data = &esdhc_imx53_data  },
	{ .compatible = "fsl,imx53-esdhc",  .data = &esdhc_imx53_data  },
	{ .compatible = "fsl,imx6q-usdhc",  .data = &usdhc_imx6q_data  },
	{ .compatible = "fsl,imx6sl-usdhc", .data = &usdhc_imx6sl_data },
	{ .compatible = "fsl,imx6sx-usdhc", .data = &usdhc_imx6sx_data },
	{ /* sentinel */ }
};

static struct driver_d fsl_esdhc_driver = {
	.name  = "imx-esdhc",
	.probe = fsl_esdhc_probe,
	.of_compatible = DRV_OF_COMPAT(fsl_esdhc_compatible),
};
device_platform_driver(fsl_esdhc_driver);
