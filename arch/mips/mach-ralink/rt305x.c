/*
 * Copyright (C) 2018 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * Based on openwrt
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <io.h>
#include <init.h>
#include <asm/mipsregs.h>

#include <mach/common.h>
#include <mach/ralink_regs.h>
#include <mach/rt305x.h>

#include <of_address.h>
#include <linux/clk.h>
#include <linux/clkdev.h>

enum ralink_soc_type ralink_soc;
struct ralink_soc_info soc_info;

void *rt_sysc_membase;
void *rt_memc_membase;

static void *plat_of_remap_node(const char *node)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, node);
	if (!np)
		panic("Failed to find %s node", node);

	return of_iomap(np, 0);
}

static void ralink_clk_add(const char *dev, unsigned long rate)
{
	struct clk *clk = clk_fixed(dev, rate);
	if (!clk)
		panic("failed to add clock");

	clk_register_clkdev(clk, NULL, dev);
}

static void ralink_clk_init(void)
{
	unsigned long cpu_rate, sys_rate, uart_rate, ref_rate;

	u32 t = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG);

	ref_rate = 20000000;

	if (t & RT3352_CLKCFG0_XTAL_SEL)
		ref_rate = 40000000;

	if (soc_is_rt305x() || soc_is_rt3350()) {
		t = (t >> RT305X_SYSCFG_CPUCLK_SHIFT) &
		     RT305X_SYSCFG_CPUCLK_MASK;
		switch (t) {
		case RT305X_SYSCFG_CPUCLK_LOW:
			cpu_rate = 320000000;
			break;
		case RT305X_SYSCFG_CPUCLK_HIGH:
			cpu_rate = 384000000;
			break;
		}
		sys_rate = uart_rate = cpu_rate / 3;
	} else if (soc_is_rt3352()) {
		t = (t >> RT3352_SYSCFG0_CPUCLK_SHIFT) &
		     RT3352_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT3352_SYSCFG0_CPUCLK_LOW:
			cpu_rate = 384000000;
			break;
		case RT3352_SYSCFG0_CPUCLK_HIGH:
			cpu_rate = 400000000;
			break;
		}
		sys_rate = cpu_rate / 3;
		uart_rate = 40000000;
	} else if (soc_is_rt5350()) {
		t = (t >> RT5350_SYSCFG0_CPUCLK_SHIFT) &
		     RT5350_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT5350_SYSCFG0_CPUCLK_360:
			cpu_rate = 360000000;
			sys_rate = cpu_rate / 3;
			break;
		case RT5350_SYSCFG0_CPUCLK_320:
			cpu_rate = 320000000;
			sys_rate = cpu_rate / 4;
			break;
		case RT5350_SYSCFG0_CPUCLK_300:
			cpu_rate = 300000000;
			sys_rate = cpu_rate / 3;
			break;
		default:
			BUG();
		}
		uart_rate = 40000000;
	} else {
		BUG();
	}

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("sys", sys_rate);
	ralink_clk_add("ref", ref_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000b40.spi", sys_rate);
	ralink_clk_add("10000c00.uartlite", uart_rate);
	ralink_clk_add("10100000.ethernet", sys_rate);

	pr_info("Clocks: CPU: %lu MHz, Bus: %lu MHz, Ref: %lu MHz\n",
		cpu_rate / 1000000, sys_rate / 1000000, ref_rate / 1000000);
}

static int mach_cpu_init(void)
{
	void __iomem *sysc = (void __iomem *) KSEG1ADDR(RT305X_SYSC_BASE);
	unsigned char *name;
	u32 n0;
	u32 n1;
	u32 id;

	n0 = __raw_readl(sysc + SYSC_REG_CHIP_NAME0);
	n1 = __raw_readl(sysc + SYSC_REG_CHIP_NAME1);

	if (n0 == RT3052_CHIP_NAME0 && n1 == RT3052_CHIP_NAME1) {
		unsigned long icache_sets;

		icache_sets = (read_c0_config1() >> 22) & 7;
		if (icache_sets == 1) {
			ralink_soc = RT305X_SOC_RT3050;
			name = "RT3050";
			soc_info.compatible = "ralink,rt3050-soc";
		} else {
			ralink_soc = RT305X_SOC_RT3052;
			name = "RT3052";
			soc_info.compatible = "ralink,rt3052-soc";
		}
	} else if (n0 == RT3350_CHIP_NAME0 && n1 == RT3350_CHIP_NAME1) {
		ralink_soc = RT305X_SOC_RT3350;
		name = "RT3350";
		soc_info.compatible = "ralink,rt3350-soc";
	} else if (n0 == RT3352_CHIP_NAME0 && n1 == RT3352_CHIP_NAME1) {
		ralink_soc = RT305X_SOC_RT3352;
		name = "RT3352";
		soc_info.compatible = "ralink,rt3352-soc";
	} else if (n0 == RT5350_CHIP_NAME0 && n1 == RT5350_CHIP_NAME1) {
		ralink_soc = RT305X_SOC_RT5350;
		name = "RT5350";
		soc_info.compatible = "ralink,rt5350-soc";
	} else {
		panic("rt305x: unknown SoC, n0:%08x n1:%08x", n0, n1);
	}

	id = __raw_readl(sysc + SYSC_REG_CHIP_ID);

	snprintf(soc_info.sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s id:%u rev:%u",
		name,
		(id >> CHIP_ID_ID_SHIFT) & CHIP_ID_ID_MASK,
		(id & CHIP_ID_REV_MASK));

	pr_info("SOC: %s\n", soc_info.sys_type);

	rt_sysc_membase = plat_of_remap_node("ralink,rt3050-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,rt3050-memc");

	ralink_clk_init();

	return 0;
}

postcore_initcall(mach_cpu_init);
