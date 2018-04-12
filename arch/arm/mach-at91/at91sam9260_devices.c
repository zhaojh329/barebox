/*
 * arch/arm/mach-at91/at91sam9260_devices.c
 *
 *  Copyright (C) 2006 Atmel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <common.h>
#include <init.h>
#include <linux/sizes.h>
#include <gpio.h>
#include <asm/armlinux.h>
#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/at91_pmc.h>
#include <mach/at91sam9260_matrix.h>
#include <mach/at91sam9_sdramc.h>
#include <mach/at91_rtt.h>
#include <mach/iomux.h>
#include <mach/io.h>
#include <mach/cpu.h>
#include <i2c/i2c-gpio.h>

#include "generic.h"

void at91_add_device_sdram(u32 size)
{
	if (!size)
		size = at91sam9260_get_sdram_size();

	arm_add_mem_device("ram0", AT91_CHIPSELECT_1, size);
	if (cpu_is_at91sam9g20()) {
		add_mem_device("sram0", AT91SAM9G20_SRAM_BASE,
			AT91SAM9G20_SRAM_SIZE, IORESOURCE_MEM_WRITEABLE);
	} else {
		add_mem_device("sram0", AT91SAM9260_SRAM_BASE,
			AT91SAM9260_SRAM_SIZE, IORESOURCE_MEM_WRITEABLE);
	}
}

#if defined(CONFIG_USB_OHCI)
void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data)
{
	int i;

	if (!data)
		return;

	/* Enable VBus control for UHP ports */
	for (i = 0; i < data->ports; i++) {
		if (gpio_is_valid(data->vbus_pin[i]))
			at91_set_gpio_output(data->vbus_pin[i],
					     data->vbus_pin_active_low[i]);
	}

	add_generic_device("at91_ohci", DEVICE_ID_DYNAMIC, NULL, AT91SAM9260_UHP_BASE,
			1024 * 1024, IORESOURCE_MEM, data);
}
#else
void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  USB Device (Gadget)
 * -------------------------------------------------------------------- */

#ifdef CONFIG_USB_GADGET_DRIVER_AT91
void __init at91_add_device_udc(struct at91_udc_data *data)
{
	if (gpio_is_valid(data->vbus_pin)) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
	}

	add_generic_device("at91_udc", DEVICE_ID_DYNAMIC, NULL, AT91SAM9260_BASE_UDP,
			SZ_16K,  IORESOURCE_MEM, data);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif

#if defined(CONFIG_DRIVER_NET_MACB)
void at91_add_device_eth(int id, struct macb_platform_data *data)
{
	if (!data)
		return;

	/* Pins used for MII and RMII */
	at91_set_A_periph(AT91_PIN_PA19, 0);	/* ETXCK_EREFCK */
	at91_set_A_periph(AT91_PIN_PA17, 0);	/* ERXDV */
	at91_set_A_periph(AT91_PIN_PA14, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PA15, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PA18, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PA16, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PA12, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PA13, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PA21, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PA20, 0);	/* EMDC */

	if (data->phy_interface != PHY_INTERFACE_MODE_RMII) {
		at91_set_B_periph(AT91_PIN_PA28, 0);	/* ECRS */
		at91_set_B_periph(AT91_PIN_PA29, 0);	/* ECOL */
		at91_set_B_periph(AT91_PIN_PA25, 0);	/* ERX2 */
		at91_set_B_periph(AT91_PIN_PA26, 0);	/* ERX3 */
		at91_set_B_periph(AT91_PIN_PA27, 0);	/* ERXCK */
		if (data->flags & AT91SAM_ETX2_ETX3_ALTERNATIVE) {
			at91_set_B_periph(AT91_PIN_PA10, 0);	/* ETX2 */
			at91_set_B_periph(AT91_PIN_PA11, 0);	/* ETX3 */
		} else {
			at91_set_B_periph(AT91_PIN_PA23, 0);	/* ETX2 */
			at91_set_B_periph(AT91_PIN_PA24, 0);	/* ETX3 */
		}
		at91_set_B_periph(AT91_PIN_PA22, 0);	/* ETXER */
	}

	add_generic_device("macb", 0, NULL, AT91SAM9260_BASE_EMAC, 0x1000,
			   IORESOURCE_MEM, data);
}
#else
void at91_add_device_eth(int id, struct macb_platform_data *data) {}
#endif

