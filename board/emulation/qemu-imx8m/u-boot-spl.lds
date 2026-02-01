/* SPDX-License-Identifier: GPL-2.0+ */
/*
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

MEMORY { .rom : ORIGIN = IMAGE_TEXT_BASE,
		LENGTH = IMAGE_MAX_SIZE }
MEMORY { .sram : ORIGIN = 0x900000,
        LENGTH = 0x18000 }

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
	.text : {
		. = ALIGN(8);
		CPUDIR/start.o (.text*)

        . = ORIGIN(.sram) + 0x980;
		__myptrs_start = .;
		KEEP(*(.myptrs));
		__myptrs_end = .;

		*(.text*)
	} >.rom

	.rodata : {
		. = ALIGN(8);
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} >.rom

    . = ORIGIN(.sram);

    __image_copy_start = ADDR(.data);
    .data ALIGN(8) : AT(ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 8)) {
		*(.__data_start)
		*(.data*)
    } >.sram

    .binman_sym_table ALIGN(8) : AT(ALIGN(LOADADDR(.data) + SIZEOF(.data), 8)) {
		__binman_sym_start = .;
		KEEP(*(SORT(.binman_sym*)));
		__binman_sym_end = .;
		. = ALIGN(8);
    } > .sram

    __u_boot_list ALIGN(8) : AT(ALIGN(LOADADDR(.binman_sym_table) + SIZEOF(.binman_sym_table), 8)) {
		KEEP(*(SORT(__u_boot_list*)));
		. = ALIGN(8);
    } >.sram

.got ALIGN(8) : AT(ALIGN(LOADADDR(__u_boot_list) + SIZEOF(__u_boot_list), 8)) {
    *(.got)
    *(.got.*)
} > .sram

	__image_copy_end = .;
	_end = .;
    _image_binary_end = LOADADDR(.got) + SIZEOF(.got);

    .bss : {
		__bss_start = .;
		*(.bss*)
		 . = ALIGN(8);
		__bss_end = .;
	} >.sram
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
