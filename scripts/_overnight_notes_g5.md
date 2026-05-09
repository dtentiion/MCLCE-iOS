# Overnight notes — G5 + F3-real-input session (2026-05-03)

You went to bed mid-G5 with chunk render hitting a 0x0 SIGSEGV inside
`updateDirtyChunks`'s forced search. Below is what landed while you
were out.

## State when you left

- Build at `50dc711`: sideload-safe. Sky/ground/sun/cloud rendering
  works. No terrain. UDC2_CKPT diagnostic logs every 60 calls but
  showing `dirtyChunkPresent=0` because of upstream's 125ms cooldown
  resetting the flag.
- chunks[0].length = 2304 (allocation worked). lists=11 (no chunks
  rebuilt — display lists empty).

## What I shipped (newest first; CI green at the latest unless noted)

0. **`7dabe48` — TEMP left-stick walk (extends `7dac9e3`).**
   Free-fly movement: y = forward/back along facing direction, x =
   strafe. No collision, no gravity. Same TEMP non-parity bypass as
   right-stick look-around. Walking shifts the camera through the
   world so cloud parallax (clouds are world-fixed at altitude)
   should become visible.

1. **`7dac9e3` — TEMP non-parity look-around via right stick.**
   Right stick now directly mutates `g_player->xRot` / `yRot` each
   frame, bypassing the upstream `LocalPlayer::tickInput → Input::tick
   → InputManager` chain. That chain stays parity-correct (item 3
   below kept it intact) but can't actually run yet because
   LocalPlayer.cpp doesn't compile in our build (UI/render deps) and
   `Input::tick` derefs `Minecraft::localgameModes` which our shim
   doesn't have. Right stick = camera tracks, marked TEMP, will be
   replaced once LocalPlayer wires up. Pitch clamped to ±90°.
   Sensitivity ~4°/frame at full deflection.

2. **`30d151f` — camera rotation reads player xRot/yRot.**
   The world-drive matrix setup applies `glRotatef(xRot, 1, 0, 0)`
   and `glRotatef(yRot + 180, 0, 1, 0)` before the player-position
   translate. Order matches upstream GameRenderer.

3. **`c35a7e1` — F3-real-input wiring.**
   Replaced variadic-stub `C_4JInput` methods with non-template
   overloads that route through `INP_iOS_Bridge` into the
   GameController reader. Sticks, triggers, connection check. Y axis
   inverted on stick reads (GC y-up vs upstream y-down). Currently
   inactive (no LocalPlayer is calling these) but parity-correct so
   the moment LocalPlayer wires up, it just works.

4. **`a45e5ec` — WD_CKPT brackets around upstream calls.**
   World-drive code logs `WD_CKPT before/after updateDirtyChunks`
   and `before/after render` for the first few calls via os_log
   (system-retained even on signal kill). If any chunk crash
   returns we'll see exactly which call took it.

5. **`50dc711` — reverted force-dirty.**
   Forced `dirtyChunkPresent=true` was crashing inside the search
   on something deeper than the lc null guard reaches. Reverted to
   plain diagnostic mode so the build stays sideload-safe.

## What you should see when you sideload

The latest `.ipa` should:
- Boot fine (sky / sun / clouds visible).
- **Right stick rotates the camera** — tilt with pitch, yaw with horizontal.
  The sky / sun / cloud quads stay at fixed world positions, so as you
  rotate you see different parts. Pitch clamped at top/bottom.
