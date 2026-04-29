/*
 * test_init_placeholder.c -- host-side unit test
 *
 * Compiled against src/net.c without __SATURN__ defined, this exercises
 * the Phase 1 placeholder-dial rejection and related init paths. Runs
 * on the host machine; no Saturn toolchain required.
 *
 * The register-access macros in uart.h dereference SH-2 physical
 * addresses. That's fine here because none of the init paths we test
 * actually touch the UART -- init only validates config.
 */

#include <stdio.h>
#include <string.h>

#include "saturn_io/net.h"

static void on_frame(const uint8_t* p, uint16_t len, void* user) {
    (void)p; (void)len; (void)user;
}

static int expect(const char* label, saturn_io_status_t got,
                  saturn_io_status_t want) {
    int ok = (got == want);
    printf("%-38s : %s (got=%d, want=%d)\n", label,
           ok ? "PASS" : "FAIL", (int)got, (int)want);
    return ok ? 0 : 1;
}

int main(void) {
    int failures = 0;
    saturn_io_config_t cfg;

    /* NULL callbacks rejected. */
    cfg = (saturn_io_config_t)SATURN_IO_DEFAULTS;
    failures += expect("init rejects NULL on_frame",
                       saturn_io_init(&cfg),
                       SATURN_IO_ERR_INVALID_CONFIG);

    /* NULL dial_number rejected. */
    cfg = (saturn_io_config_t)SATURN_IO_DEFAULTS;
    cfg.on_frame = on_frame;
    failures += expect("init rejects NULL dial_number",
                       saturn_io_init(&cfg),
                       SATURN_IO_ERR_INVALID_CONFIG);

    /* Empty dial_number rejected. */
    cfg = (saturn_io_config_t)SATURN_IO_DEFAULTS;
    cfg.on_frame    = on_frame;
    cfg.dial_number = "";
    failures += expect("init rejects empty dial_number",
                       saturn_io_init(&cfg),
                       SATURN_IO_ERR_INVALID_CONFIG);

    /* Placeholder "0000000" rejected. */
    cfg = (saturn_io_config_t)SATURN_IO_DEFAULTS;
    cfg.on_frame    = on_frame;
    cfg.dial_number = "0000000";
    failures += expect("init rejects 0000000 placeholder",
                       saturn_io_init(&cfg),
                       SATURN_IO_ERR_INVALID_CONFIG);

    /* Real number accepted. */
    cfg = (saturn_io_config_t)SATURN_IO_DEFAULTS;
    cfg.on_frame    = on_frame;
    cfg.dial_number = "5551234";
    failures += expect("init accepts real dial_number",
                       saturn_io_init(&cfg),
                       SATURN_IO_OK);

    /* Double-init rejected. */
    failures += expect("init rejects second call",
                       saturn_io_init(&cfg),
                       SATURN_IO_ERR_ALREADY_INIT);

    /* saturn_io_shutdown() powers off the modem via SMPC, which
     * dereferences SH-2 physical addresses. That's not safe on a host,
     * so this test intentionally stops here before calling shutdown. */

    if (failures == 0) {
        printf("\nAll checks passed.\n");
        return 0;
    }
    printf("\n%d check(s) failed.\n", failures);
    return 1;
}
