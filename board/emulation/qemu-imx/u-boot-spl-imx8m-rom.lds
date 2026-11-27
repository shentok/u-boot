/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Linker script for the QEMU i.MX8M virtual boot ROMs. The image executes in
 * place from the boot ROM at 0x0. Only .data and .bss are located in RAM
 * (.sdram), where .data is copied to by arch/arm/include/asm/arch-imx8m/boot0.h.
 *
 * (C) Copyright 2013
 * David Feng <fenghua@phytium.com.cn>
 *
 * (C) Copyright 2002
 * Gary Jennejohn, DENX Software Engineering, <garyj@denx.de>
 *
 * (C) Copyright 2010
 * Texas Instruments, <www.ti.com>
 *	Aneesh V <aneesh@ti.com>
 */

#ifndef CONFIG_SPL_SEPARATE_BSS
#error "The virtual boot ROM requires CONFIG_SPL_SEPARATE_BSS"
#endif

MEMORY { .sram : ORIGIN = IMAGE_TEXT_BASE,
		LENGTH = IMAGE_MAX_SIZE }
#ifdef CONFIG_SPL_SEPARATE_BSS
MEMORY { .sdram : ORIGIN = CONFIG_SPL_BSS_START_ADDR,
		LENGTH = CONFIG_SPL_BSS_MAX_SIZE }
#endif

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
	__image_copy_start = ADDR(.text);
	.text : {
		. = ALIGN(8);
		CPUDIR/start.o (.text*)

		/*
		 * Interfaces at fixed locations used by SPL and U-Boot proper,
		 * see HAB_RVT_BASE, g_rom_api and get_boot_device():
		 * - all: HAB ROM Vector Table
		 * - i.MX8MN/8MP: ROM API table
		 * - all: pointer to the ROM's software info structure
		 */
		. = ORIGIN(.sram) + 0x900;
		KEEP(*(.rom_hab_rvt));
		. = ORIGIN(.sram) + 0x980;
		KEEP(*(.rom_api));
		. = ORIGIN(.sram) + 0x9e8;
		KEEP(*(.rom_sw_info_ptr));

		*(.text*)
	} >.sram

	.rodata : {
		. = ALIGN(8);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} >.sram

	.binman_sym_table : {
		. = ALIGN(8);
		__binman_sym_start = .;
		KEEP(*(SORT(.binman_sym*)));
		__binman_sym_end = .;
		. = ALIGN(8);
	} > .sram

	__u_boot_list : {
		. = ALIGN(8);
		KEEP(*(SORT(__u_boot_list*)));
		. = ALIGN(8);
	} >.sram

	/* Resolved at link time, so these can stay in ROM */
	.got : {
		. = ALIGN(8);
		*(.got.plt)
		*(.got)
	} >.sram

	.data : {
		. = ALIGN(8);
		__rom_data_start = .;
		*(.__data_start)
		*(.data*)
		. = ALIGN(8);
		__rom_data_end = .;
	} >.sdram AT>.sram
	__rom_data_load = LOADADDR(.data);

	/* The device tree is appended to the image in ROM */
	__image_copy_end = LOADADDR(.data) + SIZEOF(.data);
	_end = __image_copy_end;
	_image_binary_end = __image_copy_end;

#ifdef CONFIG_SPL_SEPARATE_BSS
	.bss : {
		__bss_start = .;
		*(.bss*)
		. = ALIGN(8);
		__bss_end = .;
	} >.sdram
#else
	.bss _image_binary_end (OVERLAY) : {
		__bss_start = .;
		*(.bss*)
		 . = ALIGN(8);
		__bss_end = .;
	} >.sram
#endif
	__bss_size = __bss_end - __bss_start;

	/DISCARD/ : { *(.rela*) }
	/DISCARD/ : { *(.dynsym) }
	/DISCARD/ : { *(.dynstr*) }
	/DISCARD/ : { *(.dynamic*) }
	/DISCARD/ : { *(.plt*) }
	/DISCARD/ : { *(.interp*) }
	/DISCARD/ : { *(.gnu*) }

#ifdef CONFIG_LINUX_KERNEL_IMAGE_HEADER
#include "linux-kernel-image-header-vars.h"
#endif
}

ASSERT(_image_binary_end % 8 == 0, \
       "_image_binary_end must be 8-byte aligned for device tree");

ASSERT(ADDR(.bss) % 8 == 0, \
       ".bss must be 8-byte aligned");
