/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * QEMU i.MX53 virtual boot ROM
 */

#ifndef __QEMU_IMX53_ROM_H
#define __QEMU_IMX53_ROM_H

#include <asm/arch/imx-regs.h>

#define CFG_MXC_UART_BASE		UART1_BASE

/*
 * Only the SPL is used as boot ROM. The following is just needed for building
 * U-Boot proper, which is otherwise unused.
 */
#define PHYS_SDRAM_1			CSD0_BASE_ADDR
#define CFG_SYS_SDRAM_BASE		PHYS_SDRAM_1
#define CFG_SYS_INIT_RAM_ADDR		IRAM_BASE_ADDR
#define CFG_SYS_INIT_RAM_SIZE		IRAM_SIZE

#endif
