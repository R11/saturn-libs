# saturn-smpc

Saturn peripherals + RTC, presented as a flat 12-slot pad table with a
portable active-high button bitmask. Hides the SMPC register protocol,
multitap routing, and active-low quirk.

Two ports x up to 6 multitap slots = 12 peripherals. v1 reads digital
pads + analog pad axes/triggers + the SMPC real-time clock; mouse,
keyboard, and gun are reserved in the kind enum but not yet polled.

## Public API

| Header                          | What it provides                              |
|---------------------------------|-----------------------------------------------|
| `saturn_smpc.h`                 | lifecycle, pad table, button helpers, RTC     |
| `saturn_smpc/pal.h`             | `saturn_smpc_pal_t` install hook              |
| `saturn_smpc/saturn.h`          | Saturn-target PAL registration                |

Surface in one breath:

- `saturn_smpc_init` / `_shutdown` / `_poll`
- `saturn_smpc_pad_count`, `_pad_get`, `_pad_by_addr`
- `saturn_smpc_button_held` / `_pressed` / `_released` (edge-triggered helpers)
- `saturn_smpc_pad_just_connected` / `_just_disconnected`
- `saturn_smpc_rtc_read`
- `saturn_smpc_install_pal` (host shells, tests)
- `saturn_smpc_register_saturn_pal` (Saturn target)

## Integration

```c
#include <saturn_smpc.h>
#include <saturn_smpc/saturn.h>   /* Saturn build only */

int main(void) {
    saturn_smpc_register_saturn_pal();   /* one-shot */
    if (saturn_smpc_init() != SATURN_OK) return 1;

    while (1) {
        slSynch();
        saturn_smpc_poll();

        const saturn_smpc_pad_t* p = saturn_smpc_pad_by_addr(0, 0);
        if (p && saturn_smpc_button_pressed(p, SATURN_SMPC_BUTTON_START)) {
            /* P1 just pressed START this frame. */
        }
    }
}
```

Host tests install a mock PAL via `saturn_smpc_install_pal` and feed
controlled pad state through it.

## Build / test

```sh
make test       # host tests
make test-tap   # TAP 14 output
make clean
```

## Layer

L1 Saturn-specific. Depends on `saturn-base`. The Saturn shell reads
the global `Smpc_Peripheral[]` (declared in `<sgl_defs.h>`); the host
test PAL touches no platform headers.