#if defined(CONFIG_NAND_ATMEL)
static struct resource nand_resources[] = {
	[0] = {
		.start	= AT91_CHIPSELECT_3,
		.end	= AT91_CHIPSELECT_3 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_BASE_SYS + AT91_ECC,
		.end	= AT91_BASE_SYS + AT91_ECC + 512 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

void at91_add_device_nand(struct atmel_nand_data *data)
{
	unsigned long csa;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_CS3A_SMC_SMARTMEDIA);

	/* enable pin */
	if (gpio_is_valid(data->enable_pin))
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (gpio_is_valid(data->rdy_pin))
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (gpio_is_valid(data->det_pin))
		at91_set_gpio_input(data->det_pin, 1);

	add_generic_device_res("atmel_nand", 0, nand_resources,
			       ARRAY_SIZE(nand_resources), data);
}
#else
void at91_add_device_nand(struct atmel_nand_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  TWI (i2c)
 * -------------------------------------------------------------------- */

#if defined(CONFIG_I2C_GPIO)
static struct i2c_gpio_platform_data pdata_i2c = {
	.sda_pin		= AT91_PIN_PA23,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PA24,
	.scl_is_open_drain	= 1,
	.udelay			= 5,		/* ~100 kHz */
};

void at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices)
{
	struct i2c_gpio_platform_data *pdata = &pdata_i2c;

	i2c_register_board_info(0, devices, nr_devices);

	at91_set_GPIO_periph(pdata->sda_pin, 1);		/* TWD (SDA) */
	at91_set_multi_drive(pdata->sda_pin, 1);

	at91_set_GPIO_periph(pdata->scl_pin, 1);		/* TWCK (SCL) */
	at91_set_multi_drive(pdata->scl_pin, 1);

	add_generic_device_res("i2c-gpio", 0, NULL, 0, pdata);
}
#else
void at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices) {}
#endif

/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_DRIVER_SPI_ATMEL)
static int spi0_standard_cs[4] = { AT91_PIN_PA3, AT91_PIN_PC11, AT91_PIN_PC16, AT91_PIN_PC17 };

static int spi1_standard_cs[4] = { AT91_PIN_PB3, AT91_PIN_PC5, AT91_PIN_PC4, AT91_PIN_PC3 };

static struct at91_spi_platform_data spi_pdata[] = {
	[0] = {
		.chipselect = spi0_standard_cs,
		.num_chipselect = ARRAY_SIZE(spi0_standard_cs),
	},
	[1] = {
		.chipselect = spi1_standard_cs,
		.num_chipselect = ARRAY_SIZE(spi1_standard_cs),
	},
};

void __init at91_add_device_spi(int spi_id, struct at91_spi_platform_data *pdata)
{
	int i;
	int cs_pin;
	resource_size_t start = ~0;

	BUG_ON(spi_id > 1);

	if (!pdata)
		pdata = &spi_pdata[spi_id];

	for (i = 0; i < pdata->num_chipselect; i++) {
		cs_pin = pdata->chipselect[i];

		/* enable chip-select pin */
		if (gpio_is_valid(cs_pin))
			at91_set_gpio_output(cs_pin, 1);
	}

	/* Configure SPI bus(es) */
	switch (spi_id) {
	case 0:
		start = AT91SAM9260_BASE_SPI0;
		at91_set_A_periph(AT91_PIN_PA0, 0);	/* SPI0_MISO */
		at91_set_A_periph(AT91_PIN_PA1, 0);	/* SPI0_MOSI */
		at91_set_A_periph(AT91_PIN_PA2, 0);	/* SPI1_SPCK */
		break;
	case 1:
		start = AT91SAM9260_BASE_SPI1;
		at91_set_A_periph(AT91_PIN_PB0, 0);	/* SPI1_MISO */
		at91_set_A_periph(AT91_PIN_PB1, 0);	/* SPI1_MOSI */
		at91_set_A_periph(AT91_PIN_PB2, 0);	/* SPI1_SPCK */
		break;
	}

	add_generic_device("atmel_spi", spi_id, NULL, start, SZ_16K,
			   IORESOURCE_MEM, pdata);
}
#else
void __init at91_add_device_spi(int spi_id, struct at91_spi_platform_data *pdata) {}
#endif

