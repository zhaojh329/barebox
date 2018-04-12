/*
 * Copyright (C) 2009-2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#include <linux/sizes.h>

#include <asm/barebox-arm.h>

#include <mach/at91sam926x_board_init.h>
#include <mach/at91sam9263_matrix.h>

#define MASTER_CLOCK		180

#if MASTER_CLOCK == 200
#define MASTER_PLL_MUL		100
#else
#define MASTER_PLL_MUL		90
#endif
#define MASTER_PLL_DIV		6

static void __bare_init usb_a9263_board_config(struct at91sam926x_board_cfg *cfg)
{
	/* Disable Watchdog */
	cfg->wdt_mr =
		AT91_WDT_WDIDLEHLT | AT91_WDT_WDDBGHLT |
		AT91_WDT_WDV |
		AT91_WDT_WDDIS |
		AT91_WDT_WDD;

	/* define PDC[31:16] as DATA[31:16] */
	cfg->ebi_pio_pdr = 0xFFFF0000;
	/* no pull-up for D[31:16] */
	cfg->ebi_pio_ppudr = 0xFFFF0000;
	/* EBI0_CSA, CS1 SDRAM, CS3 NAND Flash, 3.3V memories */
	cfg->ebi_csa =
		AT91_MATRIX_EBI0_DBPUC | AT91_MATRIX_EBI0_VDDIOMSEL_3_3V |
		AT91_MATRIX_EBI0_CS1A_SDRAMC;

	cfg->smc_cs = 3;
	cfg->smc_mode =
		AT91_SMC_READMODE | AT91_SMC_WRITEMODE |
		AT91_SMC_DBW_8 |
		AT91_SMC_EXNWMODE_DISABLE |
		AT91_SMC_TDF_(2);
	cfg->smc_cycle =
		AT91_SMC_NWECYCLE_(5) | AT91_SMC_NRDCYCLE_(5);
	cfg->smc_pulse =
		AT91_SMC_NWEPULSE_(3) | AT91_SMC_NCS_WRPULSE_(3) |
		AT91_SMC_NRDPULSE_(3) | AT91_SMC_NCS_RDPULSE_(3);
	cfg->smc_setup =
		AT91_SMC_NWESETUP_(1) | AT91_SMC_NCS_WRSETUP_(0) |
		AT91_SMC_NRDSETUP_(1) | AT91_SMC_NCS_RDSETUP_(0);

	cfg->pmc_mor = AT91_PMC_OSCBYPASS;
	cfg->pmc_pllar =
		AT91_PMC_PLLA_WR_ERRATA | /* Bit 29 must be 1 when prog */
		AT91_PMC_PLLCOUNT |	/* PLL Counter */
		(0 << 28) |		/* PLL Clock Frequency Range */
		((MASTER_PLL_MUL - 1) << 16) | (MASTER_PLL_DIV);
	/* PCK/2 = MCK Master Clock from PLLA */
	cfg->pmc_mckr1 =
		AT91_PMC_CSS_SLOW |
		AT91_PMC_PRES_1 |
		AT91SAM9_PMC_MDIV_2 |
		AT91_PMC_PDIV_1;
	/* PCK/2 = MCK Master Clock from PLLA */
	cfg->pmc_mckr2 =
		AT91_PMC_CSS_PLLA |
		AT91_PMC_PRES_1 |
		AT91SAM9_PMC_MDIV_2 |
		AT91_PMC_PDIV_1;

	/* SDRAM */
	/* SDRAMC_TR - Refresh Timer register */
	cfg->sdrc_tr1 = 0x13C;
	/* SDRAMC_CR - Configuration register*/
	cfg->sdrc_cr =
		AT91_SDRAMC_NR_13 |
		AT91_SDRAMC_NB_4 |
		AT91_SDRAMC_CAS_2 |
		AT91_SDRAMC_DBW_32 |
		(2 <<  8) |		/* Write Recovery Delay */
		(7 << 12) |		/* Row Cycle Delay */
		(2 << 16) |		/* Row Precharge Delay */
		(2 << 20) |		/* Row to Column Delay */
		(5 << 24) |		/* Active to Precharge Delay */
		(8 << 28);		/* Exit Self Refresh to Active Delay */

	if (IS_ENABLED(CONFIG_AT91_HAVE_SRAM_128M))
		cfg->sdrc_cr |= AT91_SDRAMC_NC_10;
	else
		cfg->sdrc_cr |= AT91_SDRAMC_NC_9;

	/* Memory Device Register -> SDRAM */
	cfg->sdrc_mdr = AT91_SDRAMC_MD_SDRAM;
	/* SDRAM_TR */
	cfg->sdrc_tr2 = (MASTER_CLOCK * 7);

	/* user reset enable */
	cfg->rstc_rmr =
		AT91_RSTC_KEY |
		AT91_RSTC_PROCRST |
		AT91_RSTC_RSTTYP_WAKEUP |
		AT91_RSTC_RSTTYP_WATCHDOG;
}

static void __bare_init usb_a9263_init(void)
{
	struct at91sam926x_board_cfg cfg;

	cfg.pio = IOMEM(AT91SAM9263_BASE_PIOD);
	cfg.sdramc = IOMEM(AT91SAM9263_BASE_SDRAMC0);
	cfg.ebi_pio_is_peripha = true;
	cfg.matrix_csa = AT91_MATRIX_EBI0CSA;

	usb_a9263_board_config(&cfg);
	at91sam926x_board_init(&cfg);

	barebox_arm_entry(AT91_CHIPSELECT_1, at91_get_sdram_size(cfg.sdramc),
	                  NULL);
}

void __naked __bare_init barebox_arm_reset_vector(void)
{
	arm_cpu_lowlevel_init();

	arm_setup_stack(AT91SAM9263_SRAM0_BASE + AT91SAM9263_SRAM0_SIZE - 16);

	usb_a9263_init();
}
