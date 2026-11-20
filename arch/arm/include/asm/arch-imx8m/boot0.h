/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Boot0 hook for the QEMU i.MX8M virtual boot ROMs.
 *
 * The ROMs execute in place from the read-only boot ROM region at 0x0. Copy
 * their .data section to RAM before anything else, see
 * board/emulation/qemu-imx/u-boot-spl-imx8m-rom.lds.
 */

#if IS_ENABLED(CONFIG_QEMU_IMX_ROM) && defined(CONFIG_XPL_BUILD)
	ldr	x0, 3f
	ldr	x1, 4f
	ldr	x2, 5f
1:	cmp	x1, x2
	b.hs	2f
	ldr	x3, [x0], #8
	str	x3, [x1], #8
	b	1b

	.align 3
3:	.quad	__rom_data_load
4:	.quad	__rom_data_start
5:	.quad	__rom_data_end
2:
#endif
	b	reset
