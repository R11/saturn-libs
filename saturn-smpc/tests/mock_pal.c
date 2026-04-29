/*
 * tests/mock_pal.c — implementation of the shared mock SMPC PAL.
 */

#include "mock_pal.h"

#include <string.h>

saturn_smpc_pad_t mock_pads[SATURN_SMPC_MAX_PADS];
uint8_t           mock_n_pads;
saturn_smpc_rtc_t mock_rtc;
saturn_result_t   mock_rtc_result;

int mock_init_calls;
int mock_shutdown_calls;
int mock_read_pads_calls;
int mock_read_rtc_calls;

static saturn_result_t pal_init(void* ctx)
{
    (void)ctx;
    mock_init_calls++;
    return SATURN_OK;
}

static void pal_shutdown(void* ctx)
{
    (void)ctx;
    mock_shutdown_calls++;
}

static saturn_result_t pal_read_pads(void* ctx,
                                     saturn_smpc_pad_t out[SATURN_SMPC_MAX_PADS],
                                     uint8_t* n_out)
{
    (void)ctx;
    mock_read_pads_calls++;
    memcpy(out, mock_pads, sizeof(mock_pads));
    *n_out = mock_n_pads;
    return SATURN_OK;
}

static saturn_result_t pal_read_rtc(void* ctx, saturn_smpc_rtc_t* out)
{
    (void)ctx;
    mock_read_rtc_calls++;
    *out = mock_rtc;
    return mock_rtc_result;
}

static const saturn_smpc_pal_t mock_pal_inst = {
    pal_init,
    pal_shutdown,
    pal_read_pads,
    pal_read_rtc,
    NULL
};

void mock_pal_reset(void)
{
    memset(mock_pads, 0, sizeof(mock_pads));
    mock_n_pads          = 0;
    memset(&mock_rtc, 0, sizeof(mock_rtc));
    mock_rtc_result      = SATURN_OK;
    mock_init_calls      = 0;
    mock_shutdown_calls  = 0;
    mock_read_pads_calls = 0;
    mock_read_rtc_calls  = 0;
}

void mock_pal_install_and_init(void)
{
    mock_pal_reset();
    saturn_smpc_install_pal(&mock_pal_inst);
    saturn_smpc_init();
}

void mock_pal_uninstall(void)
{
    saturn_smpc_shutdown();
    saturn_smpc_install_pal(NULL);
}
