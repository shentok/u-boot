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
 * Images are authenticated by executing the HAB4 Command Sequence File (CSF)
 * referenced by their IVT:
 * - Install SRK: The SHA-256 digest over the digests of the keys in the Super
 *   Root Key table must match the SRK_HASH fuses.
 * - Install CSFK/Install Key: X.509 certificates verified with an installed
 *   key, usually the SRK
 * - Authenticate CSF/Authenticate Data: CMS signatures over the CSF commands
 *   respectively the given blocks
 * Only RSA keys and SHA-256 digests are supported.
 *
 * Whether the device is open or closed is determined by the SEC_CONFIG fuse.
 * The SoC specific parts provide the fuses, see rom_hab_closed() and
 * rom_hab_srk_hash().
 * Like the real ROM, failures are only enforced on closed devices, while they
 * are recorded as events on both.
 */

#include <hang.h>
#include <log.h>
#include <malloc.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/unaligned.h>
#include <asm/mach-imx/hab.h>
#include <crypto/pkcs7.h>
#include <crypto/pkcs7_parser.h>
#include <crypto/public_key.h>
#include <crypto/x509_parser.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <u-boot/sha256.h>
#include "qemu_imx_rom.h"

DECLARE_GLOBAL_DATA_PTR;

#define HAB_VERSION			0x43
#define HAB_MAX_EVENTS			8
#define HAB_EVENT_DATA_SIZE		24

/* HAB4 data structures and commands, see the CST documentation */
#define HAB_TAG_CSF			0xd4
#define HAB_TAG_CRT			0xd7
#define HAB_TAG_SIG			0xd8
#define HAB_TAG_KEY_PUB			0xe1
#define HAB_CMD_INS_KEY			0xbe
#define HAB_CMD_AUT_DAT			0xca
#define HAB_CMD_UNLK			0xb2
#define HAB_CMD_NOP			0xc0
#define HAB_PCL_SRK			0x03
#define HAB_PCL_X509			0x09
#define HAB_PCL_CMS			0xc5
#define HAB_ALG_SHA256			0x17
#define HAB_INS_KEY_CSF			BIT(1)

#define HAB_KEY_SRK			0
#define HAB_KEY_CSFK			1
#define HAB_NUM_KEYS			5
#define HAB_MAX_SRKS			4
#define HAB_MAX_BLOCKS			8

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

struct hab_region {
	ulong start;
	size_t size;
};

/* State while executing a CSF */
struct hab_csf_state {
	const u8 *csf;
	size_t csf_len;
	struct public_key *keys[HAB_NUM_KEYS];
	bool csf_authenticated;
	/* Regions authenticated by Authenticate Data commands */
	struct hab_region auth[HAB_MAX_BLOCKS];
	uint num_auth;
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

static size_t hab_hdr_len(const u8 *hdr)
{
	return get_unaligned_be16(hdr + 1);
}

/* Returns the payload of a data structure in the CSF, e.g. a certificate */
static const u8 *hab_csf_data(struct hab_csf_state *st, u32 offset, u8 tag,
			      size_t *len)
{
	const u8 *hdr = st->csf + offset;

	if (offset + 4 > st->csf_len || hdr[0] != tag ||
	    (hdr[3] & HAB_MAJ_MASK) != HAB_MAJ_VER ||
	    hab_hdr_len(hdr) < 4 || offset + hab_hdr_len(hdr) > st->csf_len)
		return NULL;

	*len = hab_hdr_len(hdr) - 4;
	return hdr + 4;
}

/* Encodes a DER INTEGER, prepending a zero byte for positive numbers */
static u8 *hab_der_int(u8 *p, const u8 *val, size_t len)
{
	bool pad = val[0] & 0x80;
	size_t total = len + pad;

	*p++ = 0x02;
	*p++ = 0x82;
	put_unaligned_be16(total, p);
	p += 2;
	if (pad)
		*p++ = 0;
	memcpy(p, val, len);

	return p + len;
}

/* Creates a public key from an RSA public key entry of the SRK table */
static struct public_key *hab_srk_to_key(const u8 *entry, size_t len)
{
	struct public_key *key;
	size_t mod_len, exp_len;
	u8 *der, *p;

	/* Header, reserved, flags, modulus and exponent lengths */
	if (len < 12 || entry[0] != HAB_TAG_KEY_PUB)
		return NULL;