- Same lack of terrain (chunks still don't rebuild).
- No crash.
- Tick log includes `chunks0=2304 lists=11 draws=~370/tick` as before.

## What I did NOT touch

- The G5 chunk-render crash. Concluded the 0x0 deref is inside
  `Chunk::rebuild`'s deep tile-tessellation code, beyond what
  static analysis from non-running builds can pin. Needs a live
  sideload session with WD_CKPT logs to narrow. Deferred for your
  return.

- Left stick player movement. With no terrain, walking around just
  moves the camera against fixed sky lists — visually noisy, not
  much win. Skipped to keep the TEMP non-parity surface small.

- Real LocalPlayer / Input::tick. That's the parity path forward
  for input but heavy (LocalPlayer.cpp blocked on UI/render deps).

## Cleanup plan when proper LocalPlayer lands

When the parity input path is wired:
1. Delete the TEMP block in `mcle_world_drive_renderer` that mutates
   `g_player->xRot/yRot` directly.
2. Camera-rotation read of `g_player->xRot/yRot` stays — it's already
   parity-correct, just sourced from upstream's tickInput path instead
   of our short-circuit.
3. F3 wiring (4J_Input.h overloads) stays — already parity-correct.

## If anything regresses

`50dc711` is the known-good fallback. Any later commit on `main`
that's broken: `git revert <hash>`, push, re-CI, sideload that.

Camera-rotation (`30d151f`), F3-input (`c35a7e1`), and the TEMP
look-around (`7dac9e3`) are additive and shouldn't crash anything
that wasn't already crashing.

## Parity-input path: what it'd take (read-only investigation)

While idling I traced what's needed to swap the TEMP bypass for
upstream's `LocalPlayer::aiStep` -> `input->tick(this)` flow.

- `upstream/Minecraft.Client/Input.cpp` IS already in our build
  (WorldProbe/CMakeLists.txt:924). So `Input::tick(LocalPlayer*)`
  itself would link.
- `Input::tick` derefs `pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_*)`
  on every axis check (Input.cpp:40, 43, 89, 99, 139, 141, 193).
  Our `Minecraft` shim doesn't have `localgameModes[]` populated.
- Stub plan: add a `MultiPlayerGameMode_iOSStub` whose
  `isInputAllowed(int)` returns true unconditionally, point
  `mc->localgameModes[0]` at a static instance. ~20 lines.
- BUT `Input::tick` only writes raw axis values into `input->xa/ya`
  + button state. The actual movement integration (gravity,
  collision, friction, jump arc) lives in `LocalPlayer::aiStep`
  (LocalPlayer.cpp:165-...). aiStep is what mutates `x/y/z`.
- `LocalPlayer.cpp` doesn't compile because:
  1. `ConsoleUIController` (Xbox_UIController.h, ~60 virtuals)
     is referenced as `extern ConsoleUIController ui;` at line 59
     and called heavily in `handleMouseDown` and others. Stubbing
     means a no-op subclass with all 60+ pure virtuals overridden.
  2. `MultiplayerLocalPlayer.cpp` and friends pull in render/UI
     headers that themselves transitively pull D3DX9, GameSWF
     internals, etc. Each blocker is a header patch + stub.
- Estimated effort: 2-3 sessions for `ConsoleUIController` no-op
  subclass + each transitive header. Probably worth it because it
  unlocks every gameplay feature that goes through aiStep
  (movement, jumping, sneaking, sprint, inventory open via input).

So the right sequence on return is:
1. Sideload `7dabe48`, confirm right/left stick actually drive
   the camera + free-fly walk (or capture the WD_CKPT logs if
   anything regressed).
2. Pin the G5 chunk crash via WD_CKPT brackets.
3. Then start the LocalPlayer compile push (separate from G5).

## Pre-staged: candidate deref sites in Chunk::rebuild

When the WD_CKPT log lands, if it points at `updateDirtyChunks ->
Chunk::rebuild`, here are the candidates ranked by likelihood of
the 0x0 SIGSEGV. Each is a single-line CKPT addition we can land
in one push.

Read upstream/Minecraft.Client/Chunk.cpp:182-485

Ranked by likelihood (highest first):

1. **Chunk.cpp:237** — `level->getChunkAt(x,z)->getBlockData(tileArray)`
   TWO derefs. `level` is the chunk's stored level pointer (set
   by us via setLevel). `getChunkAt(x,z)` could return null on
   the client if our MultiPlayerChunkCache miss handler returns
   nullptr instead of an empty chunk. `getBlockData(...)` then
   derefs at offset 0 -> 0x0 SIGSEGV. Strong fit.

2. **Chunk.cpp:217** — `levelRenderer->getGlobalIndexForChunk(...)`
   `levelRenderer` is the chunk's back-pointer. If `setLevel`
   set `level` but didn't set `levelRenderer`, this 0x0's. Worth
   a quick assert before line 237.

3. **Chunk.cpp:239** — `new Region(level, ...)`
   Region ctor walks `level->getChunkAt(...)` for each chunk in
   the region range. If any returns null inside Region::Region,
   we crash there instead of at line 237.

4. **Chunk.cpp:240** — `new TileRenderer(region, ...)`
   TileRenderer ctor walks tileIds and may call back into
   region. Less likely than 1-3 since region just constructed
   above.

5. **Chunk.cpp:300+** — per-tile render loop.
   Reads tileIds[] which we passed in. Iterates 16x16x128. If
   maxBuildHeight or COMPRESSED_CHUNK_SECTION_HEIGHT differs from
   what saveData.ms actually has, we'd index past array end. But
   that's an OOB heap read, not a 0x0 deref.

6. **Chunk.cpp:459/464** — `levelRenderer->clearGlobalChunkFlag(...)`
   Same null candidate as #2.

7. **Chunk.cpp:491** — `levelRenderer->getGlobalIndexForChunk(...)`
   followed by `EnterCriticalSection(globalRenderableTileEntities_cs)`.
   If our `globalRenderableTileEntities_cs` global isn't init'd
   (initialiser missed), EnterCriticalSection on null lock crashes.
   Check WorldProbe/probe_stub.cpp for this global.

Plan after log lands: bracket lines 217, 237, 239, 240 with
CHUNK_REBUILD_CKPT lines (Python patch in scripts/), push, sideload,
narrow.

### Hypothesis update (2026-05-03)

Read MultiPlayerChunkCache.cpp:241-258 and ServerChunkCache.cpp:253-273
(via grep) — confirmed both `getChunk` paths return waterChunk /
emptyChunk sentinels rather than nullptr on a miss. Level::getChunkAt
(Level.cpp:904) just forwards to `chunkSource->getChunk`. So
`level->getChunkAt(x,z)` returning null is unlikely.

That weakens suspect #1. New ranking:
- **#1 now**: getBlockData on emptyChunk sentinel — if emptyChunk's
  internal data array is null/uninit, getBlockData walks it and 0x0's.
- **#2**: line 217 `levelRenderer->getGlobalIndexForChunk` — if our
  LevelRenderer wasn't ctor'd properly so its level[] isn't set.
- **#3**: inside Region(level, ...) ctor — Region preloads tile data
  from level for the bounds, so any of the same getBlockData paths
  fire here.
- **#4**: TileRenderer ctor reads tileIds we passed in; if the static
  array `tileIds[16*16*Level::maxBuildHeight]` is shorter than
  upstream's expected layout (e.g. our maxBuildHeight differs), OOB.

The pre-staged CHUNK_REBUILD_CKPT script logs all four points, so
once log lands we'll know which call took the SIGSEGV. emptyChunk
internals are check #1 to investigate post-log.
