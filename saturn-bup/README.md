# saturn-bup

Saturn Backup RAM (BUP) storage — named records on the 32 KB internal
cart, behind a single API that works the same on Saturn hardware and
on a host development machine.

Two PAL backends:

- **Saturn shell** — calls into the BUP BIOS via the SH-2 vector table
  at `0x06000350`, with a 16 KB workspace owned by the lib.
- **Host shell** — files under `~/.lobby_bup/` (or `$SBUP_HOME`), one
  file per record name, raw bytes.

Filenames are caller-supplied ASCII (≤11 chars); the lib pads to 11
internally to match the BUP directory format. Writes always overwrite.

## Public API

| Header                          | What it provides                              |
|---------------------------------|-----------------------------------------------|
| `saturn_bup.h`                  | handle, `sbup_init/read/write/erase/stat`     |
| `saturn_bup/host.h`             | `sbup_register_host_pal`, `sbup_host_set_basedir` |
| `saturn_bup/saturn.h`           | `sbup_register_saturn_pal` (BUP BIOS PAL)     |
| `saturn_bup/pal.h`              | `sbup_pal_t` install hook (custom backends)   |

Surface:

- `sbup_init(handle)` after a PAL is registered
- `sbup_read(h, name, out, cap, *out_len)`
- `sbup_write(h, name, data, len)` (overwrite)
- `sbup_erase(h, name)` (idempotent)
- `sbup_stat(h, &info)` -> totals + free + record count
- `sbup_last_error(h)` / `sbup_error_string(err)` for human strings

Errors mirror BUP BIOS values (`SBUP_NOT_FOUND`, `SBUP_NO_SPACE`,
`SBUP_VERIFY_FAILED`, ...) so callers see a single error space across
backends.

## Integration

```c
#include <saturn_bup.h>
#include <saturn_bup/saturn.h>      /* or saturn_bup/host.h on host */

int main(void) {
    sbup_register_saturn_pal();      /* one-shot at boot */

    sbup_handle_t h;
    if (!sbup_init(&h)) return 1;

    static const uint8_t payload[] = "hello bup";
    if (!sbup_write(&h, "MYGAME_SAVE", payload, sizeof(payload))) {
        /* handle sbup_last_error(&h) */
    }

    uint8_t buf[64];
    size_t  n = 0;
    if (sbup_read(&h, "MYGAME_SAVE", buf, sizeof(buf), &n)) {
        /* buf[0..n) holds the record */
    }
    return 0;
}
```

## Build / test

```sh
make test       # host PAL exercised through ~/.lobby_bup/
make test-tap
make clean
```

The host PAL respects `$SBUP_HOME`; tests use `sbup_host_set_basedir`
to pin a tmpdir per test.

## Layer

L1 Saturn-specific. Depends on `saturn-base`. The Saturn shell uses
the BUP BIOS jump table; the host shell uses POSIX file IO. No
platform headers leak through `saturn_bup.h`.
