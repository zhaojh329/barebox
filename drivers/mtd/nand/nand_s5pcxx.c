/*
 * Copyright (C) 2017 Jianhui Zhao
 *
 * Based on the original driver by Alexey Galakhov
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <config.h>
#include <common.h>
#include <driver.h>
#include <malloc.h>
#include <io.h>
#include <init.h>
#include <asm-generic/errno.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <mach/s5pcxx-nand.h>

#define NFCONF	0x00
#define NFCONT	0x04
#define NFCMMD	0x08
#define NFADDR	0x0C
#define NFDATA	0x10
#define NFSTAT	0x28
#define NFMECC	0x34

#define NFCONT_EN			(1)
#define NFCONT_nCE0			(1 << 1)
#define NFCONT_MECCINIT		(1 << 5)
#define NFCONT_MECCLOCK		(1 << 7)
#define NFCONT_MECCDIRWR 	(1 << 18)

#define NFSTAT_BUSY			(1)

#define NFECCBASE		0x20000
#define NFECCCONF		(NFECCBASE + 0x00)
#define NFECCCONT		(NFECCBASE + 0x20)
#define NFECCSTAT		(NFECCBASE + 0x30)
#define NFECCSECSTAT	(NFECCBASE + 0x40)
#define NFECCPRGECC		(NFECCBASE + 0x90)
#define NFECCERL		(NFECCBASE + 0xC0)
#define NFECCERP		(NFECCBASE + 0xF0)

#define NFECCCONT_MECCRESET	(1 << 0)
#define NFECCCONT_MECCINIT	(1 << 2)
#define NFECCCONT_ECCDIRWR	(1 << 16)

#define NFECCSTAT_ECCBUSY	(1 << 31)

struct s5pcxx_nand_host {
	struct mtd_info		mtd;
	struct nand_chip	nand;
	struct device_d		*dev;
	void __iomem		*base;
};

/*
 * See "S5PV210 iROM Application Note" for recommended ECC layout
 * ECC layout for 8-bit ECC (13 bytes/page)
 * Compatible with bl0 bootloader, see iROM appnote
 */
static struct nand_ecclayout nand_oob_64 = {
	.eccbytes = 52,
	.eccpos = {
		   12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
		   25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
		   38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
		   51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
		  },
	.oobfree = { { .offset = 2, .length = 10 } }
};


/**
 * Enable the NAND flash controller
 * @param[in] host Base address of the NAND controller
 * @param[in] timing Timing to access the NAND memory
 */
static void enable_nand_controller(void __iomem *host, uint32_t timing)
{
	writel(NFCONT_EN | NFCONT_nCE0, host + NFCONT);
	writel(timing, host + NFCONF);
}

/**
 * Diable the NAND flash controller
 * @param[in] host Base address of the NAND controller
 */
static void disable_nand_controller(void __iomem *host)
{
	writel(NFCONT_nCE0, host + NFCONT);
}

static void s5pcxx_nand_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s5pcxx_nand_host *host = nand_chip->priv;

	if (cmd == NAND_CMD_NONE)
		return;
	/* If the CLE should be active, this call is a NAND command */
	if (ctrl & NAND_CLE)
		writeb(cmd, host->base + NFCMMD);
	/* If the ALE should be active, this call is a NAND address */
	if (ctrl & NAND_ALE)
		writeb(cmd, host->base + NFADDR);
}

static void s5pcxx_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s5pcxx_nand_host *host = nand_chip->priv;

	uint32_t reg = readl(host->base + NFCONT);
	reg |= NFCONT_nCE0;
	switch (chip) {
	case 0:
		reg &= ~NFCONT_nCE0;
		break;
	default:
		break;
	}
	writel(reg, host->base + NFCONT);
}

static int s5pcxx_nand_devready(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct s5pcxx_nand_host *host = nand_chip->priv;

	return readl(host->base + NFSTAT) & NFSTAT_BUSY;
}

static inline void rwl(void __iomem *reg, uint32_t rst, uint32_t set)
{
	uint32_t r;
	r = readl(reg);
	r &= ~rst;
	r |= set;
	writel(r, reg);
}

static void s5pcxx_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct nand_chip *chip = mtd->priv;
	struct s5pcxx_nand_host *host = chip->priv;

	uint32_t reg;

	/* Set ECC mode */
	reg = 3; /* 8-bit */
	reg |= (chip->ecc.size - 1) << 16;
	writel(reg, host->base + NFECCCONF);

	/* Set ECC direction */
	rwl(host->base + NFECCCONT, NFECCCONT_ECCDIRWR,
	    (mode == NAND_ECC_WRITE) ? NFECCCONT_ECCDIRWR : 0);

	/* Reset status bits */
	rwl(host->base + NFECCSTAT, 0, (1 << 24) | (1 << 25));

	/* Unlock ECC */
	rwl(host->base + NFCONT, NFCONT_MECCLOCK, 0);

	/* Initialize ECC */
	rwl(host->base + NFECCCONT, 0, NFECCCONT_MECCINIT);
}

