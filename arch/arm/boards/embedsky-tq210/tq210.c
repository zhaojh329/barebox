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
#include <mach/s5pcxx-nand.h>
#include <mach/bbu.h>
#include <mach/s3c-fb.h>

static const unsigned pin_usage[] = {
	/* Video out */
	GPF00_LCD_HSYNC,
	GPF01_LCD_VSYNC,
	GPF02_LCD_VDEN,
	GPF03_LCD_VCLK,
	GPF04_LCD_VD0,
	GPF05_LCD_VD1,
	GPF06_LCD_VD2,
	GPF07_LCD_VD3,
	GPF10_LCD_VD4,
	GPF11_LCD_VD5,
	GPF12_LCD_VD6,
	GPF13_LCD_VD7,
	GPF14_LCD_VD8,
	GPF15_LCD_VD9,
	GPF16_LCD_VD10,
	GPF17_LCD_VD11,
	GPF20_LCD_VD12,
	GPF21_LCD_VD13,
	GPF22_LCD_VD14,
	GPF23_LCD_VD15,
	GPF24_LCD_VD16,
	GPF25_LCD_VD17,
	GPF26_LCD_VD18,
	GPF27_LCD_VD19,
	GPF30_LCD_VD20,
	GPF31_LCD_VD21,
	GPF32_LCD_VD22,
	GPF33_LCD_VD23,
	GPD00_GPIO | GPIO_OUT,	/*  PWR(backlight) */

	/* NAND requirements */
	GPMP012_NFCS0,
	GPMP030_NF_CLE,
	GPMP031_NF_ALE,
	GPMP032_NF_FWE,
	GPMP033_NF_FRE,
	GPMP034_NF_RnB0
};

static struct gpio_led leds[] = {
	{
		.gpio = GPC03,
		.led = {
			.name = "led1",
		}
	}, {
		.gpio = GPC04,
		.led = {
			.name = "led2",
		}
	}
};

static int tq210_mem_init(void)
{
	arm_add_mem_device("ram0", S3C_SDRAM_BASE, s5p_get_memory_size());
	return 0;
}
mem_initcall(tq210_mem_init);

static int tq210_console_init(void)
{
	/*
	 * configure the UART1 right now, as barebox will
	 * start to send data immediately
	 */
	s3c_gpio_mode(GPA00_RXD0 | ENABLE_PU);
	s3c_gpio_mode(GPA01_TXD0);
	s3c_gpio_mode(GPA02_NCTS0 | ENABLE_PU);
	s3c_gpio_mode(GPA03_NRTS0);

	barebox_set_model("Embedsky tq210");
	barebox_set_hostname("tq210");

	add_generic_device("s3c_serial", DEVICE_ID_DYNAMIC, NULL,
			   S3C_UART1_BASE, S3C_UART1_SIZE,
			   IORESOURCE_MEM, NULL);
	return 0;
}
console_initcall(tq210_console_init);

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

/*
 * Nand Flash access timings
 * HCLK_PSYS=133MHz(7.5ns)
 * S5PV210_UM_REV1.1.pdf - 4.5.2.1 Nand Flash Configuration Register
 * TACLS: Duration = HCLK x TACLS, Corresponding to NAND flash's tCLS and tALS (12ns)
 *		HCLK * 2 = 15ns > 12ns
 * TWRPH0: Duration = HCLK x ( TWRPH0 + 1 ), Corresponding to NAND flash's tWP(12ns)
 * 			Note: You should add additional cycles about 10ns for page read
 *			because of additional signal delay on PCB pattern.
 *			HCLK * (2 + 1) = 22.5ns > (12 + 10)ns
 * TWRPH1: Duration = HCLK x ( TWRPH1 + 1 ), Corresponding to NAND flash's tALH and tALH(5ns)
 *			HCLK * (0 + 1) = 7.5ns > 5ns
 */
#define TACLS 2
#define TWRPH0 2
#define TWRPH1 0

static struct s5pcxx_nand_platform_data nand_info = {
	.nand_timing = CALC_NFCONF_TIMING(TACLS, TWRPH0, TWRPH1),
};

static struct fb_videomode s3c_fb_modes[] = {
	{
		.name		= "TN92",
		.refresh	= 60,
		.xres		= 800,
		.left_margin	= 46,
		.right_margin	= 210,
		.hsync_len	= 2,
		.yres		= 480,
		.upper_margin	= 23,
		.lower_margin	= 22,
		.vsync_len	= 2,
		/* Cannot work when set to 33300. Fix me */
		.pixclock	= KHZ2PICOS(27000)
	}
};

static void lcd_enable(int enable)
{
	if (enable)
		gpio_set_value(GPD00, 1);
	else
		gpio_set_value(GPD00, 0);
}

static struct s3c_fb_platform_data s3c_fb_data = {
	.mode_list		= s3c_fb_modes,
	.mode_cnt		= sizeof(s3c_fb_modes) / sizeof(struct fb_videomode),
	.bits_per_pixel	= 32,
	.enable = lcd_enable
};

static int tq210_devices_init(void)
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
	
	add_generic_device("s5pcxx_nand", DEVICE_ID_DYNAMIC, NULL,
				S3C_NAND_BASE, 0x40000, IORESOURCE_MEM, &nand_info);
	
#ifdef CONFIG_NAND
	/* ----------- add some vital partitions -------- */
	devfs_del_partition("self_raw");
	devfs_add_partition("nand0", 0x00000, 0x40000, DEVFS_PARTITION_FIXED, "self_raw");
	dev_add_bb_dev("self_raw", "self0");

	devfs_del_partition("env_raw");
	devfs_add_partition("nand0", 0x40000, 0x20000, DEVFS_PARTITION_FIXED, "env_raw");
	dev_add_bb_dev("env_raw", "env0");

	s5pcxx_bbu_nand_register_handler();
#endif

	add_generic_device("s3c_fb", DEVICE_ID_DYNAMIC, NULL,
				S5P_LCD_BASE, 0x100, IORESOURCE_MEM, &s3c_fb_data);

    pr_info("xxxxxxxxx12\n");

	return 0;
}
device_initcall(tq210_devices_init);
