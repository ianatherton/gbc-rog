---
name: verify
description: Build and drive gbc-rog (GBC ROM) in a real SDL SameBoy window via X11/XTest for on-device visual verification.
---

# Verifying gbc-rog in-emulator

Surface is pixels — this is a GBC ROM, not a service. `make` then drive a real
SDL SameBoy window with synthetic X11 input; screenshot and read the HUD/log
text. Headless `sameboy_tester`/`sameboy_scripted` only do random "monkey"
input and can't reach a specific game state on purpose — use the real display
instead (`DISPLAY=:0`, python-Xlib + XTest available).

## Build + launch

```bash
make   # from repo root
DISPLAY=:0 LD_LIBRARY_PATH="$PWD/emu/.deps/sdl2/lib" \
  nohup ./emu/SameBoy-1.0.3/build/bin/SDL/sameboy --stop-debugger \
  "$PWD/build/gbc/gbc-rog.gbc" > /tmp/sameboy_run.log 2>&1 &
sleep 2
DISPLAY=:0 wmctrl -lp | grep -i sameboy   # find YOUR window's id — a pre-existing
                                          # user SameBoy session may already be running;
                                          # never touch/kill that one, match by PID
```

## Drive it

Default SDL keymap: arrows = D-pad, **A = x key, B = z key, Select =
BackSpace, Start = Return**. A minimal driver (adapt as needed — save it to
scratchpad, it's throwaway):

```python
# drive.py <winid-hex> tapshot <Key names...> <out.png>   |   drive.py <winid> shot <out.png>
import sys, time
from Xlib import X, display, XK
from Xlib.ext import xtest
WINID = int(sys.argv[1], 0)
d = display.Display()
KEYMAP = {"Up":"Up","Down":"Down","Left":"Left","Right":"Right",
          "A":"x","B":"z","Select":"BackSpace","Start":"Return"}
def focus():
    win = d.create_resource_object('window', WINID)
    win.set_input_focus(X.RevertToParent, X.CurrentTime); d.sync()
def tap(name, hold=0.06, after=0.12):
    kc = d.keysym_to_keycode(XK.string_to_keysym(KEYMAP.get(name, name)))
    xtest.fake_input(d, X.KeyPress, kc); d.sync(); time.sleep(hold)
    xtest.fake_input(d, X.KeyRelease, kc); d.sync(); time.sleep(after)
def screenshot(path):
    win = d.create_resource_object('window', WINID)
    g = win.get_geometry()
    raw = win.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
    from PIL import Image
    Image.frombytes("RGB",(g.width,g.height),raw.data,"raw","BGRX").save(path)
# main: focus(), then tap()/screenshot() per argv (see call sites above)
```

Read results with the Read tool on the saved PNG (multimodal) — don't try to
OCR/parse pixels yourself.

## Boot flow (title → gameplay)

1. Title screen → tap `A` then `Start` to reach class select (sometimes needs
   `A` then `Start` again — check the screenshot, don't assume the first tap
   landed).
2. Class select → `Start` confirms the highlighted class (Knight by default).
3. Story crawl (black screen, then scrolling text) → several `A` taps skip it.
   A "Descending" transition screen follows, then gameplay on the hub
   (floor 0) — screenshot to confirm before proceeding, loading can take a
   frame or two.

## Fastest path to a specific state

Temporarily patch source rather than fighting menu/hub navigation blind.
Mark every such line `// TEMP-VERIFY: ...`:
- `level_init.c`'s fresh-run branch (`else` block, ~line 75): change
  `floor_num = 0;` to boot straight onto any floor (e.g.
  `TOWN_FLOOR_BASE + 0u` for town 0's interior). Add `run_seed = 12345u;`
  alongside it to also fix the RNG for a fully reproducible layout (town
  interiors are deterministic from `(run_seed, town_id)` — see
  `biome_town.c`'s `tg_hash`/`town_generate_interior`; building jitter is
  ±1 tile and up to ~6 random deco pines get scattered, so a fixed seed
  avoids flaky obstacle placement across runs).
- To trigger a specific code path without navigating to it (e.g. a signpost
  read that depends on procedurally-placed, seed-dependent coordinates you
  don't want to hunt for): wire a normally-unused hub-only button (SELECT and
  bare B are both no-ops on the hub, guarded by `floor_num == 0u` elsewhere in
  `state_gameplay.c`) to call the function directly near the top of
  `state_gameplay_tick`, e.g. `overworld_signpost_read(SIGN_KIND_TOWN | 0u)`.
  **Must also force a redraw after the call** — `ui_combat_log_push` only
  writes to a WRAM buffer; nothing repaints the screen until the normal
  per-turn draw path runs. Add `wait_vbl_done(); draw_gameplay_overlays(g_player_x, g_player_y);`
  right after, or the chat log update won't appear in your screenshot even
  though it happened. (Learned this the hard way: first attempt captured a
  stale HUD because the push had no accompanying redraw.)
  Caveat: BackSpace (Select) key delivery via XTest was flaky in one session
  (worked once, then silently no-op'd on repeat taps) while `z` (B) was
  reliably registered every time — if a synthetic key seems to do nothing,
  don't assume the game logic is broken; retry with a different/adjacent
  input or fall back to real navigation before concluding it's a code bug.
- Revert every TEMP-VERIFY line and rebuild before finishing. Confirm with
  `git diff --stat -- src/` that only the real feature files changed.

## Gotchas

- Combat/chat log panel (`ui_draw_combat_panel` in `ui.c`) only shows pushed
  log lines when `combat_log_any()` is true; otherwise it falls back to an
  idle stats panel (class/level, seed words, mons/items count) — don't
  mistake the idle panel for "nothing happened."
- The log is a 3-row ring buffer (`COMBAT_LOG_LINES`) — pushing 2 lines
  shifts older content up/off; account for this when reading a screenshot
  that mixes leftover text with your new push.
- BMP screenshots from `sameboy_tester` (headless tool, not the SDL path
  above) use a nonstandard header — PIL can't open them directly; manual
  decode needed if you ever use that tool instead (data offset at byte
  0x0A, width 0x12, height 0x16 negative=top-down, pixels `[0,B,G,R]`).
