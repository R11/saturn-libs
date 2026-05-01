/*
 * libs/saturn-bup/saturn — Saturn BUP-BIOS PAL.
 *
 * Calls into the BUP BIOS via the SH-2 vector table at 0x06000350.
 * The macro layout below is lifted verbatim from the working impl in
 * coup's pal/saturn/saturn_storage.h (with the SATURN_BUP_* names
 * adapted to the local types).
 *
 * Verification: this file cannot be unit-tested from the host. Correctness
 * is guaranteed by:
 *   1. Vector-table offsets matching the SBL/SEGA_BUP.H specification
 *      (BUP_LIB_ADDRESS = 0x06000350 + 8, BUP_VECTOR_ADDRESS = +4).
 *   2. The same BIOS sequence (Init -> Stat -> conditional Format)
 *      that ships in coup/saturn_storage.c and has been verified on
 *      real hardware and in mednafen.
 *
 * Compiles only under sh-elf-gcc with SGL headers on the include path
 * (slResetDisable / slResetEnable / Smpc_Status). The host build of
 * this lib excludes this file via Makefile.
 */

#include <saturn_bup.h>
#include <saturn_bup/pal.h>
#include <saturn_bup/saturn.h>

/* SGL public surface — only visible inside saturn/ shells. */
#include "sgl_defs.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * BUP BIOS types (mirroring SEGA_BUP.H)
 * ------------------------------------------------------------------------- */

typedef struct {
    Uint16 unit_id;
    Uint16 partition;
} bup_config_t;

typedef struct {
    Uint32 totalsize;
    Uint32 totalblock;
    Uint32 blocksize;
    Uint32 freesize;
    Uint32 freeblock;
    Uint32 datanum;
} bup_stat_t;

typedef struct {
    Uint8 year;     /* offset from 1980 */
    Uint8 month;
    Uint8 day;
    Uint8 time;     /* hour */
    Uint8 min;
    Uint8 week;     /* day of week */
} bup_date_t;

typedef struct {
    char   filename[12];   /* 11 chars + null */
    char   comment[11];    /* 10 chars + null */
    Uint8  language;
    Uint32 date;
    Uint32 datasize;
    Uint16 blocksize;
} bup_dir_t;

/* ---------------------------------------------------------------------------
 * BIOS function pointers (lifted from coup/saturn_storage.h:88-148)
 * ------------------------------------------------------------------------- */

#define BUP_LIB_ADDRESS    (*(volatile Uint32 *)(0x6000350 + 8))
#define BUP_VECTOR_ADDRESS (*(volatile Uint32 *)(0x6000350 + 4))

#define BUP_Init(lib, work, configs) \
    ((void (*)(Uint32*, Uint32*, bup_config_t*))(BUP_LIB_ADDRESS))(lib, work, configs)

#define BUP_SelPart(dev, part) \
    ((Sint32 (*)(Uint32, Uint16))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 4)))(dev, part)

#define BUP_Format(dev) \
    ((Sint32 (*)(Uint32))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 8)))(dev)

#define BUP_Stat(dev, sz, stat) \
    ((Sint32 (*)(Uint32, Uint32, bup_stat_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 12)))(dev, sz, stat)

#define BUP_Write(dev, dir, data, mode) \
    ((Sint32 (*)(Uint32, bup_dir_t*, void*, Uint8))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 16)))(dev, dir, data, mode)

#define BUP_Read(dev, name, buf) \
    ((Sint32 (*)(Uint32, const char*, void*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 20)))(dev, name, buf)

#define BUP_Delete(dev, name) \
    ((Sint32 (*)(Uint32, const char*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 24)))(dev, name)

#define BUP_Dir(dev, name, max, dir) \
    ((Sint32 (*)(Uint32, const char*, Uint16, bup_dir_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 28)))(dev, name, max, dir)

#define BUP_Verify(dev, name, data) \
    ((Sint32 (*)(Uint32, const char*, void*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 32)))(dev, name, data)

#define BUP_GetDate(date, out) \
    ((void (*)(Uint32, bup_date_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 36)))(date, out)

#define BUP_SetDate(in) \
    ((Uint32 (*)(bup_date_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 40)))(in)

/* ---------------------------------------------------------------------------
 * Static workspace + state.
 *
 * BUP_Init wants a 16 KB code buffer (`lib`) it keeps for the program's
 * lifetime, and an 8 KB scratch (`work`) only used during Init itself.
 * ------------------------------------------------------------------------- */

static Uint32      s_bup_lib[4096] __attribute__((aligned(4)));   /* 16 KB */
static bup_config_t s_configs[3];
static int         s_initialised;

/* Map a BIOS Sint32 return into our enum. The first 9 codes line up
 * with SBUP_OK..SBUP_BROKEN by design. */
static sbup_error_t map_bios(Sint32 rc) {
    if (rc >= 0 && rc <= 8) return (sbup_error_t)rc;
    return SBUP_BROKEN;
}

