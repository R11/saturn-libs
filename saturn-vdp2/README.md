# saturn-vdp2

Minimal VDP2 surface: clear colour, NBG0 cell-mode ASCII text (40x28),
and an optional NBG1 RGB555 bitmap backdrop (320x224 visible). Same
PAL pattern as saturn-vdp1: portable core + per-shell flush.

The NBG0 path renders ASCII through a 4bpp font shared with
`saturn-base/font_8x8`. NBG1 bitmap uploads are independent of the text
path and intended for splash screens / per-game backdrops.

## Public API

| Header                          | What it provides                              |
|---------------------------------|-----------------------------------------------|
| `saturn_vdp2.h`                 | init/clear/print/begin/end + read-side        |
| `saturn_vdp2/bg.h`              | NBG1 bitmap upload + enable                   |
| `saturn_vdp2/pal.h`             | `saturn_vdp2_pal_t` install hook              |
| `saturn_vdp2/saturn.h`          | Saturn-target PAL registration                |

Surface:

- `saturn_vdp2_init` / `_shutdown`
- `saturn_vdp2_begin_frame` -> `_clear(rgb555)` + N x `_print(col, row, palette, str)` -> `_end_frame`
- `saturn_vdp2_text_count`, `_texts`, `_clear_color` for shell flush
- `saturn_vdp2_bg_init`, `_bg_set_image`, `_bg_clear`, `_bg_enable`, `_bg_is_enabled`

Caps: 64 strings per frame, 40-char max length, 40x28 grid.

## Integration

```c
#include <saturn_vdp2.h>
#include <saturn_vdp2/bg.h>
#include <saturn_vdp2/saturn.h>

int main(void) {
    saturn_vdp2_register_saturn_pal();
    saturn_vdp2_init();
    saturn_vdp2_bg_init();

    while (1) {
        slSynch();

        saturn_vdp2_begin_frame();
        saturn_vdp2_clear(0x0000);
        saturn_vdp2_print(2, 2, 1, "hello, saturn");
        saturn_vdp2_end_frame();
    }
}
```

## Build / test

```sh
make test
make test-tap
make clean
```

## Layer

L1 Saturn-specific. Depends on `saturn-base` (font + sgl_defs). The
text path and the NBG1 bitmap path share VDP2 VRAM but have no
header-level coupling between them.
