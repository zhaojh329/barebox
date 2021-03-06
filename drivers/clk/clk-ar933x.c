/*
 * Copyright (C) 2014 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * Based on the Linux ath79 clock code
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>

#include <mach/ath79.h>
#include <dt-bindings/clock/ath79-clk.h>

static struct clk *clks[ATH79_CLK_END];
static struct clk_onecell_data clk_data;

struct clk_ar933x {
	struct clk clk;
	void __iomem *base;
	u32 div_shift;
	u32 div_mask;
	const char *parent;
};

static unsigned long clk_ar933x_recalc_rate(struct clk *clk,
	unsigned long parent_rate)
{
	struct clk_ar933x *f = container_of(clk, struct clk_ar933x, clk);
	unsigned long rate;
	unsigned long freq;
	u32 clock_ctrl;
	u32 cpu_config;
	u32 t;

	clock_ctrl = __raw_readl(f->base + AR933X_PLL_CLOCK_CTRL_REG);

	if (clock_ctrl & AR933X_PLL_CLOCK_CTRL_BYPASS) {
		rate = parent_rate;
	} else {
		cpu_config = __raw_readl(f->base + AR933X_PLL_CPU_CONFIG_REG);

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_REFDIV_MASK;
		freq = parent_rate / t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_NINT_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_NINT_MASK;
		freq *= t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_OUTDIV_MASK;
		if (t == 0)
			t = 1;

		freq >>= t;

		t = ((clock_ctrl >> f->div_shift) & f->div_mask) + 1;
		rate = freq / t;
	}

	return rate;
}

struct clk_ops clk_ar933x_ops = {
	.recalc_rate = clk_ar933x_recalc_rate,
};

static struct clk *clk_ar933x(const char *name, const char *parent,
	void __iomem *base, u32 div_shift, u32 div_mask)
{
	struct clk_ar933x *f = xzalloc(sizeof(*f));

	f->parent = parent;
	f->base = base;
	f->div_shift = div_shift;
	f->div_mask = div_mask;

	f->clk.ops = &clk_ar933x_ops;
	f->clk.name = name;
	f->clk.parent_names = &f->parent;
	f->clk.num_parents = 1;

	clk_register(&f->clk);

	return &f->clk;
}

static void ar933x_pll_init(void __iomem *base)
{
	clks[ATH79_CLK_CPU] = clk_ar933x("cpu", "ref", base,
		AR933X_PLL_CLOCK_CTRL_CPU_DIV_SHIFT,
		AR933X_PLL_CLOCK_CTRL_CPU_DIV_MASK);

	clks[ATH79_CLK_DDR] = clk_ar933x("ddr", "ref", base,
		AR933X_PLL_CLOCK_CTRL_DDR_DIV_SHIFT,
		AR933X_PLL_CLOCK_CTRL_DDR_DIV_MASK);

	clks[ATH79_CLK_AHB] = clk_ar933x("ahb", "ref", base,
		AR933X_PLL_CLOCK_CTRL_AHB_DIV_SHIFT,
		AR933X_PLL_CLOCK_CTRL_AHB_DIV_MASK);
}

static u32 clk_get_rate_mhz(const char *name)
{
	struct clk *clk = clk_lookup(name);

	if (clk)
		return clk_get_rate(clk) / 1000000;

	return 0;
}

static int ar933x_clk_probe(struct device_d *dev)
{
	struct resource *iores;
	void __iomem *base;

	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	base = IOMEM(iores->start);

	ar933x_pll_init(base);

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(dev->device_node, of_clk_src_onecell_get,
			    &clk_data);

	pr_info("Clocks: CPU:%u MHz, DDR:%u MHz, AHB:%u MHz, Ref:%u MHz\n",
		clk_get_rate_mhz("cpu"), clk_get_rate_mhz("ddr"),
		clk_get_rate_mhz("ahb"), clk_get_rate_mhz("ref"));

	return 0;
}

static __maybe_unused struct of_device_id ar933x_clk_dt_ids[] = {
	{
		.compatible = "qca,ar9330-pll",
	}, {
		/* sentinel */
	}
};

static struct driver_d ar933x_clk_driver = {
	.probe	= ar933x_clk_probe,
	.name	= "ar933x_clk",
	.of_compatible = DRV_OF_COMPAT(ar933x_clk_dt_ids),
};

static int ar933x_clk_init(void)
{
	return platform_driver_register(&ar933x_clk_driver);
}
postcore_initcall(ar933x_clk_init);

/**
 * of_fixed_clk_setup() - Setup function for simple fixed rate clock
 */
static int of_fixed_clk_setup(struct device_node *node)
{
	struct clk *clk;
	u32 rate, rv;

	rv = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (rv & AR933X_BOOTSTRAP_REF_CLK_40)
		rate = 40000000;
	else
		rate = 25000000;

	clk = clk_fixed(node->name, rate);
	if (IS_ERR(clk))
		return IS_ERR(clk);
	return of_clk_add_provider(node, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(fixed_clock, "qca,ar9330-fixed-clock", of_fixed_clk_setup);
