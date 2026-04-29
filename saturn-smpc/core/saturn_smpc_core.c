/*
 * libs/saturn-smpc/core — portable peripheral logic.
 *
 * Owns the in-memory pad table, edge bookkeeping, button decoders, and
 * RTC validation. Talks to hardware exclusively through the installed
 * PAL.
 *
 * No platform headers, no malloc, no globals beyond a single fixed-size
 * state block. Host-testable; the same .c also compiles under sh-elf-gcc
 * and emcc unchanged.
 */

#include <saturn_smpc.h>
#include <saturn_smpc/pal.h>
#include <saturn_base/result.h>

#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

static const saturn_smpc_pal_t* g_pal;
static int                      g_initialised;

static saturn_smpc_pad_t        g_pads[SATURN_SMPC_MAX_PADS];
static uint8_t                  g_n_pads;          /* clamped to MAX */

/* Edge-event tracking. Updated each poll; the public query functions
 * return these flags directly. */
static uint8_t                  g_prev_connected[SATURN_SMPC_MAX_PADS];
static uint8_t                  g_just_connected[SATURN_SMPC_MAX_PADS];
static uint8_t                  g_just_disconnected[SATURN_SMPC_MAX_PADS];

/* ---------------------------------------------------------------------------
 * PAL install
 *
 * Installing or replacing the PAL clears the initialised flag — the
 * caller must call init() again before polling.
 * ------------------------------------------------------------------------- */

void saturn_smpc_install_pal(const saturn_smpc_pal_t* pal)
{
    g_pal         = pal;
    g_initialised = 0;
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

saturn_result_t saturn_smpc_init(void)
{
    saturn_result_t r;

    if (!g_pal || !g_pal->init) return SATURN_ERR_NOT_READY;

    r = g_pal->init(g_pal->ctx);
    if (r != SATURN_OK) return r;

    /* Reset all internal state. */
    memset(g_pads,               0, sizeof(g_pads));
    g_n_pads = 0;
    memset(g_prev_connected,     0, sizeof(g_prev_connected));
    memset(g_just_connected,     0, sizeof(g_just_connected));
    memset(g_just_disconnected,  0, sizeof(g_just_disconnected));

    g_initialised = 1;
    return SATURN_OK;
}

void saturn_smpc_shutdown(void)
{
    if (!g_initialised) return;
    if (g_pal && g_pal->shutdown) g_pal->shutdown(g_pal->ctx);
    g_initialised = 0;
}

/* ---------------------------------------------------------------------------
 * Poll
 *
 * Snapshot prev_buttons and prev_connected, ask the PAL for fresh data,
 * then recompute edge-event flags. The PAL is expected to write valid
 * pad records in slots [0..n-1] of `fresh`; slots beyond n are zeroed
 * before the call so they appear as not-connected.
 * ------------------------------------------------------------------------- */

saturn_result_t saturn_smpc_poll(void)
{
    saturn_smpc_pad_t fresh[SATURN_SMPC_MAX_PADS];
    uint16_t          prev_buttons[SATURN_SMPC_MAX_PADS];
    saturn_result_t   r;
    uint8_t           n = 0;
    uint8_t           i;

    if (!g_initialised || !g_pal || !g_pal->read_pads) {
        return SATURN_ERR_NOT_READY;
    }

    /* Snapshot prior state. */
    for (i = 0; i < SATURN_SMPC_MAX_PADS; ++i) {
        prev_buttons[i]      = g_pads[i].buttons;
        g_prev_connected[i]  = g_pads[i].connected;
    }

    /* Hand a zeroed buffer to the PAL so untouched slots read as
     * !connected if it writes fewer than MAX entries. */
    memset(fresh, 0, sizeof(fresh));

    r = g_pal->read_pads(g_pal->ctx, fresh, &n);
    if (r != SATURN_OK) return r;

    if (n > SATURN_SMPC_MAX_PADS) n = SATURN_SMPC_MAX_PADS;
    g_n_pads = n;

    /* Commit fresh data and compute edge events. */
    for (i = 0; i < SATURN_SMPC_MAX_PADS; ++i) {
        g_pads[i]              = fresh[i];
        g_pads[i].prev_buttons = prev_buttons[i];

        g_just_connected[i] =
            (g_pads[i].connected && !g_prev_connected[i]) ? 1u : 0u;
        g_just_disconnected[i] =
            (!g_pads[i].connected && g_prev_connected[i]) ? 1u : 0u;
    }

    return SATURN_OK;
}

/* ---------------------------------------------------------------------------
 * Pad queries
 * ------------------------------------------------------------------------- */

uint8_t saturn_smpc_pad_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < SATURN_SMPC_MAX_PADS; ++i) {
        if (g_pads[i].connected) n++;
    }
    return n;
}

