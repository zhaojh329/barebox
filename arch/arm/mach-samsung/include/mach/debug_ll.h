/*
 * Copyright (C) 2012
 * Jean-Christophe PLAGNIOL-VILLARD <planioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#ifndef __MACH_DEBUG_LL_H__
#define __MACH_DEBUG_LL_H__

#include <common.h>
#include <asm/io.h>

/* Note: Offsets are for little endian access */
#define ULCON 0x00		/* line control */
#define UCON 0x04		/* UART control */
#define UFCON 0x08		/* FIFO control */
#define UMCON 0x0c		/* modem control */
#define UTRSTAT 0x10		/* Rx/Tx status */
#define UERSTAT 0x14		/* error status */
#define UFSTAT 0x18		/* FIFO status */
#define UMSTAT 0x1c		/* modem status */
#define UTXH 0x20		/* transmitt */
#define URXH 0x24		/* receive */
#define UBRDIV 0x28		/* baudrate generator */
#define UBRDIVSLOT 0x2c		/* baudrate slot generator */
#define UINTM 0x38		/* interrupt mask register */

/*
 * Before you call this function, you should also
 * configure the corresponding GPIO of the serial port
 */
static inline void debug_ll_init(void)
{
	void __iomem *base = IOMEM(BOARD_DEBUG_LL_BASE);
	
	/* Disable FIFO */
	writeb(0x00, base + UFCON);

	/* Normal,No parity,1 stop,8 bit */
	writeb(0x03, base + ULCON);

	/* Interrupt request or polling mode/Normal transmit/Normal operation/PCLK/ */
	writew(1 | (1 << 2), base + UCON);

	writew(BOARD_UBRDIV_VAL, base + UBRDIV);
	writew(BOARD_UBRDIVSLOT_VAL, base + UBRDIVSLOT);
}

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 *
 * This does not append a newline
 */
static inline void PUTC_LL(char c)
{
	void __iomem *base = IOMEM(BOARD_DEBUG_LL_BASE);
	
	/* Wait for Tx FIFO not full */
	while (!(readb(base + UTRSTAT) & 0x2));

	writeb(c, base + UTXH);
}
#endif
