# Saturn libs

L1 platform-specific libraries for Sega Saturn homebrew. Each lib wraps
one hardware block (or a single coordinated concern) and is consumed by
games and apps in sibling repos.

| Lib            | Concern                                              |
|----------------|------------------------------------------------------|
| `saturn-base`  | Shared error codes (`saturn_result_t`), font_8x8     |
| `saturn-smpc`  | Peripherals (digital pad, RTC) via SMPC              |
| `saturn-vdp1`  | Sprite/polygon/framebuffer (filled quads in v1)      |
| `saturn-vdp2`  | Backgrounds, NBG0 cell-mode text, palette            |
| `saturn-io`    | NetLink modem: UART, AT layer, framing, transport    |
| `saturn-app`   | L2 game-framework: frame loop, scene, registry       |
| `test`         | Host-only TDD framework with TAP 14 output           |

`saturn-app` is L2; everything else is L1. The L1 libs do not import
each other through illegal layer boundaries.

## Layout per lib

Each lib is internally split:

```
saturn-{name}/
  include/         portable public headers
  core/            portable C (no platform headers); host-testable
  saturn/          SH-2/SGL implementation; only built with the cross
  tests/           host unit tests via libs/test/
  Makefile         per-lib build for both host and saturn targets
```

Some libs add `web/`, `server/`, or `ffi/` shells. `saturn-io` already
has `python/` for the bridge tooling.

## Building

Host (any lib):

```sh
make -C saturn-vdp1 test
```

Saturn cross-compile happens through individual game projects (the libs
are consumed as object archives or include paths; there is no top-level
"build everything for Saturn" target here).

## Layering

These libs sit at L1 of the workspace's L1/L2/L3 model. They are
platform-specific: a Wii U or Dreamcast game cannot consume them. Apps
that need to be cross-platform consume L2 libraries (e.g. `cui`) which
internally consume L1 libs through PAL interfaces. See the workspace
`CLAUDE.md` (one level up) for the full layering rules.

## History

`saturn-io` was originally extracted from `saturn-tools` and lived as
its own published repo at `R11/saturn-io`. It was merged into this
monorepo with full commit history preserved (via `git subtree`) so that
all coupled L1 Saturn libs ship and evolve together.
