#define pr_fmt(fmt) "I2SE Duckbill: " fmt
#define DEBUG

#include <common.h>
#include <linux/sizes.h>
#include <asm/barebox-arm-head.h>
#include <asm/barebox-arm.h>
#include <mach/imx28-regs.h>
#include <mach/init.h>
#include <io.h>
#include <debug_ll.h>
#include <mach/iomux.h>
#include <stmp-device.h>

extern char __dtb_imx28_duckbill_start[];

ENTRY_FUNCTION(start_barebox_duckbill, r0, r1, r2)
{
	void *fdt;

	pr_debug("here we are!\n");

	fdt = __dtb_imx28_duckbill_start + get_runtime_offset();

	barebox_arm_entry(IMX_MEMORY_BASE, SZ_128M, fdt);
}

static const uint32_t iomux_pads[] = {
	/* EMI */
	EMI_DATA0, EMI_DATA1, EMI_DATA2, EMI_DATA3, EMI_DATA4, EMI_DATA5,
	EMI_DATA6, EMI_DATA7, EMI_DATA8, EMI_DATA9, EMI_DATA10, EMI_DATA11,
	EMI_DATA12, EMI_DATA13, EMI_DATA14, EMI_DATA15, EMI_ODT0, EMI_DQM0,
	EMI_ODT1, EMI_DQM1, EMI_DDR_OPEN_FB, EMI_CLK, EMI_DSQ0, EMI_DSQ1,
	EMI_DDR_OPEN, EMI_A0, EMI_A1, EMI_A2, EMI_A3, EMI_A4, EMI_A5,
	EMI_A6, EMI_A7, EMI_A8, EMI_A9, EMI_A10, EMI_A11, EMI_A12, EMI_A13,
	EMI_A14, EMI_BA0, EMI_BA1, EMI_BA2, EMI_CASN, EMI_RASN, EMI_WEN,
	EMI_CE0N, EMI_CE1N, EMI_CKE,

	/* Debug UART */
	PWM0_DUART_RX | VE_3_3V,
	PWM1_DUART_TX | VE_3_3V,
};

static noinline void duckbill_init(void)
{
	int i;

	/* initialize muxing */
	for (i = 0; i < ARRAY_SIZE(iomux_pads); i++)
		imx_gpio_mode(iomux_pads[i]);

	pr_debug("initializing power...\n");

	mx28_power_init(0, 0, 1);

	pr_debug("initializing SDRAM...\n");

	mx28_mem_init(mx28_dram_vals_default);

	pr_debug("DONE\n");
}

ENTRY_FUNCTION(prep_start_barebox_duckbill, r0, r1, r2)
{
	void (*back)(unsigned long) = (void *)get_lr();

	relocate_to_current_adr();
	setup_c();

	duckbill_init();

	back(0);
}