static void wait_for_bit(struct mtd_info *mtd, void __iomem *reg, uint32_t bit)
{
	uint64_t start = get_time_ns();
	
	do {
		if (readl(reg) & bit)
			return;
	} while (!is_timeout(start, 100 * USECOND));

	dev_err(mtd->parent, "Timed out!\n");
}

static void readecc(void __iomem *eccbase, uint8_t *ecc_code, unsigned ecc_len)
{
	unsigned i, j;
	for (i = 0; i < ecc_len; i += 4) {
		uint32_t reg = readl(eccbase + i);
		for (j = 0; (j < 4) && (i + j < ecc_len); ++j) {
			ecc_code[i + j] = reg & 0xFF;
			reg >>= 8;
		}
	}
}

static int s5pcxx_nand_calculate_ecc(struct mtd_info *mtd, const uint8_t *dat, uint8_t *ecc_code)
{
	struct nand_chip *chip = mtd->priv;
	struct s5pcxx_nand_host *host = chip->priv;

	/* Lock ECC */
	rwl(host->base + NFCONT, 0, NFCONT_MECCLOCK);

	wait_for_bit(mtd, host->base + NFECCSTAT, (1 << 25));

	readecc(host->base + NFECCPRGECC, ecc_code, chip->ecc.bytes);

	return 0;
}

/*
 * S3C error correction hardware requires reading ECC
 * See "S5PV210 iROM Application Note"
 */
#define NF8_ReadPage_Adv(a,b,c) (((int(*)(u32, u32, u8*))(*((u32 *)0xD0037F90)))(a,b,c))
static int s5pcxx_nand_read_page_hwecc(struct mtd_info *mtd, struct nand_chip *chip,
				uint8_t *buf, int oob_required, int page)
{
	u32 pages_per_block = mtd->erasesize / mtd->writesize;
	return NF8_ReadPage_Adv(page / pages_per_block, page % pages_per_block, buf);
}

static int s5pcxx_nand_inithw(struct s5pcxx_nand_host *host)
{
	struct s5pcxx_nand_platform_data *pdata = host->dev->platform_data;
	uint32_t tmp;

	/* reset the NAND controller */
	disable_nand_controller(host->base);

	if (pdata != NULL)
		tmp = pdata->nand_timing;
	else
		/* else slowest possible timing */
		tmp = CALC_NFCONF_TIMING(4, 8, 8);

	/* reenable the NAND controller */
	enable_nand_controller(host->base, tmp);

	return 0;
}

static int s5pcxx_nand_probe(struct device_d *dev)
{
	struct nand_chip *chip;
	struct s5pcxx_nand_platform_data *pdata = dev->platform_data;
	struct mtd_info *mtd;
	struct s5pcxx_nand_host *host;
	int ret;

	/* Allocate memory for MTD device structure and private data */
	host = kzalloc(sizeof(struct s5pcxx_nand_host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = dev;
	host->base = dev_request_mem_region(dev, 0);

	/* structures must be linked */
	chip = &host->nand;
	mtd = &host->mtd;
	mtd->priv = chip;
	mtd->parent = dev;

	/* 50 us command delay time */
	chip->chip_delay = 50;
	chip->priv = host;

	chip->IO_ADDR_R = chip->IO_ADDR_W = host->base + NFDATA;

	chip->cmd_ctrl = s5pcxx_nand_hwcontrol;
	chip->dev_ready = s5pcxx_nand_devready;
	chip->select_chip = s5pcxx_nand_select_chip;

	/* we are using the hardware ECC feature of this device */
	chip->ecc.calculate = s5pcxx_nand_calculate_ecc;
	chip->ecc.hwctl = s5pcxx_nand_enable_hwecc;
	chip->ecc.read_page = s5pcxx_nand_read_page_hwecc;

	chip->ecc.mode = NAND_ECC_HW;
	chip->ecc.size = 512;
	chip->ecc.bytes = 13;
	chip->ecc.layout = &nand_oob_64;

	if (pdata->flash_bbt) {
		/* use a flash based bbt */
		chip->options |= NAND_BBT_USE_FLASH;
	}

	ret = s5pcxx_nand_inithw(host);
	if (ret)
		goto on_error;

	/* Scan to find existence of the device */
	ret = nand_scan(mtd, 1);
	if (ret)
		goto on_error;

	return add_mtd_nand_device(mtd, "nand");

on_error:
	free(host);
	return ret;
}

static struct driver_d s5pcxx_nand_driver = {
	.name  = "s5pcxx_nand",
	.probe = s5pcxx_nand_probe,
};
device_platform_driver(s5pcxx_nand_driver);
