# Texture pipeline plan (post G5-step27 bypass)

## Why we bypassed

`getTexture(tt)` returns null for every tile because `textures->stitch()` is never called. Our `g_minecraftShim->textures` is currently a zero-buffer shim. As a result `TileRenderer::tesselateBlockInWorldWithAmbienceOcclusionTexLighting` derefs null Icons and crashes.

Bypass at TileRenderer.cpp ~line 319 makes `tesselateBlockInWorld` return false for now. Build is stable, chunks rebuild empty meshes, no crash.

## Upstream parity-correct path

`upstream/Minecraft.Client/Minecraft.cpp:411` does:
```cpp
levelRenderer = new LevelRenderer(this, textures);
textures->stitch();
```

`Textures::stitch()` (Textures.cpp:1370) just delegates:
```cpp
void Textures::stitch() {
    terrain->stitch();
    items->stitch();
}
```

Each `terrain` and `items` is a `PreStitchedTextureMap`. Their `stitch()` (PreStitchedTextureMap.cpp:36):
1. `loadUVs()` — populates SimpleIcons at hardcoded UV coords (no I/O)
2. Loops `Tile::tiles[i]->registerIcons(this)` for terrain
3. Loops `Item::items[i]->registerIcons(this)` for items
4. Loads atlas PNG via `TexturePack::getImageResource()`
5. Creates GPU `Texture` via `TextureManager::createTexture()`
6. Re-inits Icons with proper texture+frame
7. Animation handling via `makeTextureAnimated()`
8. `stitchResult->updateOnGPU()`

## Current iOS state

**Already compiled in WorldProbe/CMakeLists.txt:**
- SimpleIcon.cpp
- StitchedTexture.cpp
- Stitcher.cpp
- TextureHolder.cpp
- TextureAtlas.cpp
- TextureChangePacket.cpp / TexturePacket.cpp / etc
- FileTexturePack.cpp / FolderTexturePack.cpp / TexturePack.cpp

**Not compiled (need to add):**
- TextureManager.cpp ← **Step 1 (this push)**
- Textures.cpp
- PreStitchedTextureMap.cpp
- (TextureMap.cpp — alternative path, not needed for PreStitched)

**Stubbed in iOS WorldProbe/link_stubs.cpp:**
- Lots of Textures methods with no real bodies — these will collide with real Textures.cpp once it compiles, will need cleanup.

## Implementation plan (smallest first)

### Step 1: Compile TextureManager.cpp [G5-step28, this push]
Add `${UPSTREAM_DIR}/Minecraft.Client/TextureManager.cpp` to WorldProbe CMakeLists.txt. Self-contained — no calls into it yet. CI tells us if any symbols are missing (likely Texture / Stitcher methods).

**Risk:** Low. Both Texture and Stitcher already compile.

### Step 2: Compile Textures.cpp + PreStitchedTextureMap.cpp [G5-step29]
Add both. Likely link errors against:
- `TexturePackRepository` methods
- `BufferedImage` methods
- Existing stubs in link_stubs.cpp may collide

**Risk:** Medium. Need to remove conflicting stubs.

### Step 3: Wire TexturePackRepository in MCLEGameLoop [G5-step30]
Read upstream `Minecraft::init` to find the construction site. Likely:
```cpp
skins = new TexturePackRepository(...)
options = new Options(...)
```
These are prerequisites for the Textures ctor.

**Risk:** Medium-high. TexturePackRepository may have its own init dependencies (file I/O for resource pack discovery).

### Step 4: Replace zero-buffer Textures with real instance [G5-step31]
In MCLEGameLoop.cpp, replace placement-new of zero buffer with:
```cpp
Textures *textures = new Textures(skins, options);
g_minecraftShim->textures = textures;
g_levelRenderer->textures = textures;
```
Don't call stitch yet. Just verify ctor doesn't crash.

**Risk:** Medium. Textures ctor calls `loadIndexedTextures()` which loads ~170 named PNGs. May fail file resolution.

### Step 5: Call textures->stitch() [G5-step32]
After LevelRenderer construction:
```cpp
textures->stitch();
```
Watch log for:
- `Tile::tiles[i]->registerIcons` loop completing (or crashing on a specific tile)
- File-not-found warnings (acceptable, tracked)
- Atlas GPU upload via existing mcle_glbridge_* pipeline

**Risk:** High. Many sub-paths, each can have its own missing init. Likely 1-3 sub-rounds of narrowing.

### Step 6: Revert tesselateBlockInWorld bypass [G5-step33]
Once `getTexture(tt)` returns real Icons, revert the bypass at TileRenderer.cpp:319 (in patch-tilerenderer-tesselate-checkpoints.py). Sideload — terrain should render with real textures.

**Risk:** Medium. Possible additional null-derefs deeper in tesselateBlockInWorldWithAmbienceOcclusionTexLighting that the texture fix doesn't cover. Same recipe as before — narrow with CKPTs.

## Known landmines

- **D3DX9 PNG decode** in upstream `BufferedImage`. Not iOS-compatible. We have a Metal-side PNG bridge (`mcle_glbridge_load_or_get_png_path`) that the existing G4 sun/moon/clouds path uses. May need to intercept or stub.
- **TexturePack file resolution** — upstream looks for `textures/blocks/` etc. relative to a configured root. iOS sandbox layout (`Documents/Common/res/`) may differ. Expect path-fixup work.
- **Animation frame loading** — secondary, safe to skip if files missing.
- **Sparse Tile/Item arrays** — Tile::tiles[4096] / Item::items[32000] iteration. Null-check guards already in upstream loops, should be fine.
- **link_stubs.cpp collisions** — when Textures.cpp compiles, stubs in link_stubs.cpp will duplicate-define symbols. Need to remove stubs as real bodies land.

## Pace estimate

Without a Mac (CI-based loop): ~2-3 sideload rounds per step, ~30 min each round = 6-10 hours over 2-3 sessions to close G5 fully.

With a Mac (lldb attached): 1-2 hours total. (Documenting for future reference.)
