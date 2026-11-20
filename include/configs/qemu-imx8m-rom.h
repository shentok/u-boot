/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * QEMU i.MX8M virtual boot ROMs
 */

#ifndef __QEMU_IMX8M_ROM_H
#define __QEMU_IMX8M_ROM_H

#include <linux/sizes.h>
#include <asm/arch/imx-regs.h>

/*
 * Only the SPL is used as boot ROM. The following is just needed for building
 * U-Boot proper, which is otherwise unused.
 */
#define CFG_SYS_INIT_RAM_ADDR	0x40000000
#define CFG_SYS_INIT_RAM_SIZE	0x200000
#define CFG_SYS_SDRAM_BASE	0x40000000
#define PHYS_SDRAM		0x40000000
#define PHYS_SDRAM_SIZE		0x80000000

#endif
