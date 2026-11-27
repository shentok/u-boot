// SPDX-License-Identifier: GPL-2.0+
/*
 * High Assurance Boot (HAB) services of the virtual i.MX boot ROMs
 *
 * Later boot stages call the HAB functions through the ROM Vector Table (RVT)
 * at HAB_RVT_BASE: 0x900 on i.MX8M, where SPL calls them directly and U-Boot
 * proper via TF-A, and 0x94 on i.MX53. The functions execute in the context
 * of their caller and switch to the ROM's global data pointer. The caller's
 * global data pointer is restored before returning, since TF-A doesn't do
 * that.
 *
 * Whether the device is open or closed is determined by the SEC_CONFIG fuse,
 * see rom_hab_closed().
 * Image verification isn't implemented: authenticate_image() records a
 * failure event, and only succeeds on open devices which don't enforce
 * authentication.
 */

#include <asm/global_data.h>
#include <asm/mach-imx/hab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "qemu_imx_rom.h"

DECLARE_GLOBAL_DATA_PTR;

#define HAB_VERSION			0x43
#define HAB_MAX_EVENTS			8
#define HAB_EVENT_DATA_SIZE		24

struct hab_event {
	struct hab_hdr hdr;
	u8 sts;
	u8 rsn;
	u8 ctx;
	u8 eng;
	u8 data[HAB_EVENT_DATA_SIZE];
} __packed;

struct hab_rvt {
	struct hab_hdr hdr;
	/* Naturally aligned, i.e. at offset 8 on 64-bit SoCs */
	void *entry;
	void *exit;
	void *check_target;
	void *authenticate_image;
	void *run_dcd;
	void *run_csf;
	void *assert;
	void *report_event;
	void *report_status;
	void *failsafe;
};

static struct {
	struct hab_event events[HAB_MAX_EVENTS];
	uint num_events;
} hab;

static enum hab_config hab_config(void)
{
	return rom_hab_closed() ? HAB_CFG_CLOSED : HAB_CFG_OPEN;
}

static void hab_log_event(enum hab_status sts, enum hab_reason rsn,
			  enum hab_context ctx, const void *data, size_t len)
{
	struct hab_event *ev;

	if (hab.num_events >= HAB_MAX_EVENTS)
		return;

	len = min(len, sizeof(ev->data));
	ev = &hab.events[hab.num_events++];
	ev->hdr.tag = HAB_TAG_EVT;
	ev->hdr.len[0] = 0;
	ev->hdr.len[1] = offsetof(struct hab_event, data) + len;
	ev->hdr.par = HAB_VERSION;
	ev->sts = sts;
	ev->rsn = rsn;
	ev->ctx = ctx;
	ev->eng = HAB_ENG_SW;
	memcpy(ev->data, data, len);
}

static size_t hab_event_len(const struct hab_event *ev)
{
	return ev->hdr.len[0] << 8 | ev->hdr.len[1];
}

static enum hab_status hab_entry(void)
{
	return HAB_SUCCESS;
}

static enum hab_status hab_exit(void)
{
	return HAB_SUCCESS;
}

static enum hab_status hab_check_target(enum hab_target type, const void *start,
					size_t bytes)
{
	ulong addr = (ulong)start;

	if (type == HAB_TGT_PERIPHERAL)
		return HAB_SUCCESS;

	if (addr < ROM_END || addr + bytes < addr) {
		u32 data[2] = { addr, bytes };

		hab_log_event(HAB_FAILURE, HAB_INV_ADDRESS, HAB_CTX_TARGET,
			      data, sizeof(data));
		return HAB_FAILURE;
	}

	return HAB_SUCCESS;
}

static void *hab_authenticate_image(u8 cid, ptrdiff_t ivt_offset, void **start,
				    size_t *bytes, hab_loader_callback_f_t loader)
{
	const struct ivt *ivt;

	if (!start || !*start || !bytes) {
		hab_log_event(HAB_FAILURE, HAB_INV_ADDRESS, HAB_CTX_AUTHENTICATE,
			      NULL, 0);
		return NULL;
	}

	ivt = *start + ivt_offset;
	if (ivt->hdr.magic != IVT_HEADER_MAGIC ||
	    (ivt->hdr.version & HAB_MAJ_MASK) != HAB_MAJ_VER) {
		hab_log_event(HAB_FAILURE, HAB_INV_IVT, HAB_CTX_AUTHENTICATE,
			      ivt, sizeof(ivt->hdr));
		return NULL;
	}

	/* Image verification isn't implemented */
	hab_log_event(HAB_FAILURE, HAB_UNS_PROTOCOL, HAB_CTX_AUTHENTICATE,
		      &ivt->csf, sizeof(ivt->csf));

	return hab_config() == HAB_CFG_OPEN ? *start : NULL;
}

