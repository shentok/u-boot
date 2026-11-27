// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtual i.MX boot ROMs for QEMU
 *
 * This SPL is loaded into the boot ROM region at 0x0 with QEMU's -bios option
 * and executes in place. Like the real ROM it determines the boot device, reads
 * the i.MX boot image from it, executes the image's DCD, copies the boot data
 * to the location given in the IVT and jumps to its entry point.
 *
 * Out of scope: plugins, HAB, serial download, NAND, (Q)SPI.
 */

#include <blk.h>
#include <errno.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <mmc.h>
#include <spl.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <linux/string.h>
#include "qemu_imx_rom.h"

DECLARE_GLOBAL_DATA_PTR;

#if IS_ENABLED(CONFIG_XPL_BUILD)

#define IVT_TAG				0xd1
#define HAB_VERSION_MASK		0xf0
#define HAB_VERSION			0x40

#define DCD_TAG				0xd2
#define DCD_MAX_SIZE			1768
#define DCD_CMD_WRITE			0xcc
#define DCD_CMD_CHECK			0xcf
#define DCD_CMD_NOP			0xc0
#define DCD_PARAM_WIDTH			GENMASK(2, 0)
#define DCD_PARAM_MASK			BIT(3)
#define DCD_PARAM_SET			BIT(4)
#define DCD_CHECK_MAX_POLLS		1000000

struct imx_ivt {
	u8 tag;
	u8 len[2];	/* big endian */
	u8 version;
	u32 entry;
	u32 reserved1;
	u32 dcd;
	u32 boot_data;
	u32 self;
	u32 csf;
	u32 reserved2;
};

struct imx_boot_data {
	u32 start;
	u32 size;
	u32 plugin;
};

struct rom_boot_dev rom_boot_dev;
struct rom_loader rom_loader;
gd_t *rom_gd;

void board_boot_order(u32 *spl_boot_list)
{
	if (rom_boot_dev.try_all) {
		spl_boot_list[0] = BOOT_DEVICE_MMC1;
		spl_boot_list[1] = BOOT_DEVICE_MMC2;
		spl_boot_list[2] = BOOT_DEVICE_NONE;
		return;
	}

	switch (rom_mmc_devnum()) {
	case 0:
		spl_boot_list[0] = BOOT_DEVICE_MMC1;
		break;
	case 1:
		spl_boot_list[0] = BOOT_DEVICE_MMC2;
		break;
	default:
		spl_boot_list[0] = BOOT_DEVICE_NONE;
		break;
	}
}

static u32 rom_dcd_read(ulong addr, uint width)
{
	switch (width) {
	case 1:
		return readb(addr);
	case 2:
		return readw(addr);
	default:
		return readl(addr);
	}
}

static void rom_dcd_write(ulong addr, uint width, u32 val)
{
	switch (width) {
	case 1:
		writeb(val, addr);
		break;
	case 2:
		writew(val, addr);
		break;
	default:
		writel(val, addr);
		break;
	}
}

static int rom_dcd_write_cmd(const u8 *p, uint len, u8 param)
{
	uint width = FIELD_GET(DCD_PARAM_WIDTH, param);
	const u8 *end = p + len;

	for (p += 4; p + 8 <= end; p += 8) {
		ulong addr = get_unaligned_be32(p);
		u32 val = get_unaligned_be32(p + 4);

		if (param & DCD_PARAM_MASK) {
			u32 reg = rom_dcd_read(addr, width);

			val = param & DCD_PARAM_SET ? reg | val : reg & ~val;
		}

		rom_dcd_write(addr, width, val);
	}

	return 0;
}

static int rom_dcd_check_cmd(const u8 *p, uint len, u8 param)
{
	uint width = FIELD_GET(DCD_PARAM_WIDTH, param);
	ulong addr = get_unaligned_be32(p + 4);
	u32 mask = get_unaligned_be32(p + 8);
	u32 polls = len >= 16 ? get_unaligned_be32(p + 12) : DCD_CHECK_MAX_POLLS;

	while (polls--) {
		u32 reg = rom_dcd_read(addr, width) & mask;
		bool done;

		if (param & DCD_PARAM_MASK)
			done = param & DCD_PARAM_SET ? reg != 0 : reg != mask;
		else
			done = param & DCD_PARAM_SET ? reg == mask : reg == 0;

		if (done)
			return 0;
	}

	log_err("ROM: DCD check of 0x%08lx timed out\n", addr);
	return -ETIMEDOUT;
}