	mod_len = get_unaligned_be16(entry + 8);
	exp_len = get_unaligned_be16(entry + 10);
	if (!mod_len || !exp_len || 12 + mod_len + exp_len > len)
		return NULL;

	/* DER encoded RSAPublicKey: SEQUENCE { INTEGER n, INTEGER e } */
	der = malloc(4 + 2 * 5 + mod_len + exp_len);
	key = calloc(1, sizeof(*key));
	if (!der || !key) {
		free(der);
		free(key);
		return NULL;
	}

	p = hab_der_int(der + 4, entry + 12, mod_len);
	p = hab_der_int(p, entry + 12 + mod_len, exp_len);
	der[0] = 0x30;
	der[1] = 0x82;
	put_unaligned_be16(p - der - 4, der + 2);

	key->key = der;
	key->keylen = p - der;
	key->pkey_algo = "rsa";
	key->id_type = "X509";

	return key;
}

static bool hab_srk_hash_matches(const u8 *digest)
{
	u8 fuses[SHA256_SUM_LEN];

	rom_hab_srk_hash(fuses);

	return !memcmp(fuses, digest, SHA256_SUM_LEN);
}

static enum hab_reason hab_install_srk(struct hab_csf_state *st, const u8 *cmd)
{
	u8 digests[HAB_MAX_SRKS][SHA256_SUM_LEN];
	u8 digest[SHA256_SUM_LEN];
	const u8 *table, *entry = NULL, *p;
	size_t len, entry_len = 0;
	uint num = 0, src = cmd[6];

	if (cmd[5] != HAB_ALG_SHA256 || cmd[7] != HAB_KEY_SRK)
		return HAB_UNS_ALGORITHM;

	/* The SRK table is a certificate data structure */
	table = hab_csf_data(st, get_unaligned_be32(cmd + 8), HAB_TAG_CRT,
			     &len);
	if (!table)
		return HAB_INV_KEY;

	for (p = table; p + 4 <= table + len; p += hab_hdr_len(p)) {
		if (hab_hdr_len(p) < 4 || p + hab_hdr_len(p) > table + len ||
		    num >= HAB_MAX_SRKS)
			return HAB_INV_KEY;

		sha256_csum_wd(p, hab_hdr_len(p), digests[num], CHUNKSZ_SHA256);
		if (num == src) {
			entry = p;
			entry_len = hab_hdr_len(p);
		}
		num++;
	}

	sha256_csum_wd((const u8 *)digests, num * SHA256_SUM_LEN, digest,
		       CHUNKSZ_SHA256);
	if (!hab_srk_hash_matches(digest) || !entry)
		return HAB_INV_KEY;

	st->keys[HAB_KEY_SRK] = hab_srk_to_key(entry, entry_len);
	if (!st->keys[HAB_KEY_SRK])
		return HAB_UNS_KEY;