static enum hab_status hab_run_dcd(const u8 *dcd)
{
	hab_log_event(HAB_FAILURE, HAB_UNS_COMMAND, HAB_CTX_DCD, NULL, 0);
	return HAB_FAILURE;
}

static enum hab_status hab_run_csf(const u8 *csf, u8 cid, u32 srkmask)
{
	hab_log_event(HAB_FAILURE, HAB_UNS_COMMAND, HAB_CTX_CSF, NULL, 0);
	return HAB_FAILURE;
}

static enum hab_status hab_assert(u32 type, const void *data, u32 count)
{
	return HAB_SUCCESS;
}

static enum hab_status hab_report_event(enum hab_status status, u32 index,
					u8 *event, size_t *bytes)
{
	const struct hab_event *ev;
	size_t len;
	uint i;

	if (!bytes)
		return HAB_FAILURE;

	for (i = 0; i < hab.num_events; i++) {
		ev = &hab.events[i];
		if (status != HAB_STS_ANY && ev->sts != status)
			continue;
		if (index--)
			continue;

		len = hab_event_len(ev);
		if (event) {
			if (*bytes < len)
				return HAB_FAILURE;
			memcpy(event, ev, len);
		}
		*bytes = len;

		return HAB_SUCCESS;
	}

	return HAB_FAILURE;
}

static enum hab_status hab_report_status(enum hab_config *config,
					 enum hab_state *state)
{
	enum hab_status ret = HAB_SUCCESS;
	uint i;

	for (i = 0; i < hab.num_events; i++) {
		if (hab.events[i].sts == HAB_FAILURE)
			ret = HAB_FAILURE;
		else if (hab.events[i].sts == HAB_WARNING && ret == HAB_SUCCESS)
			ret = HAB_WARNING;
	}

	if (config)
		*config = hab_config();
	if (state)
		*state = HAB_STATE_NONSECURE;

	return ret;
}

static void hab_failsafe(void)
{
	while (1)
		;
}

/*
 * Wrappers executing the HAB functions with the ROM's global data pointer,
 * restoring the caller's one afterwards
 */
#define HAB_CALL(ret_type, fn, ...)			\
	({						\
		gd_t *caller_gd = (gd_t *)gd;		\
		ret_type ret;				\
							\
		set_gd(rom_gd);				\
		ret = fn(__VA_ARGS__);			\
		set_gd(caller_gd);			\
		ret;					\
	})

static enum hab_status rvt_entry(void)
{
	return HAB_CALL(enum hab_status, hab_entry);
}

static enum hab_status rvt_exit(void)
{
	return HAB_CALL(enum hab_status, hab_exit);
}

static enum hab_status rvt_check_target(enum hab_target type, const void *start,
					size_t bytes)
{
	return HAB_CALL(enum hab_status, hab_check_target, type, start, bytes);
}

static void *rvt_authenticate_image(u8 cid, ptrdiff_t ivt_offset, void **start,
				    size_t *bytes, hab_loader_callback_f_t loader)
{
	return HAB_CALL(void *, hab_authenticate_image, cid, ivt_offset, start,
			bytes, loader);
}

static enum hab_status rvt_run_dcd(const u8 *dcd)
{
	return HAB_CALL(enum hab_status, hab_run_dcd, dcd);
}

static enum hab_status rvt_run_csf(const u8 *csf, u8 cid, u32 srkmask)
{
	return HAB_CALL(enum hab_status, hab_run_csf, csf, cid, srkmask);
}

static enum hab_status rvt_assert(u32 type, const void *data, u32 count)
{
	return HAB_CALL(enum hab_status, hab_assert, type, data, count);
}

static enum hab_status rvt_report_event(enum hab_status status, u32 index,
					u8 *event, size_t *bytes)
{
	return HAB_CALL(enum hab_status, hab_report_event, status, index, event,
			bytes);
}

static enum hab_status rvt_report_status(enum hab_config *config,
					 enum hab_state *state)
{
	return HAB_CALL(enum hab_status, hab_report_status, config, state);
}

/* Located by later boot stages at HAB_RVT_BASE */
static const struct hab_rvt hab_rvt __section(".rom_hab_rvt") __used = {
	.hdr = {
		.tag = HAB_TAG_RVT,
		.len = { 0, sizeof(struct hab_rvt) },
		.par = HAB_VERSION,
	},
	.entry = rvt_entry,
	.exit = rvt_exit,
	.check_target = rvt_check_target,
	.authenticate_image = rvt_authenticate_image,
	.run_dcd = rvt_run_dcd,
	.run_csf = rvt_run_csf,
	.assert = rvt_assert,
	.report_event = rvt_report_event,
	.report_status = rvt_report_status,
	.failsafe = hab_failsafe,
};
