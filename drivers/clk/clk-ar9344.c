/*
 * Copyright (C) Jianhui Zhao <jianhuizhao329@gmail.com>
 * Copyright (C) 2017 Oleksij Rempel <linux@rempel-privat.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>

#include <mach/ath79.h>
#include <dt-bindings/clock/ath79-clk.h>

#define AR9344_OUTDIV_M				0x3
#define AR9344_OUTDIV_S				19
#define AR9344_REFDIV_M				0x1f
#define AR9344_REFDIV_S				12
#define AR9344_NINT_M				0x3f
#define AR9344_NINT_S				6
#define AR9344_NFRAC_M				0x3f
#define AR9344_NFRAC_S				0

static struct clk *clks[ATH79_CLK_END];
static struct clk_onecell_data clk_data;

struct clk_ar9344 {
	struct clk clk;
	void __iomem *base;
	const char *parent;
};

static unsigned long ar934x_cpupll_to_hz(const u32 regval, unsigned long xtal)
{
	const u32 outdiv = (regval >> AR934X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
			   AR934X_PLL_CPU_CONFIG_OUTDIV_MASK;
	const u32 refdiv = (regval >> AR934X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
			   AR934X_PLL_CPU_CONFIG_REFDIV_MASK;
	const u32 nint = (regval >> AR934X_PLL_CPU_CONFIG_NINT_SHIFT) &
			   AR934X_PLL_CPU_CONFIG_NINT_MASK;
	const u32 nfrac = (regval >> AR934X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
			   AR934X_PLL_CPU_CONFIG_NFRAC_MASK;

	return (xtal * (nint + (nfrac >> 9))) / (refdiv * (1 << outdiv));
}

static unsigned long ar934x_ddrpll_to_hz(const u32 regval, unsigned long xtal)
{
	const u32 outdiv = (regval >> AR934X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_OUTDIV_MASK;
	const u32 refdiv = (regval >> AR934X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_REFDIV_MASK;
	const u32 nint = (regval >> AR934X_PLL_DDR_CONFIG_NINT_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_NINT_MASK;
	const u32 nfrac = (regval >> AR934X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_NFRAC_MASK;

	return (xtal * (nint + (nfrac >> 9))) / (refdiv * (1 << outdiv));
}

static unsigned long clk_ar9344_recalc_rate(struct clk *clk,
	unsigned long parent_rate)
{
	struct clk_ar9344 *f = container_of(clk, struct clk_ar9344, clk);
	u32 ctrl, cpu, cpupll, ddr, ddrpll;
	u32 cpudiv, ddrdiv, busdiv;
	u32 cpuclk, ddrclk, busclk;

	cpu = __raw_readl(f->base + AR934X_PLL_CPU_CONFIG_REG);
	ddr = __raw_readl(f->base + AR934X_PLL_DDR_CONFIG_REG);
	ctrl = __raw_readl(f->base + AR934X_PLL_CPU_DDR_CLK_CTRL_REG);

	cpupll = ar934x_cpupll_to_hz(cpu, parent_rate);
	ddrpll = ar934x_ddrpll_to_hz(ddr, parent_rate);

	if (!strcmp(clk->name, "cpu")) {
		if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_PLL_BYPASS)
			cpuclk = parent_rate;
		else if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPUCLK_FROM_CPUPLL)
			cpuclk = cpupll;
		else
			cpuclk = ddrpll;


		cpudiv = (ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_SHIFT) &
			AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_MASK;

		return cpuclk / (cpudiv + 1);
	}

	if (!strcmp(clk->name, "ddr")) {
		if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_PLL_BYPASS)
			ddrclk = parent_rate;
		else if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDRCLK_FROM_DDRPLL)
			ddrclk = ddrpll;
		else
			ddrclk = cpupll;

		ddrdiv = (ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_SHIFT) &
			AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_MASK;

		return ddrclk / (ddrdiv + 1);
	}

	if (!strcmp(clk->name, "ahb")) {
		if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_PLL_BYPASS)
			busclk = parent_rate;
		else if (ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHBCLK_FROM_DDRPLL)
			busclk = ddrpll;
		else
			busclk = cpupll;

		busdiv = (ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_SHIFT) &
			AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_MASK;

		return busclk / (busdiv + 1);
	}

	return 0;
}

struct clk_ops clk_ar9344_ops = {
	.recalc_rate = clk_ar9344_recalc_rate,
};

static struct clk *clk_ar9344(const char *name, const char *parent,
	void __iomem *base)
{
	struct clk_ar9344 *f = xzalloc(sizeof(*f));

	f->parent = parent;
	f->base = base;

	f->clk.ops = &clk_ar9344_ops;
	f->clk.name = name;
	f->clk.parent_names = &f->parent;
	f->clk.num_parents = 1;

	clk_register(&f->clk);

	return &f->clk;
}

static void ar9344_pll_init(void __iomem *base)
{
	clks[ATH79_CLK_CPU] = clk_ar9344("cpu", "ref", base);
	clks[ATH79_CLK_DDR] = clk_ar9344("ddr", "ref", base);
	clks[ATH79_CLK_AHB] = clk_ar9344("ahb", "ref", base);
}

static u32 clk_get_rate_mhz(const char *name)
{
	struct clk *clk = clk_lookup(name);

	if (clk)
		return clk_get_rate(clk) / 1000000;

	return 0;
}

static int ar9344_clk_probe(struct device_d *dev)
{
	struct resource *iores;
	void __iomem *base;

	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	base = IOMEM(iores->start);

	ar9344_pll_init(base);

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(dev->device_node, of_clk_src_onecell_get,
			    &clk_data);

	pr_info("Clocks: CPU:%u MHz, DDR:%u MHz, AHB:%u MHz, Ref:%u MHz\n",
		clk_get_rate_mhz("cpu"), clk_get_rate_mhz("ddr"),
		clk_get_rate_mhz("ahb"), clk_get_rate_mhz("ref"));

	return 0;
}

static __maybe_unused struct of_device_id ar9344_clk_dt_ids[] = {
	{
		.compatible = "qca,ar9344-pll",
	}, {
		/* sentinel */
	}
};

static struct driver_d ar9344_clk_driver = {
	.probe	= ar9344_clk_probe,
	.name	= "ar9344_clk",
	.of_compatible = DRV_OF_COMPAT(ar9344_clk_dt_ids),
};

static int ar9344_clk_init(void)
{
	return platform_driver_register(&ar9344_clk_driver);
}
postcore_initcall(ar9344_clk_init);

/**
 * of_fixed_clk_setup() - Setup function for simple fixed rate clock
 */
static int of_fixed_clk_setup(struct device_node *node)
{
	struct clk *clk;
	u32 rate, rv;

	rv = ath79_reset_rr(AR934X_RESET_REG_BOOTSTRAP);
	if (rv & AR934X_BOOTSTRAP_REF_CLK_40)
		rate = 40000000;
	else
		rate = 25000000;

	clk = clk_fixed(node->name, rate);
	if (IS_ERR(clk))
		return IS_ERR(clk);
	return of_clk_add_provider(node, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(fixed_clock, "qca,ar9344-fixed-clock", of_fixed_clk_setup);
