/*
 * Copyright (C) Jianhui Zhao <jianhuizhao329@gmail.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/addrspace.h>

#ifndef __INCLUDE_BOARD_DEBUG_LL_GENERAL_RT5350_H
#define __INCLUDE_BOARD_DEBUG_LL_GENERAL_RT5350_H

#define DEBUG_LL_UART_ADDR 	KSEG1ADDR(0x10000C00)
#define DEBUG_LL_UART_SHIFT 2

/*
 * baudrate = 57600
 * CLK = 40000000 MHz
 */
#define DEBUG_LL_UART_DIVISOR   0x2c

#endif