/* ---------------------------------------------------------------------------
 * PAL ops
 * ------------------------------------------------------------------------- */

static sbup_error_t saturn_init(void* ctx, uint32_t device) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    if (s_initialised) return SBUP_OK;

    /* 8 KB scratch, only used during BUP_Init. */
    Uint32 work[2048];

    slResetDisable();
    BUP_Init(s_bup_lib, work, s_configs);
    slResetEnable();

    if (s_configs[0].unit_id == 0) return SBUP_NOT_CONNECTED;

    /* Format if unformatted. */
    bup_stat_t st;
    Sint32 rc = BUP_Stat(0, 0, &st);
    if (rc == 2 /* unformatted */) {
        slResetDisable();
        rc = BUP_Format(0);
        slResetEnable();
        if (rc != 0) return map_bios(rc);
    } else if (rc != 0) {
        return map_bios(rc);
    }

    s_initialised = 1;
    return SBUP_OK;
}

static void saturn_shutdown(void* ctx) {
    (void)ctx;
    s_initialised = 0;
    /* The BIOS workspace cannot be "uninitialised"; we just drop our
     * flag so a subsequent saturn_init() re-runs the boot sequence. */
}

static sbup_error_t saturn_read(void* ctx, uint32_t device,
                                const char* padded_name,
                                void* out, size_t cap, size_t* out_len) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    bup_dir_t dir;
    Sint32 count = BUP_Dir(device, padded_name, 1, &dir);
    if (count <= 0) return SBUP_NOT_FOUND;

    if (cap < dir.datasize) return SBUP_BROKEN;

    Sint32 rc = BUP_Read(device, padded_name, out);
    if (rc != 0) return map_bios(rc);

    if (out_len) *out_len = (size_t)dir.datasize;
    return SBUP_OK;
}

static sbup_error_t saturn_write(void* ctx, uint32_t device,
                                 const char* padded_name,
                                 const void* data, size_t len) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    bup_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    memcpy(dir.filename, padded_name, 12);
    memcpy(dir.comment, "saturn-bup", 11);
    dir.language = 0;
    dir.datasize = (Uint32)len;

    /* RTC timestamp from SMPC. The slGetStatus() / Smpc_Status pattern
     * is identical to coup's, which has shipped on real hardware. */
    slGetStatus();
    bup_date_t bdate;
    bdate.year   = (Uint8)(slDec2Hex(Smpc_Status->rtc.year) - 1980);
    bdate.month  = (Uint8)(slDec2Hex(Smpc_Status->rtc.month & 0x0F));
    bdate.day    = (Uint8)(slDec2Hex(Smpc_Status->rtc.date));
    bdate.time   = (Uint8)(slDec2Hex(Smpc_Status->rtc.hour));
    bdate.min    = (Uint8)(slDec2Hex(Smpc_Status->rtc.minute));
    bdate.week   = (Uint8)((Smpc_Status->rtc.month >> 4) & 0x0F);
    dir.date = BUP_SetDate(&bdate);

    /* Overwrite mode: drop any existing record first. Mode byte 0 is
     * "fail if exists" on some BIOS revisions, so deleting up front is
     * the safer pattern (and matches coup). */
    BUP_Delete(device, padded_name);

    slResetDisable();
    Sint32 rc = BUP_Write(device, &dir, (void*)data, 0);
    slResetEnable();
    if (rc != 0) return map_bios(rc);

    rc = BUP_Verify(device, padded_name, (void*)data);
    if (rc != 0) return map_bios(rc);

    return SBUP_OK;
}

static sbup_error_t saturn_erase(void* ctx, uint32_t device,
                                 const char* padded_name) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    slResetDisable();
    Sint32 rc = BUP_Delete(device, padded_name);
    slResetEnable();

    /* Erasing a missing record is success from the lib's POV. */
    if (rc == 5 /* SATURN_BUP_NOT_FOUND */) return SBUP_OK;
    if (rc != 0) return map_bios(rc);
    return SBUP_OK;
}

static sbup_error_t saturn_stat(void* ctx, uint32_t device,
                                sbup_device_info_t* out) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;
    if (!out) return SBUP_INVALID;

    bup_stat_t st;
    Sint32 rc = BUP_Stat(device, 0, &st);
    if (rc != 0) return map_bios(rc);

    out->total_size  = st.totalsize;
    out->free_size   = st.freesize;
    out->free_blocks = st.freeblock;
    out->data_count  = st.datanum;
    return SBUP_OK;
}

/* ---------------------------------------------------------------------------
 * Public registration
 * ------------------------------------------------------------------------- */

static const sbup_pal_t s_saturn_pal = {
    .init     = saturn_init,
    .shutdown = saturn_shutdown,
    .read     = saturn_read,
    .write    = saturn_write,
    .erase    = saturn_erase,
    .stat     = saturn_stat,
    .ctx      = NULL,
};

void sbup_register_saturn_pal(void) {
    sbup_install_pal(&s_saturn_pal);
}
