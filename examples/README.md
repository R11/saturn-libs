# Examples

Tiny Saturn-target snippets that show how to consume one lib end-to-end.
Each example is orientation, not a tutorial — the per-lib README is the
real reference.

| Example                                   | Demonstrates                       |
|-------------------------------------------|------------------------------------|
| [`vdp1-quad/`](vdp1-quad/)                | A single filled quad on VDP1       |
| [`vdp2-text/`](vdp2-text/)                | NBG0 cell-mode "hello" + backdrop  |
| [io-hello](../saturn-io/examples/hello/)  | NetLink dial + send hello (in saturn-io) |

## Building

All examples target the SH-2 toolchain in `saturn-tools`' Docker SDK.
With `$(SATURN_SDK_ROOT)` pointing at a local SDK install:

```sh
cd examples/vdp1-quad
make
```

If you don't have the SDK installed locally, run via the Docker
wrapper from `saturn-tools`
(`saturn/sdk/docker-saturn-build.sh`) pointed at the example
directory.

The host TDD pass at the repo root (`make test`) does **not** need the
Saturn SDK — it builds and runs the per-lib host tests only.
