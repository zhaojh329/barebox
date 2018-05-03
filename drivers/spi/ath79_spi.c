/*
 * Copyright (C) 2013, 2014 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This file is part of barebox.
 * See file CREDITS for list of people who contributed to this project.
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

#include <common.h>
#include <init.h>
#include <driver.h>
#include <spi/spi.h>
#include <io.h>
#include <clock.h>
#include <linux/clk.h>

#define ATH79_SPI_RRW_DELAY_FACTOR  12000
#define MHZ             (1000 * 1000)

struct ath79_spi {
    struct spi_master   master;
    u32                 ioc_base;
    u32                 reg_ctrl;
    void __iomem        *base;
    unsigned            rrw_delay;
};

#define AR71XX_SPI_REG_FS   0x00    /* Function Select */
#define AR71XX_SPI_REG_CTRL 0x04    /* SPI Control */
#define AR71XX_SPI_REG_IOC  0x08    /* SPI I/O Control */
#define AR71XX_SPI_REG_RDS  0x0c    /* Read Data Shift */

#define AR71XX_SPI_FS_GPIO  BIT(0)  /* Enable GPIO mode */

#define AR71XX_SPI_IOC_DO   BIT(0)  /* Data Out pin */
#define AR71XX_SPI_IOC_CLK  BIT(8)  /* CLK pin */
#define AR71XX_SPI_IOC_CS(n)    BIT(16 + (n))
#define AR71XX_SPI_IOC_CS0  AR71XX_SPI_IOC_CS(0)
#define AR71XX_SPI_IOC_CS1  AR71XX_SPI_IOC_CS(1)
#define AR71XX_SPI_IOC_CS2  AR71XX_SPI_IOC_CS(2)
#define AR71XX_SPI_IOC_CS_ALL   (AR71XX_SPI_IOC_CS0 | AR71XX_SPI_IOC_CS1 | \
                 AR71XX_SPI_IOC_CS2)

static inline u32 ath79_spi_rr(struct ath79_spi *sp, unsigned reg)
{
    return __raw_readl(sp->base + reg);
}

static inline void ath79_spi_wr(struct ath79_spi *sp, unsigned reg, u32 val)
{
    __raw_writel(val, sp->base + reg);
}

static inline struct ath79_spi *ath79_spidev_to_sp(struct spi_device *spi)
{
    return container_of(spi->master, struct ath79_spi, master);
}

static inline void ath79_spi_delay(struct ath79_spi *sp, unsigned nsecs)
{
    if (nsecs > sp->rrw_delay)
        ndelay(nsecs - sp->rrw_delay);
}

static void ath79_spi_chipselect(struct spi_device *spi, int is_active)
{
    struct ath79_spi *sp = ath79_spidev_to_sp(spi);
    int cs_high = (spi->mode & SPI_CS_HIGH) ? is_active : !is_active;
    u32 cs_bit = AR71XX_SPI_IOC_CS(spi->chip_select);

    if (is_active) {
        /* set initial clock polarity */
        if (spi->mode & SPI_CPOL)
            sp->ioc_base |= AR71XX_SPI_IOC_CLK;
        else
            sp->ioc_base &= ~AR71XX_SPI_IOC_CLK;

        ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);
    }

    if (cs_high)
        sp->ioc_base |= cs_bit;
    else
        sp->ioc_base &= ~cs_bit;

    ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);
}

static void ath79_spi_enable(struct ath79_spi *sp)
{
    /* enable GPIO mode */
    ath79_spi_wr(sp, AR71XX_SPI_REG_FS, AR71XX_SPI_FS_GPIO);

    /* save CTRL register */
    sp->reg_ctrl = ath79_spi_rr(sp, AR71XX_SPI_REG_CTRL);
    sp->ioc_base = ath79_spi_rr(sp, AR71XX_SPI_REG_IOC);

    /* disable remap */
    sp->reg_ctrl |= 1 << 6;
    ath79_spi_wr(sp, AR71XX_SPI_REG_CTRL, sp->reg_ctrl);
}

static void ath79_spi_disable(struct ath79_spi *sp)
{
    /* restore CTRL register */
    ath79_spi_wr(sp, AR71XX_SPI_REG_CTRL, sp->reg_ctrl);
    /* disable GPIO mode */
    ath79_spi_wr(sp, AR71XX_SPI_REG_FS, 0);
}

static int ath79_spi_setup_cs(struct spi_device *spi)
{
    struct ath79_spi *sp = ath79_spidev_to_sp(spi);
    u32 cs_bit = AR71XX_SPI_IOC_CS(spi->chip_select);

    if (spi->mode & SPI_CS_HIGH)
        sp->ioc_base &= ~cs_bit;
    else
        sp->ioc_base |= cs_bit;

    ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);

    return 0;
}

static int ath79_spi_setup(struct spi_device *spi)
{
    struct spi_master *master = spi->master;
    struct device_d spi_dev = spi->dev;

    if (spi->bits_per_word != 8) {
        dev_err(master->dev, "master doesn't support %d bits per word requested by %s\n",
            spi->bits_per_word, spi_dev.name);
        return -EINVAL;
    }

    if ((spi->mode & (SPI_CPHA | SPI_CPOL)) != SPI_MODE_0) {
        dev_err(master->dev, "master doesn't support SPI_MODE%d requested by %s\n",
            spi->mode & (SPI_CPHA | SPI_CPOL), spi_dev.name);
        return -EINVAL;
    }

    ath79_spi_setup_cs(spi);

    return 0;
}

