#include <asm/arch/sys_proto.h>

static u32 download_image(u8 *dest, u32 offset, u32 size, u32 xor)
{
    return 0;
}

static u32 query_boot_infor(u32 info_type, u32 *info, u32 xor)
{
    if ((info_type ^ (uintptr_t)info) != xor) {
        return 0;
    }

    switch (info_type) {
    case QUERY_BT_DEV:
        *info = (BT_DEV_TYPE_MMC << 16) | (2 << 8);
        return ROM_API_OKAY;
    case QUERY_PAGE_SZ:
        *info = 0x200;
        return ROM_API_OKAY;
    case QUERY_IVT_OFF:
        *info = 0;
        return ROM_API_OKAY;
    case QUERY_BT_STAGE:
        *info = BT_STAGE_PRIMARY;
        return ROM_API_OKAY;
    case QUERY_IMG_OFF:
        *info = 0x8000;
        return ROM_API_OKAY;
    }

    return 0;
}

// Define the struct with function pointers

// Create an instance of the struct and initialize it
struct rom_api func_ptrs __section(".myptrs") __used = {
    .ver = 1,
    .tag = 0,
    .download_image = download_image,
    .query_boot_infor = query_boot_infor,
};
