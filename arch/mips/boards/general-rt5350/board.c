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

#include <init.h>
#include <common.h>
#include <asm/memory.h>
#include <linux/sizes.h>

static int model_hostname_init(void)
{
	barebox_set_hostname("general-rt5350");

	return 0;
}
postcore_initcall(model_hostname_init);