resource_size_t __init at91_configure_dbgu(void)
{
	at91_set_A_periph(AT91_PIN_PB14, 1);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PB15, 0);		/* DTXD */

	return AT91_BASE_SYS + AT91_DBGU;
}

resource_size_t __init at91_configure_usart0(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB4, 0);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PB5, 1);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PB26, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PB27, 0);	/* CTS0 */
	if (pins & ATMEL_UART_DTR)
		at91_set_A_periph(AT91_PIN_PB24, 0);	/* DTR0 */
	if (pins & ATMEL_UART_DSR)
		at91_set_A_periph(AT91_PIN_PB22, 0);	/* DSR0 */
	if (pins & ATMEL_UART_DCD)
		at91_set_A_periph(AT91_PIN_PB23, 0);	/* DCD0 */
	if (pins & ATMEL_UART_RI)
		at91_set_A_periph(AT91_PIN_PB25, 0);	/* RI0 */

	return AT91SAM9260_BASE_US0;
}

resource_size_t __init at91_configure_usart1(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB6, 0);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PB7, 1);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PB28, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PB29, 0);	/* CTS1 */

	return AT91SAM9260_BASE_US1;
}

resource_size_t __init at91_configure_usart2(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB8, 0);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PB9, 1);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA4, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA5, 0);	/* CTS2 */

	return AT91SAM9260_BASE_US2;
}

resource_size_t __init at91_configure_usart3(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB10, 0);		/* TXD3 */
	at91_set_A_periph(AT91_PIN_PB11, 1);		/* RXD3 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PC8, 0);	/* RTS3 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PC10, 0);	/* CTS3 */

	return AT91SAM9260_BASE_US3;
}

resource_size_t __init at91_configure_usart4(unsigned pins)
{
	at91_set_B_periph(AT91_PIN_PA31, 0);		/* TXD4 */
	at91_set_B_periph(AT91_PIN_PA30, 1);		/* RXD4 */

	return AT91SAM9260_BASE_US4;
}

resource_size_t __init at91_configure_usart5(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB12, 0);		/* TXD5 */
	at91_set_A_periph(AT91_PIN_PB13, 1);		/* RXD5 */

	return AT91SAM9260_BASE_US5;
}

#if defined(CONFIG_MCI_ATMEL)
/* Consider only one slot : slot 0 */
void at91_add_device_mci(short mmc_id, struct atmel_mci_platform_data *data)
{
	struct device_d *dev;

	if (!data)
		return;

	/* need bus_width */
	if (!data->bus_width)
		return;

	/* input/irq */
	if (gpio_is_valid(data->detect_pin)) {
		at91_set_gpio_input(data->detect_pin, 1);
		at91_set_deglitch(data->detect_pin, 1);
	}

	if (gpio_is_valid(data->wp_pin))
		at91_set_gpio_input(data->wp_pin, 1);

	/* CLK */
	at91_set_A_periph(AT91_PIN_PA8, 0);


	if (data->slot_b) {
		/* CMD */
		at91_set_B_periph(AT91_PIN_PA1, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_B_periph(AT91_PIN_PA0, 1);
		if (data->bus_width == 4) {
			at91_set_B_periph(AT91_PIN_PA3, 1);
			at91_set_B_periph(AT91_PIN_PA4, 1);
			at91_set_B_periph(AT91_PIN_PA5, 1);
		}
	} else {
		/* CMD */
		at91_set_A_periph(AT91_PIN_PA7, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA6, 1);
		if (data->bus_width == 4) {
			at91_set_A_periph(AT91_PIN_PA9, 1);
			at91_set_A_periph(AT91_PIN_PA10, 1);
			at91_set_A_periph(AT91_PIN_PA11, 1);
		}
	}

	dev = add_generic_device("atmel_mci", mmc_id, NULL, AT91SAM9260_BASE_MCI, SZ_16K,
			   IORESOURCE_MEM, data);
}
#else
void at91_add_device_mci(short mmc_id, struct atmel_mci_platform_data *data) {}
#endif

static int at91_fixup_device(void)
{
	at91_rtt_irq_fixup(IOMEM(AT91SAM9260_BASE_RTT));
	return 0;
}
late_initcall(at91_fixup_device);
