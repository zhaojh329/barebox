/*
 * Copyright (C) 2018 Jianhui Zhao <jianhuizhao329@gmail.com>
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

#include <mach/debug_ll.h>
#include <asm/pbl_macros.h>
#include <mach/pbl_macros.h>
#include <asm/pbl_nmon.h>

	.macro	board_pbl_start
	.set	push
	.set	noreorder

	mips_barebox_10h

	mips_disable_interrupts

	debug_ll_ns16550_init

	debug_ll_outc 'R'
	debug_ll_outc 'T'
	debug_ll_outc '5'
	debug_ll_outc '3'
	debug_ll_outc '5'
	debug_ll_outc '0'
	debug_ll_outc '\r'
	debug_ll_outc '\n'

	copy_to_link_location	pbl_start

	.set	pop
	.endm
