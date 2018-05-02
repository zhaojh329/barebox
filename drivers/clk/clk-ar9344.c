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
#include <asm-generic/div64.h>

#include <mach/ath79.h>
#include <dt-bindings/clock/ath79-clk.h>

static struct clk *clks[ATH79_CLK_END];
static struct clk_onecell_data clk_data;

static u32 clk_get_rate_mhz(const char *name)
{
	struct clk *clk = clk_lookup(name);

	if (clk)
		return clk_get_rate(clk) / 1000000;

	return 0;
}

static u32 __init ar934x_get_pll_freq(u32 ref, u32 ref_div, u32 nint, u32 nfrac,
				      u32 frac, u32 out_div)
{
	u64 t;
	u32 ret;

	t = ref;
	t *= nint;
	do_div(t, ref_div);
	ret = t;

	t = ref;
	t *= nfrac;
	do_div(t, ref_div * frac);
	ret += t;

	ret /= (1 << out_div);
	return ret;
}

static void ar9344_pll_init(void __iomem *pll_base, void __iomem *srif_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll, out_div, ref_div, nint, nfrac, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;

	ref_rate = clk_get_rate_mhz("ref") * 1000 * 1000;

	pll = __raw_readl(srif_base + AR934X_SRIF_CPU_DPLL2_REG);
	if (pll & AR934X_SRIF_DPLL2_LOCAL_PLL) {
		out_div = (pll >> AR934X_SRIF_DPLL2_OUTDIV_SHIFT) &
			  AR934X_SRIF_DPLL2_OUTDIV_MASK;
		pll = __raw_readl(srif_base + AR934X_SRIF_CPU_DPLL1_REG);
		nint = (pll >> AR934X_SRIF_DPLL1_NINT_SHIFT) &
		       AR934X_SRIF_DPLL1_NINT_MASK;
		nfrac = pll & AR934X_SRIF_DPLL1_NFRAC_MASK;
		ref_div = (pll >> AR934X_SRIF_DPLL1_REFDIV_SHIFT) &
			  AR934X_SRIF_DPLL1_REFDIV_MASK;
		frac = 1 << 18;
	} else {
		pll = __raw_readl(pll_base + AR934X_PLL_CPU_CONFIG_REG);
		out_div = (pll >> AR934X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
			AR934X_PLL_CPU_CONFIG_OUTDIV_MASK;
		ref_div = (pll >> AR934X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
			  AR934X_PLL_CPU_CONFIG_REFDIV_MASK;
		nint = (pll >> AR934X_PLL_CPU_CONFIG_NINT_SHIFT) &
		       AR934X_PLL_CPU_CONFIG_NINT_MASK;
		nfrac = (pll >> AR934X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
			AR934X_PLL_CPU_CONFIG_NFRAC_MASK;
		frac = 1 << 6;
	}

	cpu_pll = ar934x_get_pll_freq(ref_rate, ref_div, nint,
				      nfrac, frac, out_div);

	pll = __raw_readl(srif_base + AR934X_SRIF_DDR_DPLL2_REG);
	if (pll & AR934X_SRIF_DPLL2_LOCAL_PLL) {
		out_div = (pll >> AR934X_SRIF_DPLL2_OUTDIV_SHIFT) &
			  AR934X_SRIF_DPLL2_OUTDIV_MASK;
		pll = __raw_readl(srif_base + AR934X_SRIF_DDR_DPLL1_REG);
		nint = (pll >> AR934X_SRIF_DPLL1_NINT_SHIFT) &
		       AR934X_SRIF_DPLL1_NINT_MASK;
		nfrac = pll & AR934X_SRIF_DPLL1_NFRAC_MASK;
		ref_div = (pll >> AR934X_SRIF_DPLL1_REFDIV_SHIFT) &
			  AR934X_SRIF_DPLL1_REFDIV_MASK;
		frac = 1 << 18;
	} else {
		pll = __raw_readl(pll_base + AR934X_PLL_DDR_CONFIG_REG);
		out_div = (pll >> AR934X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
			  AR934X_PLL_DDR_CONFIG_OUTDIV_MASK;
		ref_div = (pll >> AR934X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_REFDIV_MASK;
		nint = (pll >> AR934X_PLL_DDR_CONFIG_NINT_SHIFT) &
		       AR934X_PLL_DDR_CONFIG_NINT_MASK;
		nfrac = (pll >> AR934X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
			AR934X_PLL_DDR_CONFIG_NFRAC_MASK;
		frac = 1 << 10;
	}

	ddr_pll = ar934x_get_pll_freq(ref_rate, ref_div, nint,
				      nfrac, frac, out_div);

	clk_ctrl = __raw_readl(pll_base + AR934X_PLL_CPU_DDR_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = cpu_pll / (postdiv + 1);
	else
		cpu_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = ddr_pll / (postdiv + 1);
	else
		ddr_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	clks[ATH79_CLK_CPU] = clk_fixed("cpu", cpu_rate);
	clks[ATH79_CLK_DDR] = clk_fixed("ddr", ddr_rate);
	clks[ATH79_CLK_AHB] = clk_fixed("ahb", ahb_rate);
}

static int ar9344_clk_probe(struct device_d *dev)
{
	void __iomem *pll_base, *srif_base;

	pll_base = dev_request_mem_region_by_name(dev, "pll");
	srif_base = dev_request_mem_region_by_name(dev, "srif");

	ar9344_pll_init(pll_base, srif_base);

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
