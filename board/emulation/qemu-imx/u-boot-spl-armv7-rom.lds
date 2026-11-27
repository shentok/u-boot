/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Linker script for the QEMU virtual boot ROMs of the ARMv7 based i.MX SoCs. The
 * image executes in place from the boot ROM at 0x0. Only .data and .bss are
 * located in RAM (.sdram), where .data is copied to by
 * arch/arm/include/asm/mach-imx/boot0.h.
 *
 * Based on arch/arm/cpu/u-boot-spl.lds:
 * Copyright (c) 2004-2008 Texas Instruments
 *
 * (C) Copyright 2002
 * Gary Jennejohn, DENX Software Engineering, <garyj@denx.de>
 */

#ifndef CONFIG_SPL_SEPARATE_BSS
#error "The virtual boot ROM requires CONFIG_SPL_SEPARATE_BSS"
#endif

MEMORY { .sram : ORIGIN = IMAGE_TEXT_BASE,
		LENGTH = IMAGE_MAX_SIZE }
MEMORY { .sdram : ORIGIN = CONFIG_SPL_BSS_START_ADDR,
		LENGTH = CONFIG_SPL_BSS_MAX_SIZE }

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
	__image_copy_start = ADDR(.text);
	.text : {
		*(.vectors)
		__vectors_end = .;

		/*
		 * Interfaces at fixed locations used by later boot stages:
		 * - i.MX53: silicon revision, see get_cpu_rev()
		 * - all: HAB ROM Vector Table, see HAB_RVT_BASE
		 * - i.MX7: pointer to the ROM's software info structure, see
		 *   get_boot_device()
		 */
#ifdef CONFIG_MX53
		. = 0x48;
		KEEP(*(.rom_si_rev))
#endif
#ifdef CONFIG_ROM_UNIFIED_SECTIONS
		. = 0x100;
#else
		. = 0x94;
#endif
		__rom_hab_rvt_start = .;
		KEEP(*(.rom_hab_rvt))
		__rom_hab_rvt_end = .;
#ifdef CONFIG_MX7
		. = 0x1e8;
		KEEP(*(.rom_sw_info_ptr))
#endif

		CPUDIR/start.o (.text*)
		*(.text*)
		*(.glue*)
	} >.sram

	.rodata : {
		. = ALIGN(4);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} >.sram

	.binman_sym_table : {
		. = ALIGN(4);
		__binman_sym_start = .;
		KEEP(*(SORT(.binman_sym*)));
		__binman_sym_end = .;
	} >.sram

	__u_boot_list : {
		. = ALIGN(4);
		KEEP(*(SORT(__u_boot_list*)));
	} >.sram

	.ARM.exidx : {
		*(.ARM.exidx*)
	} >.sram

	/* Resolved at link time, so these can stay in ROM */
	.got : {
		*(.got.plt)
		*(.got)
		. = ALIGN(8);
	} >.sram

	.data : {
		. = ALIGN(8);
		__rom_data_start = .;
		*(.data*)
		. = ALIGN(8);
		__rom_data_end = .;
	} >.sdram AT>.sram
	__rom_data_load = LOADADDR(.data);

	/* The device tree is appended to the image in ROM */
	__image_copy_end = LOADADDR(.data) + SIZEOF(.data);
	_image_binary_end = __image_copy_end;
	_end = __image_copy_end;

	.bss : {
		__bss_start = .;
		*(.bss*)
		. = ALIGN(8);
		__bss_end = .;
	} >.sdram
	__bss_size = __bss_end - __bss_start;

	/DISCARD/ : { *(.dynsym) }
	/DISCARD/ : { *(.dynbss) }
	/DISCARD/ : { *(.dynstr*) }
	/DISCARD/ : { *(.dynamic*) }
	/DISCARD/ : { *(.hash*) }
	/DISCARD/ : { *(.plt*) }
	/DISCARD/ : { *(.interp*) }
	/DISCARD/ : { *(.gnu*) }
}

ASSERT(__vectors_end <= 0x48, "vectors overlap the ROM interfaces");
ASSERT(__rom_hab_rvt_end > __rom_hab_rvt_start, "HAB ROM Vector Table missing");

ASSERT(_image_binary_end % 8 == 0, \
       "_image_binary_end must be 8-byte aligned for device tree");

ASSERT(__bss_end - __rom_data_start <= (CONFIG_SPL_BSS_MAX_SIZE), \
	"SPL image .data + .bss too big");
