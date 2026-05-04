# saturn-base

Shared header-only foundation used by every other lib in this repo:

- **`saturn_result_t`** — common return type. Zero is success, negatives
  are documented errors. Each lib that returns a status returns one of
  these so consumer code can branch on a single error space.
- **`saturn_font_8x8`** — a 95-glyph (ASCII 32..126) 1bpp font plus a
  4bpp converter the saturn-vdp2 NBG0 path consumes at boot. Same byte
  array is shipped to web frontends as a JS const.
- **`sgl_defs.h`** — minimal SGL declarations (types, NBG/SPR macros,
  `slInitSystem`, `slSynch`, peripheral structs). Lets Saturn shells
  link against SGL without dragging in the full headers, which trip
  modern GCC.

## Public API

| Header                                | What it provides                      |
|---------------------------------------|---------------------------------------|
| `saturn_base/result.h`                | `saturn_result_t`, `SATURN_OK`, `SATURN_ERR_*` |
| `saturn_base/font_8x8.h`              | font data + 1bpp -> 4bpp helpers      |
| `sgl_defs.h`                          | minimal SGL declarations and constants |

There is no implementation file — the lib is consumed by include path
only.

## Integration

```make
LIB_BASE = /path/to/saturn-libs/saturn-base
CCFLAGS += -I$(LIB_BASE)/include
```

```c
#include <saturn_base/result.h>
#include <saturn_base/font_8x8.h>

saturn_result_t do_thing(void) {
    const uint8_t* glyph = saturn_font_glyph('A');
    if (!glyph) return SATURN_ERR_NOT_FOUND;
    return SATURN_OK;
}
```

Saturn shells additionally include `<sgl_defs.h>` to get `slInitSystem`,
`Smpc_Peripheral`, and the NBG/PNB/CRAM constants.

## Build / test

```sh
make            # header-only; no-op
make test       # no-op
make clean      # no-op
```

## Layer

L1, header-only. Imports nothing else. Every other lib in this repo
depends on it.
