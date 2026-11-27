/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * QEMU i.MX6 virtual boot ROMs
 */

#ifndef __QEMU_IMX6_ROM_H
#define __QEMU_IMX6_ROM_H

#include "mx6_common.h"

/*
 * Only the SPL is used as boot ROM. The following is just needed for building
 * U-Boot proper, which is otherwise unused.
 */
#define PHYS_SDRAM			MMDC0_ARB_BASE_ADDR
#define CFG_SYS_SDRAM_BASE		PHYS_SDRAM
#define CFG_SYS_INIT_RAM_ADDR		IRAM_BASE_ADDR
#define CFG_SYS_INIT_RAM_SIZE		IRAM_SIZE

#endif
