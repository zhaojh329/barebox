/*
 * start-pbl.c
 *
 * Copyright (c) 2010-2012 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) 2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <init.h>
#include <linux/sizes.h>
#include <pbl.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>
#include <asm-generic/memory_layout.h>
#include <asm/sections.h>
#include <asm/cache.h>
#include <asm/mmu.h>
#include <asm/unaligned.h>

unsigned long free_mem_ptr;
unsigned long free_mem_end_ptr;

/*
 * First instructions in the pbl image
 */
void __naked __section(.text_head_entry) pbl_start(void)
{
	barebox_arm_head();
}

extern void *input_data;
extern void *input_data_end;

__noreturn void barebox_single_pbl_start(unsigned long membase,
		unsigned long memsize, void *boarddata)
{
	unsigned long offset;
	unsigned long pg_start, pg_end, pg_len, uncompressed_len;
	void __noreturn (*barebox)(unsigned long, unsigned long, void *);
	unsigned long endmem = membase + memsize;
	unsigned long barebox_base;

	if (IS_ENABLED(CONFIG_PBL_RELOCATABLE))
		relocate_to_current_adr();

	/* Get offset between linked address and runtime address */
	offset = get_runtime_offset();

	pg_start = (unsigned long)&input_data + global_variable_offset();
	pg_end = (unsigned long)&input_data_end + global_variable_offset();
	pg_len = pg_end - pg_start;
	uncompressed_len = get_unaligned((const u32 *)(pg_start + pg_len - 4));

	if (IS_ENABLED(CONFIG_RELOCATABLE))
		barebox_base = arm_mem_barebox_image(membase, endmem, uncompressed_len + MAX_BSS_SIZE);
	else
		barebox_base = TEXT_BASE;

	if (offset && (IS_ENABLED(CONFIG_PBL_FORCE_PIGGYDATA_COPY) ||
				region_overlap(pg_start, pg_len, barebox_base, pg_len * 4))) {
		/*
		 * copy piggydata binary to its link address
		 */
		memcpy(&input_data, (void *)pg_start, pg_len);
		pg_start = (uint32_t)&input_data;
	}

	setup_c();

	if (IS_ENABLED(CONFIG_MMU_EARLY)) {
		unsigned long ttb = arm_mem_ttb(membase, endmem);
		mmu_early_enable(membase, memsize, ttb);
	}

	free_mem_ptr = arm_mem_early_malloc(membase, endmem);
	free_mem_end_ptr = arm_mem_early_malloc_end(membase, endmem);

	pbl_barebox_uncompress((void*)barebox_base, (void *)pg_start, pg_len);

	arm_early_mmu_cache_flush();
	icache_invalidate();

	if (IS_ENABLED(CONFIG_THUMB2_BAREBOX))
		barebox = (void *)(barebox_base + 1);
	else
		barebox = (void *)barebox_base;

	barebox(membase, memsize, boarddata);
}
