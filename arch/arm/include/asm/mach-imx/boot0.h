/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Boot0 hook for the QEMU virtual boot ROMs of the ARMv7 based i.MX SoCs.
 *
 * The ROMs execute in place from the read-only boot ROM region at 0x0. Copy
 * their .data section to RAM before anything else, see
 * board/emulation/qemu-imx/u-boot-spl-armv7-rom.lds. The copy routine lives
 * outside of .vectors since the ROMs provide interfaces at fixed locations
 * right after the vectors, e.g. the silicon revision at 0x48 on i.MX53.
 *
 * As required by arch/arm/lib/vectors.S, this defines _start and the vector
 * table.
 */

_start:
#if IS_ENABLED(CONFIG_QEMU_IMX_ROM) && defined(CONFIG_XPL_BUILD)
	b	rom_reset
	ldr	pc, _undefined_instruction
	ldr	pc, _software_interrupt
	ldr	pc, _prefetch_abort
	ldr	pc, _data_abort
	ldr	pc, _not_used
	ldr	pc, _irq
	ldr	pc, _fiq

	.pushsection .text.rom_reset, "ax"
	.arm
rom_reset:
	ldr	r0, =__rom_data_load
	ldr	r1, =__rom_data_start
	ldr	r2, =__rom_data_end
1:	cmp	r1, r2
	ldrlo	r3, [r0], #4
	strlo	r3, [r1], #4
	blo	1b
	b	reset
	.ltorg
	.popsection
#else
	ARM_VECTORS
#endif
