/*
 * Copyright (C) 2018 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * Based on arch/mips/boards/tplink-wdr4300/include/board/board_pbl_start.h 
 *
 * This file is part of barebox.
 * See file CREDITS for list of people who contributed to this project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <mach/debug_ll_ar9344.h>
#include <asm/pbl_macros.h>
#include <mach/pbl_macros.h>
#include <mach/pbl_ll_init_ar9344_1.1.h>
#include <asm/pbl_nmon.h>

	.macro	board_pbl_start
	.set	push
	.set	noreorder

	mips_barebox_10h

	debug_ll_ar9344_init

	hornet_mips24k_cp0_setup

	/* test if we are in the SRAM */
	pbl_blt 0xbd000000 1f t8
	b skip_flash_test
	nop
1:
	/* test if we are in the flash */
	pbl_blt 0xbf000000 skip_pll_ram_config t8
skip_flash_test:

	pbl_ar9344_v11_pll_config

	pbl_ar9344_v11_ddr2_config

skip_pll_ram_config:
    /* Initialize caches... */
    mips_cache_reset

    /* ... and enable them */
    dcache_enable

	mips_nmon

	copy_to_link_location	pbl_start

	.set	pop
	.endm