/* Executes the Device Configuration Data, e.g. to initialize the DRAM */
static int rom_run_dcd(const u8 *dcd)
{
	uint len = get_unaligned_be16(dcd + 1);
	const u8 *end = dcd + len;
	const u8 *p;
	int ret;

	if (dcd[0] != DCD_TAG || (dcd[3] & HAB_VERSION_MASK) != HAB_VERSION ||
	    len > DCD_MAX_SIZE) {
		log_err("ROM: invalid DCD\n");
		return -EINVAL;
	}

	for (p = dcd + 4; p + 4 <= end; p += len) {
		u8 param = p[3];

		len = get_unaligned_be16(p + 1);
		if (len < 4 || p + len > end) {
			log_err("ROM: malformed DCD command\n");
			return -EINVAL;
		}

		switch (p[0]) {
		case DCD_CMD_WRITE:
			ret = rom_dcd_write_cmd(p, len, param);
			break;
		case DCD_CMD_CHECK:
			ret = len >= 12 ? rom_dcd_check_cmd(p, len, param) : -EINVAL;
			break;
		case DCD_CMD_NOP:
			ret = 0;
			break;
		default:
			log_err("ROM: unsupported DCD command 0x%02x\n", p[0]);
			ret = -EINVAL;
			break;
		}

		if (ret)
			return ret;
	}

	return 0;
}

/* Reads the DCD, which follows the IVT, from the boot device and executes it */
static int rom_load_and_run_dcd(const struct imx_ivt *ivt,
				const struct spl_boot_device *bootdev)
{
	static u8 buf[DCD_MAX_SIZE + 2 * 512] __aligned(ARCH_DMA_MINALIGN);
	struct blk_desc *desc;
	struct mmc *mmc;
	ulong offset;

	if (ivt->dcd < ivt->self)
		return -EINVAL;

	mmc = find_mmc_device(bootdev->boot_device == BOOT_DEVICE_MMC2 ? 1 : 0);
	desc = mmc ? mmc_get_blk_desc(mmc) : NULL;
	if (!desc)
		return -ENODEV;

	offset = CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR * 512 +
		 (ivt->dcd - ivt->self);
	if (blk_dread(desc, offset / 512, sizeof(buf) / 512, buf) !=
	    sizeof(buf) / 512)
		return -EIO;

	return rom_run_dcd(buf + offset % 512);
}

/*
 * The boot image starts with the IVT at CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR.
 * Load everything from the IVT up to the end of the boot data. The part of the
 * boot data in front of the IVT is padding only.
 */
int spl_parse_board_header(struct spl_image_info *spl_image,
			   const struct spl_boot_device *bootdev,
			   const void *image_header, size_t size)
{
	const struct imx_ivt *ivt = image_header;
	const struct imx_boot_data *bd;
	u32 bd_offset;
	int ret;

	if (ivt->tag != IVT_TAG ||
	    (ivt->version & HAB_VERSION_MASK) != HAB_VERSION) {
		log_err("ROM: no valid IVT found\n");
		return -EINVAL;
	}

	bd_offset = ivt->boot_data - ivt->self;
	if (ivt->boot_data < ivt->self ||
	    bd_offset + sizeof(*bd) > size) {
		log_err("ROM: boot data not adjacent to IVT\n");
		return -EINVAL;
	}

	bd = image_header + bd_offset;
	if (bd->plugin) {
		log_err("ROM: plugin images are not supported\n");
		return -EINVAL;
	}

	if (ivt->self < bd->start || ivt->self - bd->start >= bd->size ||
	    ivt->entry < ivt->self || ivt->entry >= bd->start + bd->size) {
		log_err("ROM: inconsistent IVT/boot data\n");
		return -EINVAL;
	}

	if (ivt->dcd) {
		ret = rom_load_and_run_dcd(ivt, bootdev);
		if (ret) {
			log_err("ROM: DCD failed: %d\n", ret);
			return ret;
		}
	}

	spl_image->name = "loader";
	spl_image->os = IH_OS_U_BOOT;
	spl_image->offset = 0;
	spl_image->load_addr = ivt->self;
	spl_image->entry_point = ivt->entry;
	spl_image->size = bd->size - (ivt->self - bd->start);

	rom_loader.start = spl_image->load_addr;
	rom_loader.size = spl_image->size;

	log_debug("ROM: IVT entry=0x%08x self=0x%08x size=0x%x\n",
		  ivt->entry, ivt->self, spl_image->size);

	return 0;
}

void board_init_f(ulong dummy)
{
	int ret;

	/* Never returning to crt0, so clear .bss here */
	memset(__bss_start, 0, __bss_end - __bss_start);

	rom_gd = (gd_t *)gd;
	rom_detect_boot_device();

	timer_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
		hang();
	}

	preloader_console_init();

	board_init_r(NULL, 0);
}

#else /* U-Boot proper is built but unused */

int board_init(void)
{
	return 0;
}

#endif
