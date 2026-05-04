# saturn-vdp1

Minimal VDP1 surface: filled coloured rectangles via a per-frame command
list. The platform shell installs a PAL whose `flush` callback walks
the list at end-of-frame and draws to the actual surface (VDP1 hardware
via SGL on Saturn, canvas via emscripten on web). Apps never touch SGL
directly.

Sprites, polygons, gouraud, distorted, transparency are future work;
this v1 ships what the lobby project's snake-grade games need.

## Public API

| Header                          | What it provides                              |
|---------------------------------|-----------------------------------------------|
| `saturn_vdp1.h`                 | colour helper, quad submission, frame loop    |
| `saturn_vdp1/pal.h`             | `saturn_vdp1_pal_t` install hook              |
| `saturn_vdp1/saturn.h`          | Saturn-target PAL registration                |

Surface:

- `saturn_vdp1_rgb` — pack r/g/b 0..255 into RGB555 + opaque bit
- `saturn_vdp1_init(w, h)` / `_shutdown`
- frame contract: `saturn_vdp1_begin_frame` -> N x `_submit_quad` -> `_end_frame`
- `saturn_vdp1_patch_now` — main-loop hook; on Saturn re-patches slot 3
  and priority registers (no-op elsewhere)
- read-side: `saturn_vdp1_quad_count`, `_quads`, `_screen_width`, `_screen_height`
- `saturn_vdp1_install_pal` (host / web) / `saturn_vdp1_register_saturn_pal`

`SATURN_VDP1_MAX_QUADS` (512) caps a single frame's submissions; further
submits return `SATURN_ERR_NO_SPACE`.

## Integration

```c
#include <saturn_vdp1.h>
#include <saturn_vdp1/saturn.h>

int main(void) {
    saturn_vdp1_register_saturn_pal();
    saturn_vdp1_init(320, 224);

    while (1) {
        slSynch();
        saturn_vdp1_patch_now();

        saturn_vdp1_begin_frame();
        saturn_vdp1_submit_quad(40, 40, 80, 80,
                                saturn_vdp1_rgb(255, 64, 32));
        saturn_vdp1_end_frame();   /* hands the list to the PAL flush */
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

L1 Saturn-specific. Depends on `saturn-base`. Saturn shell consumes
SGL directly; the portable core does not.