	return 0;
}

static enum hab_reason hab_install_cert(struct hab_csf_state *st,
					const u8 *cmd)
{
	struct x509_certificate *cert;
	uint src = cmd[6], tgt = cmd[7];
	enum hab_reason rsn = 0;
	const u8 *der;
	size_t len;

	if (src >= HAB_NUM_KEYS || !st->keys[src] || tgt >= HAB_NUM_KEYS ||
	    tgt == HAB_KEY_SRK || st->keys[tgt])
		return HAB_INV_INDEX;

	/* The CSF key must be installed before authenticating the CSF */
	if (!!(cmd[3] & HAB_INS_KEY_CSF) != (tgt == HAB_KEY_CSFK))
		return HAB_INV_INDEX;

	der = hab_csf_data(st, get_unaligned_be32(cmd + 8), HAB_TAG_CRT, &len);
	if (!der)
		return HAB_INV_CERTIFICATE;

	cert = x509_cert_parse(der, len);
	if (IS_ERR(cert))
		return HAB_INV_CERTIFICATE;

	if (cert->unsupported_key || cert->unsupported_sig)
		rsn = HAB_UNS_KEY;
	else if (public_key_verify_signature(st->keys[src], cert->sig))
		rsn = HAB_INV_SIGNATURE;

	if (!rsn) {
		st->keys[tgt] = cert->pub;
		cert->pub = NULL;
	}
	x509_free_certificate(cert);

	return rsn;
}

static void hab_sha256_regions(const struct hab_region *regions, uint num,
			       u8 *digest)
{
	sha256_context ctx;
	uint i;

	sha256_starts(&ctx);
	for (i = 0; i < num; i++)
		sha256_update(&ctx, (const u8 *)regions[i].start,
			      regions[i].size);
	sha256_finish(&ctx, digest);
}

/* Verifies a CMS signature over the given regions */
static enum hab_reason hab_verify_cms(struct hab_csf_state *st,
				      const struct public_key *key,
				      const u8 *der, size_t len,
				      const struct hab_region *regions,
				      uint num)
{
	struct pkcs7_signed_info *sinfo;
	struct pkcs7_message *msg;
	u8 digest[SHA256_SUM_LEN];
	enum hab_reason rsn = 0;
	sha256_context ctx;
	u8 tag = 0x31;

	msg = pkcs7_parse_message(der, len);
	if (IS_ERR(msg))
		return HAB_INV_SIGNATURE;

	sinfo = msg->signed_infos;
	if (!sinfo || !sinfo->sig || !sinfo->authattrs || !sinfo->msgdigest ||
	    sinfo->msgdigest_len != SHA256_SUM_LEN ||
	    strcmp(sinfo->sig->hash_algo, "sha256")) {
		rsn = HAB_UNS_ALGORITHM;
		goto out;
	}

	/* The message digest attribute covers the signed data */
	hab_sha256_regions(regions, num, digest);
	if (memcmp(digest, sinfo->msgdigest, SHA256_SUM_LEN)) {
		rsn = HAB_INV_SIGNATURE;
		goto out;
	}

	/*
	 * The signature covers the attributes encoded as SET OF. The parser
	 * keeps them without their implicit tag.
	 */
	sha256_starts(&ctx);
	sha256_update(&ctx, &tag, 1);
	sha256_update(&ctx, sinfo->authattrs, sinfo->authattrs_len);
	sinfo->sig->digest = malloc(SHA256_SUM_LEN);
	if (!sinfo->sig->digest) {
		rsn = HAB_MEM_FAIL;
		goto out;
	}
	sha256_finish(&ctx, sinfo->sig->digest);
	sinfo->sig->digest_size = SHA256_SUM_LEN;

	if (public_key_verify_signature(key, sinfo->sig))
		rsn = HAB_INV_SIGNATURE;

out:
	pkcs7_free_message(msg);
	return rsn;
}

static enum hab_reason hab_authenticate_data(struct hab_csf_state *st,
					     const u8 *cmd, size_t len)
{
	struct hab_region regions[HAB_MAX_BLOCKS];
	uint key = cmd[4], num = 0, i;
	const u8 *der;
	size_t der_len;
	enum hab_reason rsn;

	if (cmd[5] != HAB_PCL_CMS)
		return HAB_UNS_PROTOCOL;

	if (key >= HAB_NUM_KEYS || !st->keys[key])
		return HAB_INV_INDEX;

	/* Without blocks, the command authenticates the CSF itself */
	if (len == 12) {
		if (key != HAB_KEY_CSFK || st->csf_authenticated)
			return HAB_INV_CALL;
		regions[0].start = (ulong)st->csf;
		regions[0].size = hab_hdr_len(st->csf);
		num = 1;
	} else {
		if (!st->csf_authenticated)
			return HAB_INV_CALL;
		if ((len - 12) % 8 || (len - 12) / 8 > HAB_MAX_BLOCKS ||
		    st->num_auth + (len - 12) / 8 > HAB_MAX_BLOCKS)
			return HAB_INV_SIZE;
		for (i = 12; i < len; i += 8, num++) {
			regions[num].start = get_unaligned_be32(cmd + i);
			regions[num].size = get_unaligned_be32(cmd + i + 4);
			if (regions[num].start < ROM_END ||
			    regions[num].start + regions[num].size <
			    regions[num].start)
				return HAB_INV_ADDRESS;
		}
	}

	der = hab_csf_data(st, get_unaligned_be32(cmd + 8), HAB_TAG_SIG,
			   &der_len);
	if (!der)
		return HAB_INV_SIGNATURE;

	rsn = hab_verify_cms(st, st->keys[key], der, der_len, regions, num);
	if (rsn)
		return rsn;

	if (len == 12) {
		st->csf_authenticated = true;
	} else {
		memcpy(&st->auth[st->num_auth], regions,
		       num * sizeof(regions[0]));
		st->num_auth += num;
	}

	return 0;
}

static enum hab_reason hab_run_csf_cmd(struct hab_csf_state *st, const u8 *cmd,
				       size_t len)
{
	switch (cmd[0]) {
	case HAB_CMD_INS_KEY:
		if (len != 12)
			return HAB_INV_COMMAND;
		if (cmd[4] == HAB_PCL_SRK)
			return st->keys[HAB_KEY_SRK] ? HAB_INV_CALL :
						       hab_install_srk(st, cmd);
		if (cmd[4] != HAB_PCL_X509)
			return HAB_UNS_PROTOCOL;
		/* Only the CSF key may be installed before authenticating it */
		if (!st->csf_authenticated && !(cmd[3] & HAB_INS_KEY_CSF))
			return HAB_INV_CALL;
		return hab_install_cert(st, cmd);
	case HAB_CMD_AUT_DAT:
		if (len < 12)
			return HAB_INV_COMMAND;
		return hab_authenticate_data(st, cmd, len);
	case HAB_CMD_UNLK:
	case HAB_CMD_NOP:
		/* Engine configuration isn't relevant for emulation */
		return st->csf_authenticated ? 0 : HAB_INV_CALL;
	default:
		return HAB_UNS_COMMAND;
	}
}

static bool hab_region_authenticated(struct hab_csf_state *st, ulong start,
				     size_t size)
{
	uint i;

	for (i = 0; i < st->num_auth; i++) {
		if (start >= st->auth[i].start &&
		    start + size <= st->auth[i].start + st->auth[i].size)
			return true;
	}

	return false;
}

/*
 * Executes the CSF referenced by the IVT of an image and checks that the IVT
 * and the entry point are covered by the authenticated data
 */
static bool hab_verify_image(const struct ivt *ivt, ulong start, size_t bytes)
{
	struct hab_csf_state st = { 0 };
	enum hab_reason rsn = 0;
	const u8 *cmd;
	size_t len;
	uint i;

	st.csf = (const u8 *)(ulong)ivt->csf;
	if (!ivt->csf || ivt->csf < start || ivt->csf >= start + bytes ||
	    st.csf[0] != HAB_TAG_CSF ||
	    (st.csf[3] & HAB_MAJ_MASK) != HAB_MAJ_VER) {
		hab_log_event(HAB_FAILURE, HAB_INV_CSF, HAB_CTX_AUTHENTICATE,
			      &ivt->csf, sizeof(ivt->csf));
		return false;
	}
	st.csf_len = start + bytes - ivt->csf;

	for (cmd = st.csf + 4; cmd < st.csf + hab_hdr_len(st.csf);
	     cmd += len) {
		len = hab_hdr_len(cmd);
		if (len < 4 || cmd + len > st.csf + hab_hdr_len(st.csf)) {
			rsn = HAB_INV_COMMAND;
			break;
		}

		rsn = hab_run_csf_cmd(&st, cmd, len);
		if (rsn)
			break;
	}

	if (rsn) {
		hab_log_event(HAB_FAILURE, rsn, HAB_CTX_COMMAND, cmd, len);
	} else if (!st.csf_authenticated ||
		   !hab_region_authenticated(&st, (ulong)ivt, IVT_SIZE) ||
		   !hab_region_authenticated(&st, ivt->entry, 4)) {
		rsn = HAB_INV_IVT;
		hab_log_event(HAB_FAILURE, rsn, HAB_CTX_AUTHENTICATE, ivt,
			      IVT_SIZE);
	}

	for (i = 0; i < HAB_NUM_KEYS; i++)
		public_key_free(st.keys[i]);

	return !rsn;
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

	if (hab_verify_image(ivt, (ulong)*start, *bytes))
		return *start;

	/* Open devices record failures without enforcing authentication */
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
	if (state) {
		if (hab_config() == HAB_CFG_OPEN)
			*state = HAB_STATE_NONSECURE;
		else
			*state = ret == HAB_FAILURE ? HAB_STATE_FAIL_SOFT :
						      HAB_STATE_TRUSTED;
	}

	return ret;
}

static void hab_failsafe(void)
{
	hang();
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

/*
 * Authenticates the loader before starting it. Unsigned loaders are only
 * started on open devices, without recording an event.
 */
void spl_board_prepare_for_boot(void)
{
	void *start = (void *)rom_loader.start;
	size_t bytes = rom_loader.size;
	const struct ivt *ivt = start;

	if (hab_config() == HAB_CFG_OPEN && !ivt->csf)
		return;

	if (!hab_authenticate_image(HAB_CID_ROM, 0, &start, &bytes, NULL)) {
		log_err("ROM: authentication of the loader failed\n");
		hang();
	}
}
