# saturn-app

A platform-agnostic lobby framework. Owns a registry of games, a top-
level state machine (name entry / unified lobby / playing / game over /
playing online / results), a BUP-backed identity record, and an online
context fed by a pluggable network backend. The platform shell drives
one entry point per frame and gets a `lobby_scene_t` back to render.

This is the sole **L2** library in this repo: it consumes L1 libs
(saturn-bup for identity persistence, saturn-vdp1/vdp2 indirectly via
the rendered scene) but is itself portable C — the same code drives the
Saturn shell, a host harness, and the web frontend.

## Public API

| Header                                | What it provides                          |
|---------------------------------------|-------------------------------------------|
| `saturn_app.h`                        | `sapp_init`, `sapp_run_one_frame`, registry |
| `saturn_app/scene.h`                  | `lobby_scene_t`, quad/text/bg ref         |
| `saturn_app/game.h`                   | `lobby_game_t` plug-in contract           |
| `saturn_app/state.h`                  | top-level `lobby_state_t` enum            |
| `saturn_app/identity.h`               | LOBBY_ID record (BUP-backed)              |
| `saturn_app/local_lobby.h`            | unified-screen state + cursor + votes     |
| `saturn_app/online.h`                 | room list / room state / lockstep accessors |
| `saturn_app/net.h`                    | `sapp_net_host_install` (host TCP backend)|
| `saturn_app/widgets/keyboard.h`       | on-screen keyboard widget                 |

Frame contract: shells call `sapp_init`, `sapp_bootstrap_identity`,
register one or more `lobby_game_t`, then call `sapp_run_one_frame`
once per frame with a fresh `lobby_input_t[LOBBY_MAX_PLAYERS]`. The
returned `lobby_scene_t*` carries quads, texts, and a backdrop ref
the shell lowers onto saturn-vdp1/vdp2 (or a canvas, or stdout).

Game registration: each game ships a `lobby_game_t` with `init`, `tick`,
`render_scene`, `is_done`, `teardown`. The framework allocates
`state_size` bytes of arena per game.

## Integration

```c
#include <saturn_app.h>
#include <saturn_app/game.h>

extern const lobby_game_t my_game;     /* defined by your game */

int main(void) {
    /* shell: install BUP + smpc + vdp1 + vdp2 PALs first */
    sapp_init(320, 224, /*boot_seed*/ 0xC0FFEE);
    sapp_bootstrap_identity();
    sapp_register_game(&my_game);

    lobby_input_t inputs[LOBBY_MAX_PLAYERS] = {0};
    while (1) {
        slSynch();
        saturn_smpc_poll();
        const saturn_smpc_pad_t* p1 = saturn_smpc_pad_by_addr(0, 0);
        inputs[0] = p1 ? p1->buttons : 0;

        const lobby_scene_t* s = sapp_run_one_frame(inputs);
        /* shell lowers s onto vdp1/vdp2 here */
        (void)s;
    }
}
```

## Build / test

```sh
make test
make test-tap
make clean
```

The host build links a TCP backend so online states are exercisable
without a Saturn or NetLink.

## Layer

L2. Consumes `saturn-base` (result type) and `saturn-bup` (identity
persistence). Calls into platform-specific backends through a vtable
(`sapp_net_*`); does not import saturn-vdp1/vdp2 directly. Saturn
shells live in sibling app/game projects, not here.
