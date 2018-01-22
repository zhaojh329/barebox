/*
 * Copyright (C) 2017 Jianhui Zhao
 * Based on tiny210 code by Alexey Galakhov
 *
 * In some ways inspired by code
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
 */

#include <common.h>
#include <driver.h>
#include <init.h>
#include <linux/sizes.h>
#include <generated/mach-types.h>
#include <gpio.h>
#include <led.h>
#include <io.h>
#include <nand.h>
#include <asm/armlinux.h>
#include <mach/iomux.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-clocks.h>
#include <mach/s3c-generic.h>
#include <mach/s3c-busctl.h>
#include <platform_data/eth-dm9000.h>

static const unsigned pin_usage[] = {

};

static struct gpio_led leds[] = {
	{
		.gpio = GPH20,
		.led = {
			.name = "led1",
		}
	}, {
		.gpio = GPH21,
		.led = {
			.name = "led2",
		}
	}, {
		.gpio = GPH22,
		.led = {
			.name = "led3",
		}
	}, {
		.gpio = GPH23,
		.led = {
			.name = "led4",
		}
	}
};

static int ok210_mem_init(void)
{
	arm_add_mem_device("ram0", S3C_SDRAM_BASE, s5p_get_memory_size());
	return 0;
}
mem_initcall(ok210_mem_init);

static int ok210_console_init(void)
{
	/*
	 * configure the UART1 right now, as barebox will
	 * start to send data immediately
	 */
	s3c_gpio_mode(GPA10_RXD2 | ENABLE_PU);
	s3c_gpio_mode(GPA11_TXD2);
	s3c_gpio_mode(GPA12_NCTS2 | ENABLE_PU);
	s3c_gpio_mode(GPA13_NRTS2);

	barebox_set_model("Forlinx ok210");
	barebox_set_hostname("ok210");

	add_generic_device("s3c_serial", DEVICE_ID_DYNAMIC, NULL,
			   S3C_UART3_BASE, S3C_UART3_SIZE,
			   IORESOURCE_MEM, NULL);
	return 0;
}
console_initcall(ok210_console_init);

static struct dm9000_platform_data dm9000_data = {
	.srom = 0,  /* no serial ROM for the ethernet address */
};

static void ethernet_init(void)
{
	/* CS#1 to access the network controller */
	u32 reg = readl(S3C_BWSCON);
	reg &= ~(0xF << 4);
	reg |=  (0x3 << 4);
	writel(reg, S3C_BWSCON);

	/*
	 * HCLK_PSYS=133MHz(7.5ns)
	 * Tacs=0ns     0 << 28
	 * Tcos=0ns     0 << 24
	 * Tacc=22ns    2 << 16
	 * Tcoh=0ns     0 << 12
	 * Tcah=0ns     0 << 8
	 */
	writel((0 << 28) | (0 << 24) | (2 << 16) | (0 << 12) | (0 << 8), S3C_BANKCON1);
	
	add_dm9000_device(0, S3C_CS1_BASE, S3C_CS1_BASE + 0x4, IORESOURCE_MEM_16BIT, &dm9000_data);
}

static int ok210_devices_init(void)
{
	int i;

	/* ----------- configure the access to the outer space ---------- */
	for (i = 0; i < ARRAY_SIZE(pin_usage); i++)
		s3c_gpio_mode(pin_usage[i]);
	
	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].active_low = 1;
		gpio_direction_output(leds[i].gpio, leds[i].active_low);
		led_gpio_register(&leds[i]);
	}

	led_set_trigger(LED_TRIGGER_HEARTBEAT, &leds[0].led);

	ethernet_init();

	return 0;
}
device_initcall(ok210_devices_init);
