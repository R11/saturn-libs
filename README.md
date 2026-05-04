# saturn-libs

L1 platform-specific libraries for Sega Saturn homebrew (with one L2
framework on top), plus a host-side TDD framework. Each lib wraps one
hardware block (or a single coordinated concern) and is consumed by
games and apps in sibling repos.

| Lib            | Concern                                              | Layer |
|----------------|------------------------------------------------------|-------|
| `saturn-base`  | Shared error codes (`saturn_result_t`), font_8x8, sgl_defs | L1 |
| `saturn-smpc`  | Peripherals (digital pad, RTC) via SMPC              | L1    |
| `saturn-vdp1`  | Sprite/polygon/framebuffer (filled quads in v1)      | L1    |
| `saturn-vdp2`  | Backgrounds, NBG0 cell-mode text, NBG1 bitmap        | L1    |
| `saturn-io`    | NetLink modem: UART, AT layer, framing, transport    | L1    |
| `saturn-bup`   | Backup RAM records (Saturn BIOS + host file backends) | L1   |
| `saturn-app`   | Lobby framework: registry, scene, identity, online   | L2    |
| `test`         | Host-only TDD framework with TAP 14 output           | -     |

`saturn-app` is L2; everything else is L1. The L1 libs do not import
each other through illegal layer boundaries.

## Quick start

```sh
git clone https://github.com/R11/saturn-libs
cd saturn-libs
make test            # host TDD pass across all libs
make test-tap        # same, TAP 14 output for CI
```

Saturn-target builds happen inside individual game projects (each
includes the libs it needs by include path). See `examples/` for
minimal Saturn-target snippets and `saturn-io/examples/hello/` for the
NetLink "dial + send hello" demo.

## Per-lib docs

- [saturn-base](saturn-base/README.md) — shared types, font, sgl_defs
- [saturn-smpc](saturn-smpc/README.md) — pads + RTC
- [saturn-vdp1](saturn-vdp1/README.md) — sprites / quads
- [saturn-vdp2](saturn-vdp2/README.md) — text + backgrounds
- [saturn-io](saturn-io/README.md) — NetLink modem stack
- [saturn-bup](saturn-bup/README.md) — backup RAM records
- [saturn-app](saturn-app/README.md) — lobby framework (L2)
- [test](test/README.md) — TDD framework

## Layout per lib

```
saturn-{name}/
  include/         portable public headers
  core/            portable C (no platform headers); host-testable
  saturn/          SH-2/SGL implementation; only built with the cross
  tests/           host unit tests via libs/test/
  Makefile         per-lib build for both host and saturn targets
```

Some libs add `web/`, `host/`, or `python/` shells (e.g. `saturn-io`
ships a Python bridge under `python/`).

## Toolchain

- **Host tests** need only `cc` (gcc or clang) plus `make`. The CI job
  installs `build-essential` on ubuntu-latest and runs `make test-tap`
  at the repo root.
- **Saturn cross-compile** needs an SH-2 GCC toolchain. The reference
  path is the Docker SDK shipped with `R11/saturn-tools`
  (`saturn/sdk/docker-saturn-build.sh`). Per-lib Makefiles read
  `$(SATURN_SDK_ROOT)` (default `/opt/saturn-sdk`) for `bin/sh-elf-gcc`
  and the SGL include directory.

The libs do not bundle a "build everything for Saturn" target — that
lives in consuming game projects.

## Layering

These libs sit at L1/L2 of the broader workspace's L1/L2/L3 model. They
are platform-specific (or platform-agnostic-but-Saturn-aware in
saturn-app's case): a Wii U or Dreamcast game cannot consume them
directly. Cross-platform games consume an L2 library elsewhere
(e.g. `cui`) which itself may consume L1 libs through PAL interfaces.

## License

MIT — see [LICENSE](LICENSE).
