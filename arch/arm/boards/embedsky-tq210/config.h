#define S5PCXX_CLOCK_REFERENCE 24000000

#define set_pll(mdiv, pdiv, sdiv)	(1<<31 | mdiv<<16 | pdiv<<8 | sdiv)

#define BOARD_APLL_VAL	set_pll(0x7d, 0x3, 0x1)
#define BOARD_MPLL_VAL	set_pll(0x29b, 0xc, 0x1)
#define BOARD_EPLL_VAL	set_pll(0x60, 0x6, 0x2)
#define BOARD_VPLL_VAL	set_pll(0x6c, 0x6, 0x3)

#define BOARD_CLK_DIV0_MASK	0xFFFFFFFF
#define BOARD_CLK_DIV0_VAL	0x14131440
#define BOARD_APLL_LOCKTIME	0x2cf

#define S5P_DRAM_WR		3
#define S5P_DRAM_CAS	5

/* A2MCLK = 200MHz, 5ns */
#define DMC_TIMING_AREF	0x00000618	/* 7.8us * 200MHz = 0x618 */

#define DMC_TIMING_ROW_HELPER(ras, rc, rcd, rp, rrd, rfc) \
	((ras) | ((rc) << 6) | ((rcd) << 12) | ((rp) << 16) | ((rrd) << 20) | ((rfc) << 24))

#define DMC_TIMING_DATA_HELPER(cl, rtp, wr, wtr) \
	(((cl) << 16) | ((rtp) << 20) | ((wr) << 24) | ((wtr) << 28))

#define DMC_TIMING_PWR_HELPER(mrd, cke, xp, xsr, faw) \
	((mrd) | ((cke) << 4) | ((xp) << 8) | ((xsr) << 16) | ((faw) << 24))
	
#define DMC_TIMING_ROW	DMC_TIMING_ROW_HELPER(9, 12, 3, 3, 2, 15)
#define DMC_TIMING_DATA	DMC_TIMING_DATA_HELPER(5, 2, 3, 2)
#define DMC_TIMING_PWR	DMC_TIMING_PWR_HELPER(2, 3, 2, 200, 10)

/* low level debug */
#define BOARD_DEBUG_LL_BASE		0xE2900000	/* UART1 (Start numbering from 1) */

/*
 * pclk_psys = 66700000 Hz
 * DIV_VAL = (66700000/(115200 x 16))-1 = 36.187 - 1 = 35.2
 * UBRDIV0 = 35
 * (num of 1's in UDIVSLOTn)/16 = 0.2
 * (num of 1's in UDIVSLOTn) = 3.2
 */
#define BOARD_UBRDIV_VAL		35
#define BOARD_UBRDIVSLOT_VAL	0x0888