const saturn_smpc_pad_t* saturn_smpc_pad_get(uint8_t flat_index)
{
    if (flat_index >= SATURN_SMPC_MAX_PADS) return NULL;
    return &g_pads[flat_index];
}

const saturn_smpc_pad_t* saturn_smpc_pad_by_addr(uint8_t port, uint8_t tap)
{
    uint8_t i;
    for (i = 0; i < SATURN_SMPC_MAX_PADS; ++i) {
        if (g_pads[i].connected && g_pads[i].port == port
                                 && g_pads[i].tap  == tap) {
            return &g_pads[i];
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Button helpers
 * ------------------------------------------------------------------------- */

int saturn_smpc_button_held(const saturn_smpc_pad_t* p, uint16_t mask)
{
    if (!p) return 0;
    return (p->buttons & mask) ? 1 : 0;
}

int saturn_smpc_button_pressed(const saturn_smpc_pad_t* p, uint16_t mask)
{
    uint16_t edges;
    if (!p) return 0;
    edges = (uint16_t)(p->buttons & ~p->prev_buttons);
    return (edges & mask) ? 1 : 0;
}

int saturn_smpc_button_released(const saturn_smpc_pad_t* p, uint16_t mask)
{
    uint16_t edges;
    if (!p) return 0;
    edges = (uint16_t)(p->prev_buttons & ~p->buttons);
    return (edges & mask) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Connect / disconnect events
 * ------------------------------------------------------------------------- */

int saturn_smpc_pad_just_connected(uint8_t flat_index)
{
    if (flat_index >= SATURN_SMPC_MAX_PADS) return 0;
    return g_just_connected[flat_index] ? 1 : 0;
}

int saturn_smpc_pad_just_disconnected(uint8_t flat_index)
{
    if (flat_index >= SATURN_SMPC_MAX_PADS) return 0;
    return g_just_disconnected[flat_index] ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * RTC
 *
 * Validate the readout against sane field ranges. We do NOT enforce
 * per-month day caps (28..31) here — that would require a calendar
 * dependency and we have no need for it right now; a 31-day cap is
 * enough to catch malformed bytes from a confused PAL.
 * ------------------------------------------------------------------------- */

static int rtc_fields_valid(const saturn_smpc_rtc_t* r)
{
    if (r->month   < 1 || r->month   > 12) return 0;
    if (r->day     < 1 || r->day     > 31) return 0;
    if (r->weekday > 6)                    return 0;
    if (r->hour    > 23)                   return 0;
    if (r->minute  > 59)                   return 0;
    if (r->second  > 59)                   return 0;
    return 1;
}

saturn_result_t saturn_smpc_rtc_read(saturn_smpc_rtc_t* out)
{
    saturn_smpc_rtc_t r;
    saturn_result_t   res;

    if (!out)                                  return SATURN_ERR_INVALID;
    if (!g_initialised || !g_pal
                       || !g_pal->read_rtc)    return SATURN_ERR_NOT_READY;

    res = g_pal->read_rtc(g_pal->ctx, &r);
    if (res != SATURN_OK)                      return res;
    if (!rtc_fields_valid(&r))                 return SATURN_ERR_HARDWARE;

    *out = r;
    return SATURN_OK;
}
