/*
 * Copyright (C) 2012 Alexey Galakhov
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <config.h>
#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/sizes.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>
#include <asm/sections.h>
#include <mach/s3c-iomap.h>
#include <mach/s3c-clocks.h>
#include <mach/s3c-generic.h>
#include <debug_ll.h>

#define IRAM_CODE_BASE		0xD0020010

/* TQ210 has 2 leds numbered from 0 to 1 at GPC0[3:4] */
static inline void __bare_init debug_led(int led, bool state)
{
	uint32_t r;

    led += 3;
	    
    /* GPC0CON: mode 0001=output */
    r = readl(0xE0200060);
    r &= ~(0xF << (4 * led));
    r |=  (0x1 << (4 * led));
    writel(r, 0xE0200060);
    /* GPC0DAT: active low */
    r = readl(0xE0200064);
	r &= ~(1 << led);
	r |= state << led;
	writel(r, 0xE0200064);
}

/*
 * iROM boot from MMC
 * TODO: replace this by native boot
 */

#define ADDR_V210_SDMMC_BASE	0xD0037488
#define ADDR_CopySDMMCtoMem	0xD0037F98

int __bare_init s5p_irom_load_mmc(void *dest, uint32_t start_block, uint16_t block_count)
{
	typedef uint32_t (*func_t) (int32_t, uint32_t, uint16_t, uint32_t*, int8_t);
	uint32_t chbase = readl(ADDR_V210_SDMMC_BASE);
	func_t func = (func_t)readl(ADDR_CopySDMMCtoMem);
	int chan = (chbase - 0xEB000000) >> 20;
	if (chan != 0 && chan != 2)
		return 0;
	return func(chan, start_block, block_count, (uint32_t*)dest, 0) ? 1 : 0;
}

/* See "S5PV210 iROM Application Note" */
#define NF8_ReadPage_Adv(block_addr, page_offset, dest) \
	(((int(*)(u32, u32, u8*))(*((u32 *)0xD0037F90)))((block_addr), (page_offset), (dest)))

/*
 * K9K8G08U0B
 * page size: 2K
 * block size: 2K * 64
 */
int __bare_init s5p_irom_load_nand(void *dest, uint32_t size)
{
	int page = 0;
	int total_pages = (size + 2047) / 2048;

	for (; page < total_pages; page++, dest += 2048)
		NF8_ReadPage_Adv(page / 64, page % 64, dest);

	return 1;
}

static __bare_init __naked void jump_sdram(unsigned long offset)
{	
	__asm__ __volatile__ (
			"sub lr, lr, %0;"
			"mov pc, lr;" : : "r"(offset)
			);
}

static __bare_init bool load_stage2(void *dest, size_t size)
{
	u32 OM = *(volatile u32 *)(0xE0000004); /* OM Register, see original u-boot from samsung */

	OM &= 0x1F;

	if (OM == 0x0C)
		return s5p_irom_load_mmc(dest, 1, (size+ 511) / 512);
	else if (OM == 0x2)
		return s5p_irom_load_nand(dest, size);
	else
		return 0;
}

/*
 * GPA0_0 -> UART_0_RXD
 * GPA0_1 -> UART_0_TXD
 */
static __bare_init void debug_ll_gpio_init(void)
{
	void __iomem *reg = IOMEM(0xE0200000);
	writel((readl(reg) & ~0xFF) | 0x22, reg);
}

void __bare_init barebox_arm_reset_vector(void)
{
	if (get_pc() < IRAM_CODE_BASE) /* Are we running from iRAM? */
		/* No, we don't. */
		goto boot;
	
	arm_cpu_lowlevel_init();

#ifdef CONFIG_S3C_PLL_INIT
	s5p_init_pll();
#endif

	/* low level debug init */
	debug_ll_gpio_init();
	debug_ll_init();

	/*
	 * Note: `puts_ll` cannot be used before relocated.
	 * puts_ll("\nIn iram...\n");
	 * String constants are stored in ro-data
	 */
	putc_ll('\r');
	putc_ll('\n');
	putc_ll('I');
	putc_ll('n');
	putc_ll(' ');
	putc_ll('i');
	putc_ll('r');
	putc_ll('a');
	putc_ll('m');
	putc_ll('.');
	putc_ll('.');
	putc_ll('.');
	putc_ll('\r');
	putc_ll('\n');

	debug_led(0, 1);

	s5p_init_dram_bank_ddr2(S5P_DMC0_BASE, 0x20E00323, 0, 0);
	s5p_init_dram_bank_ddr2(S5P_DMC1_BASE, 0x40E00323, 0, 0);

	debug_led(1, 1);

	if (! load_stage2((void*)(_text - 16),
#ifdef CONFIG_PBL_IMAGE		
		barebox_pbl_size + barebox_image_size + 16)) {
#else
		barebox_image_size + 16)) {
#endif
		while (1) { } /* hang */
	}

	debug_led(0, 0);

	puts_ll("jump to sdram...\n");
	
	jump_sdram(IRAM_CODE_BASE - (unsigned long)_text);

	debug_led(1, 1);

boot:
	barebox_arm_entry(S3C_SDRAM_BASE, SZ_256M, NULL);
}
