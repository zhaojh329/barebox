/*
 * based on linux.git/drivers/tty/serial/ar933x_uart.c
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

#ifndef __AR933X_DEBUG_LL__
#define __AR933X_DEBUG_LL__

#include <asm/addrspace.h>
#include <mach/ar71xx_regs.h>

#define DEBUG_LL_UART_ADDR	KSEG1ADDR(AR933X_UART_BASE)

#define AR933X_UART_DATA_REG            0x00
#define AR933X_UART_DATA_TX_RX_MASK     0xff
#define AR933X_UART_DATA_TX_CSR		0x200
#define AR933X_UART_DATA_RX_CSR		0x100

#ifndef __ASSEMBLY__

#include <io.h>

/*
 * C macros
 */

static inline void ar933x_debug_ll_writel(u32 b, int offset)
{
	__raw_writel(b, (u8 *)DEBUG_LL_UART_ADDR + offset);
}

static inline u32 ar933x_debug_ll_readl(int offset)
{
	return __raw_readl((u8 *)DEBUG_LL_UART_ADDR + offset);
}

static inline void PUTC_LL(int ch)
{
	u32 data;

	/* wait transmitter ready */
	data = ar933x_debug_ll_readl(AR933X_UART_DATA_REG);
	while (!(data & AR933X_UART_DATA_TX_CSR))
		data = ar933x_debug_ll_readl(AR933X_UART_DATA_REG);

	data = (ch & AR933X_UART_DATA_TX_RX_MASK) | AR933X_UART_DATA_TX_CSR;
	ar933x_debug_ll_writel(data, AR933X_UART_DATA_REG);
}
#else /* __ASSEMBLY__ */
/*
 * Macros for use in assembly language code
 */

#define AR933X_UART_CS_REG		0x04
#define UART_CS_REG	((KSEG1 | AR933X_UART_BASE) | AR933X_UART_CS_REG)
#define AR933X_UART_CS_IF_MODE_S	2
#define	  AR933X_UART_CS_IF_MODE_DCE	2
#define AR933X_UART_CS_TX_READY_ORIDE	BIT(7)
#define AR933X_UART_CS_RX_READY_ORIDE	BIT(8)

/* calculated by the function ar933x_uart_get_scale_step from drivers/serial/serial_ar933x.c */
#define CLOCK_25MHZ_SCALE	43
#define CLOCK_25MHZ_STEP	26575
#define CLOCK_40MHZ_SCALE	36
#define CLOCK_40MHZ_STEP	13967

#define AR933X_UART_CLOCK_REG		0x08
#define CLOCK_REG	((KSEG1 | AR933X_UART_BASE) | AR933X_UART_CLOCK_REG)

.macro debug_ll_ar9331_init
#ifdef CONFIG_DEBUG_LL

	pbl_reg_writel ((AR933X_UART_CS_IF_MODE_DCE << AR933X_UART_CS_IF_MODE_S) \
			| AR933X_UART_CS_TX_READY_ORIDE \
			| AR933X_UART_CS_RX_READY_ORIDE), UART_CS_REG

	li		t0, RESET_REG_BOOTSTRAP
	lw		t1, 0(t0)
	andi	t1, t1, AR933X_BOOTSTRAP_REF_CLK_40
	beqz	t1, 1f
	nop

	pbl_reg_writel ((CLOCK_40MHZ_SCALE << 16) | CLOCK_40MHZ_STEP), CLOCK_REG
	b 2f
	nop
1:
	pbl_reg_writel ((CLOCK_25MHZ_SCALE << 16) | CLOCK_25MHZ_STEP), CLOCK_REG
2:
#endif /* CONFIG_DEBUG_LL */
.endm

/*
 * output a character in a0
 */
.macro	debug_ll_outc_a0
#ifdef CONFIG_DEBUG_LL
	.set	push
	.set	reorder

	la	t0, DEBUG_LL_UART_ADDR
201:
	lw	t1, AR933X_UART_DATA_REG(t0)	/* get line status */
	andi	t1, t1, AR933X_UART_DATA_TX_CSR	/* check for transmitter empty */
	beqz	t1, 201b	/* try again */
	andi	a0, a0, AR933X_UART_DATA_TX_RX_MASK
	ori	a0, a0, AR933X_UART_DATA_TX_CSR
	sw	a0, 0(t0)	/* write the character */
	.set	pop
#endif /* CONFIG_DEBUG_LL */
.endm

/*
 * output a character
 */
.macro	debug_ll_outc chr
#ifdef CONFIG_DEBUG_LL
	li	a0, \chr
	debug_ll_outc_a0
#endif /* CONFIG_DEBUG_LL */
.endm

/*
 * check character in input buffer
 * return value:
 *  v0 = 0   no character in input buffer
 *  v0 != 0  character in input buffer
 */
/* FIXME: use tstc */
.macro	debug_ll_tstc
#ifdef CONFIG_DEBUG_LL
	.set	push
	.set	reorder

	la	t0, DEBUG_LL_UART_ADDR

	/* get line status and check for data present */
	lw	v0, AR933X_UART_DATA_REG(t0)
	andi	v0, v0, AR933X_UART_DATA_RX_CSR

	.set	pop
#endif /* CONFIG_DEBUG_LL */
.endm

/*
 * get character to v0
 */
.macro	debug_ll_getc
#ifdef CONFIG_DEBUG_LL
	.set	push
	.set	reorder

	la	t0, DEBUG_LL_UART_ADDR
204:
	lw	v0, AR933X_UART_DATA_REG(t0)
	andi	v0, v0, AR933X_UART_DATA_RX_CSR

	/* try again */
	beqz	v0, 204b

	/* read a character */
	lw	v0, AR933X_UART_DATA_REG(t0)
	andi	v0, v0, AR933X_UART_DATA_TX_RX_MASK

	/* remove the character from the FIFO */
	li	t1, AR933X_UART_DATA_RX_CSR
	sw  t1, AR933X_UART_DATA_REG(t0)

	.set	pop
#endif /* CONFIG_DEBUG_LL */
.endm
#endif /* __ASSEMBLY__ */

#endif /* __AR933X_DEBUG_LL__ */