static u32 ath79_spi_txrx_mode0(struct spi_device *spi, unsigned nsecs,
                   u32 word, u8 bits)
{
    struct ath79_spi *sp = ath79_spidev_to_sp(spi);
    u32 ioc = sp->ioc_base;

    /* clock starts at inactive polarity */
    for (word <<= (32 - bits); likely(bits); bits--) {
        u32 out;

        if (word & (1 << 31))
            out = ioc | AR71XX_SPI_IOC_DO;
        else
            out = ioc & ~AR71XX_SPI_IOC_DO;

        /* setup MSB (to slave) on trailing edge */
        ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, out);
        ath79_spi_delay(sp, nsecs);
        ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, out | AR71XX_SPI_IOC_CLK);
        ath79_spi_delay(sp, nsecs);
        if (bits == 1)
            ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, out);

        word <<= 1;
    }

    return ath79_spi_rr(sp, AR71XX_SPI_REG_RDS);
}

static int ath79_spi_read(struct spi_device *spi, void *buf, size_t nbyte)
{
    ssize_t cnt = 0;
    u8 *rxf_buf = buf;

    while (cnt < nbyte) {
        *rxf_buf = ath79_spi_txrx_mode0(spi, 1000, 0, 8);
        rxf_buf++;
        cnt++;
    }

    return cnt;
}

static int ath79_spi_write(struct spi_device *spi,
                const void *buf, size_t nbyte)
{
    ssize_t cnt = 0;
    const u8 *txf_buf = buf;

    while (cnt < nbyte) {
        ath79_spi_txrx_mode0(spi, 1000, (u32)*txf_buf, 8);
        txf_buf++;
        cnt++;
    }

    return 0;
}

static int ath79_spi_transfer(struct spi_device *spi, struct spi_message *mesg)
{
    struct spi_transfer *t;

    mesg->actual_length = 0;

    /* activate chip select signal */
    ath79_spi_chipselect(spi, 1);

    list_for_each_entry(t, &mesg->transfers, transfer_list) {

        if (t->tx_buf)
            ath79_spi_write(spi, t->tx_buf, t->len);

        if (t->rx_buf)
            ath79_spi_read(spi, t->rx_buf, t->len);

        mesg->actual_length += t->len;
    }

    /* inactivate chip select signal */
    ath79_spi_chipselect(spi, 0);

    return 0;
}

static bool ath79_spi_flash_read_supported(struct spi_device *spi)
{
    if (spi->chip_select)
        return false;

    return true;
}

static int ath79_spi_read_flash_data(struct spi_device *spi,
                     struct spi_flash_read_message *msg)
{
    struct ath79_spi *sp = ath79_spidev_to_sp(spi);

    if (msg->addr_width > 3)
        return -EOPNOTSUPP;

    /* disable GPIO mode */
    ath79_spi_wr(sp, AR71XX_SPI_REG_FS, 0);

    memcpy(msg->buf, sp->base + msg->from, msg->len);

    /* enable GPIO mode */
    ath79_spi_wr(sp, AR71XX_SPI_REG_FS, AR71XX_SPI_FS_GPIO);

    /* restore IOC register */
    ath79_spi_wr(sp, AR71XX_SPI_REG_IOC, sp->ioc_base);

    msg->retlen = msg->len;

    return 0;
}

static int ath79_spi_probe(struct device_d *dev)
{
    struct resource *iores;
    struct spi_master *master;
    struct ath79_spi *sp;
    struct clk *clk;
    unsigned long rate;

    sp = xzalloc(sizeof(*sp));
    dev->priv = sp;

    master = &sp->master;
    master->dev = dev;

    master->bus_num = dev->id;
    master->setup = ath79_spi_setup;
    master->transfer = ath79_spi_transfer;
    master->num_chipselect = 3;

    master->spi_flash_read = ath79_spi_read_flash_data;
    master->flash_read_supported = ath79_spi_flash_read_supported;

    if (IS_ENABLED(CONFIG_OFDEVICE)) {
        struct device_node *node = dev->device_node;
        u32 num_cs;
        int ret;

        ret = of_property_read_u32(node, "num-chipselects", &num_cs);
        if (ret)
            num_cs = 3;

        if (num_cs > 3) {
            dev_err(dev, "master doesn't support num-chipselects > 3\n");
        }

        master->num_chipselect = num_cs;
    }

    iores = dev_request_mem_resource(dev, 0);
    if (IS_ERR(iores))
        return PTR_ERR(iores);
    sp->base = IOMEM(iores->start);

    clk = clk_get(dev, "ahb");
    if (IS_ERR(clk)) {
        dev_err(dev, "could not get clk: ahb\n");
        return  PTR_ERR(clk);
    }

    rate = DIV_ROUND_UP(clk_get_rate(clk), MHZ);
    sp->rrw_delay = ATH79_SPI_RRW_DELAY_FACTOR / rate;

    /* enable gpio mode */
    ath79_spi_enable(sp);

    spi_register_master(master);

    return 0;
}

static void ath79_spi_remove(struct device_d *dev)
{
    struct ath79_spi *sp = dev->priv;

    ath79_spi_disable(sp);
}

static __maybe_unused struct of_device_id ath79_spi_dt_ids[] = {
    {
        .compatible = "qca,ar7100-spi",
    },
    {
        /* sentinel */
    }
};

static struct driver_d ath79_spi_driver = {
    .name  = "ath79-spi",
    .probe = ath79_spi_probe,
    .remove = ath79_spi_remove,
    .of_compatible = DRV_OF_COMPAT(ath79_spi_dt_ids),
};
device_platform_driver(ath79_spi_driver);
