/*
 * Copyright (C) 2009 Juergen Beisert, Pengutronix
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
 *
 */

#ifndef MACH_S5PCXX_NAND_H
#define MACH_S5PCXX_NAND_H

/**
 * Locate the timing bits for the NFCONF register
 * @param setup is the TACLS clock count
 * @param access is the TWRPH0 clock count
 * @param hold is the TWRPH1 clock count
 *
 * @note A clock count of 0 means always 1 HCLK clock.
 * @note Clock count settings depend on the NAND flash requirements and the current HCLK speed
 */
# define CALC_NFCONF_TIMING(setup, access, hold) \
	((setup << 12) + (access << 8) + (hold << 4))

/**
 * Define platform specific data for the NAND controller and its device
 */
struct s5pcxx_nand_platform_data {
	uint32_t nand_timing;	/**< value for the NFCONF register (timing bits only) */
	char flash_bbt;	/**< force a flash based BBT */
};

/**
 * @file
 * @brief Basic declaration to use the s5pcxx NAND driver
 */

#endif /* MACH_S5PCXX_NAND_H */
