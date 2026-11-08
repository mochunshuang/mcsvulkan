# intent7 UI ability matrix — migration report

Date: 2026-11-08
Scope: `test_ui_ability2` UI capability migration into the intent7 `[pass ... pass]`
framework, plus the resize/fullscreen closure that was missing in the first handoff.

## 1. Current status

Build: PASS
Normal run: PASS
Current scene: `ui_new::ability_test_scene`
Executable: `output/test_program/mcsvulkan/vma/test_intent7.exe`
Default client size: 1280 x 900 (fixed-pixel design grid, 7 x 6 cells)

Runtime stats:

```text
[ability-test] passes=7 glyphs=897 layer=rebuilt   # first frame / resize
[ability-test] passes=5 glyphs=897 layer=cached    # steady state
[pass-c-api] opcodes=37 calls=259
```

Normal run, runtime resize (1280x900 -> 1582x953 client) and F11 fullscreen
(1920x1080) all run with:

- no VUID
- no validation error
- no pass-validator failure
- no assert

`[resize] new extent=...` is printed by the swapchain rebuild path. Fullscreen
and windowed restore both rebuild the extent-dependent resources.

## 2. Fixed-pixel contract (the main design change)

The previous ability scene used `s = max(W / 800, 1)`, so every widget grew with
the window. The new scene is a fixed-pixel UI:

- tile = 170 x 126 framebuffer pixels, gap 8, margin 16, header 86 px.
- The grid never scales. A larger window only reveals more empty space around
  the grid; the grid is centered horizontally.
- Pixel -> NDC is still computed by `ui_dao::view{W,H}` with the live
  framebuffer extent. The background border, corner markers, right-aligned
  viewport readout, and the fixed grid all prove that the mapping follows the
  live framebuffer while the widget footprints stay constant.
- `VIEW 1582x953 | PX SCALE 1.0 (FIXED)` in the header is the live framebuffer
  extent; the tiles next to it remain 170 x 126 px. This is the same behaviour
  as `ADD_MSDF_DEF`-style fixed-size text/UI components.

Evidence screenshots:

- `output/intent7_ability.png` — 1280 x 900 client, default.
- `output/intent7_resize_1600.png` — 1582 x 953 client after runtime resize; the
  tile size is unchanged, only the surrounding space grew.

## 3. Capability matrix

36 individual feature cases + one composed panel. Every tile is a visible test
case, not a label without content.

### Shape program

1. solid fill + outer drop shadow + border
2. linear gradient (horizontal and vertical)
3. radial gradient
4. conic gradient
5. per-corner radius
6. border / nested ring
7. ellipse (anisotropic SDF)
8. outer shadow / blurred silhouette
9. inner shadow (soft border)
10. procedural alpha mask (dots and diagonal check)
11. sRGB-encoded vs linear color (real gamma difference on an sRGB swapchain)

### Path programs

12. triangle fill with analytic AA
13. pentagram fill with even-odd hole
14. arc stroke with caps
15. dashed stroke
16. rotated rounded rect
17. skewed rounded rect

### Texture / effect programs

18. nine-slice (3x3 sub-rects + matching UVs)
19. texture alpha mask (photo masked by the emoji atlas alpha channel)
20. explicit mip LOD 0 / 1.5 / 3 / 4.5
21. blur (comparison: blurred top + sharp bottom)
22. sepia color matrix
23. grayscale and invert color matrices
24. FBM noise
25. 4x MSAA + average resolve (thin rotated bars and 1.5 px borders)
26. real blend states: over / additive / multiply

### Text

27. MSDF glyphs (`Aa`, `MSDF 0123`)
28. RTL Arabic (bidi visual order, right aligned)
29. CJK (`世界`, `是的 一不`)
30. color emoji (`😀😃😄`, `😁😂🤣`)
31. UAX #14 wrapped paragraph in a fixed-width tile
32. mixed bidi line (`ABC مرحبا 123`)
33. text overlay is a separate LOAD/DONT_CARE pass

### Pass / target features

34. nested scissor clip (intersection with the tile scissor)
35. group opacity: two overlapping alpha shapes flattened to an offscreen
    layer and composited with alpha 0.55
36. backdrop blur: offscreen texture sampled with a real blur convolution
37. stencil clip: `D32_SFLOAT_S8_UINT`, mask `ALWAYS -> REPLACE`, content
    `EQUAL reference 1`
38. composed UI panel: gradient/texture/border/noise chips, mock table,
    additive/multiply glow, glass card, and several text scripts

Infrastructure exercised by the same scene:

- 7 passes on a rebuild frame: base / shape-path / texture-effect / offscreen
  layer / stencil clip / offscreen composite (secondary command buffer) / text
  overlay.
- retained offscreen composition layer: the offscreen + stencil passes are
  skipped on clean frames (7 -> 5 recorded passes).
- 2 MRT (color + `R32G32B32A32_UINT` pick), 4x MSAA, resolve, depth target.
- `LOAD/STORE` and `LOAD/DONT_CARE` attachment states.
- indexed + instanced + indirect + BDA.
- per-segment `setScissor`; nested `pushScissor/popScissor`.
- pipeline switches between opaque / alpha / additive / multiply.
- explicit pre/post image barriers around offscreen/stencil passes.
- `vkCmdExecuteCommands` for the composite pass (secondary command buffer).
- hover highlight from the MRT pick attachment.

## 4. Resize / fullscreen closure

The first handoff named this as the explicit gap. The fix has two parts.

### 4.1 Resource rebuild

`recreateSwapChain` now rebuilds, in addition to the swapchain/MSAA/depth/pick/
resolve attachments:

- offscreen MSAA color image,
- offscreen sampled resolve image,
- stencil (`D32_SFLOAT_S8_UINT`) attachment.

The offscreen/stencil builders are stored in `mainCtx` so they can be reused
with `setCreateInfoExtent`. The sampled offscreen image is replaced in the same
texture-manager slot, then `descriptorSetManager.update_texture()` is called so
the descriptor set points at the new image view. The ability scene gets the new
image/view handles through `ability_test_scene::updateResources()`; it also
invalidates its cached glyph quads because their NDC transform depends on the
framebuffer extent.

Old extent-dependent resources are released before the new ones are allocated
(`device.waitIdle()`, then free the texture slot / reset resources / destroy the
swapchain). At 1920x1080 the 128-bit pick attachment is large, and an
old+new overlap can exceed the device-local heap.

### 4.2 4x MSAA cap

The scene is a 4x MSAA showcase, but the attachments/pipelines used
`getMaxUsableSampleCount()` (8 on this device). At 1920x1080 the pick attachment
(`R32G32B32A32_UINT`, 16 bytes/texel, 8 samples) required ~265 MB and tripped
`VUID-vkAllocateMemory-pAllocateInfo-01713`. All main/offscreen/stencil
attachments and pipelines now use the named `SAMPLE_COUNT = 4` constant, which
matches the `4x MSAA` claim and stays inside the heap at fullscreen.

### 4.3 Fullscreen key

The shared WSI reserves F1 and toggles it on both press and release. To make the
intent7 target usable without editing the shared WSI, `test_intent7.cpp` owns an
edge-triggered **F11** fullscreen switch (save window pos/size, call
`glfwSetWindowMonitor`, restore on the next press). The resize/fullscreen path is
the same GLFW framebuffer-resize path as a drag resize.

Verified extents:

```text
start       1280x900  (client, window 1298x947)
resize      1582x953  (client, window 1600x1000)
fullscreen  1920x1080
restore     1280x900
```

### 4.4 Retained offscreen composition layer

The offscreen group/backdrop/stencil composition is now cached:

- `ability_test_scene::updateResources()` sets `layerDirty_ = true` when the
  swapchain/offscreen/stencil resources are rebuilt.
- On a dirty frame the scene records the 7 passes (base, shape, effect,
  offscreen, stencil, composite, text).
- On subsequent frames the scene skips the offscreen and stencil passes and
  records 5 passes (base, shape, effect, composite, text). The composite pass
  samples the retained offscreen resolve texture.
- The layer is static content (group/backdrop/stencil tiles), so hover and text
  changes do not invalidate it; resize/fullscreen does.
- The composite pass now writes the tile entity to the MRT pick attachment, so
  hovering over those tiles still selects the card.

This is a real layer cache, not just a text cache. Full damage-rect partial
redraw of the base/shape/effect layers is still a separate retained-layer step
(see section 7).

## 5. Cleanup

- Deleted `shaders/test_intent7.comp`.
- Removed the minimal compute pipeline and its `COMP_SHADER_PATH` CMake wiring.
  The compute pass was a no-op for UI; removing it also removes the extra
  shader target and the dead `computePipeline` fields.
- The transfer pass (`FillBuffer -> barrier -> CopyBuffer`) is still exercised.

## 6. Files changed in this pass

| File | Change |
|---|---|
| `test_intent7_ability_test.hpp` | Fixed-pixel 7x6 grid; 36 feature cases + composed panel; text lab (MSDF/RTL/CJK/emoji/wrap/mixed bidi); emoji texture alpha mask; cached text per extent; retained offscreen composition layer; `updateResources()`. |
| `test_intent7.cpp` | 1280x900 default client; `SAMPLE_COUNT = 4`; store offscreen/stencil builders; rebuild them on resize; release old resources; update texture slot + descriptors; scene handle refresh; F11 fullscreen; remove compute pipeline; delete compute CMake path. |
| `vma.cmake` | Removed `test_intent7.comp` shader target and `COMP_SHADER_PATH`. |
| `shaders/test_intent7.frag` | Linearize the sRGB emoji bitmap atlas before writing to the sRGB swapchain. |
| `INTENT7_MIGRATION_REPORT.md` | This report. |
| `output/intent7_ability.png` | Current default-size ability screenshot. |
| `output/intent7_resize_1600.png` | Runtime-resize screenshot showing fixed tile pixels. |

## 7. Where HDR and damage/layer-cache actually belong

intent6 fixed the layering:

```text
Widget -> controller::client -> service::canvas -> ui_dao -> shader_data
       -> [pass ... pass] -> FrameBuilder/OpStream -> VkEmitter -> vkCmd*
```

The two remaining items are **not** DAO/shader/pass-API problems. They are
service-layer problems, which is why they were not faked inside the ability
tile switch:

### HDR / wide gamut

- The DAO already emits colors, the shader already works in linear light, and
  `PassTarget` already carries an arbitrary `VkFormat`. The missing part is the
  **service/WSI color contract**: swapchain format/color-space negotiation
  (`VK_EXT_swapchain_colorspace`), `VkHdrMetadataEXT`
  (`VK_EXT_hdr_metadata`), 10-bit or float formats, and a display that actually
  supports HDR.
- If the swapchain candidate list only asks for `B8G8R8A8_SRGB` +
  `SRGB_NONLINEAR` (the current `test_intent7.cpp` setup), an "HDR" tile would
  only show PQ math on an SDR output. That is not the feature; it would be a
  demo of a transfer curve. The honest implementation is a `color_service`
  that owns:
  - source color space (sRGB UI colors, sRGB photo textures, linear MSDF/emoji
    glyphs),
  - output color space (SDR sRGB, Display-P3, or Rec.2020/PQ),
  - tone mapping / gamut mapping,
  - the swapchain/offscreen format choice.
- The shader contract stays "write linear light to an sRGB/linear/PQ target";
  the DAO contract stays "rect + color + program parameters". The pass framework
  does not need to change. This is a sizeable service/WSI change, not a tile.

### Damage / dirty region / layer cache

- The pass framework can already express the operations: `LOAD`, scissor and
  nested scissor, offscreen targets, explicit image barriers, and per-pass
  `PassSource` lists. The hard part is **retained state ownership**:
  - swapchain images are recycled, so partial redraw must target a persistent
    UI layer, not the swapchain image;
  - the layer is per-frame-in-flight and must be invalidated on resize, format
    change, text cache rebuild, hover/animation, and layer content change;
  - dirty rectangles are produced by the **controller**, not the shader: hover,
    pressed/selected state, text edit, scroll, layout change;
  - the service must decide between "redraw dirty rects into the layer" and
    "redraw a layer subtree", then composite the layer to the swapchain;
  - barriers and resolve must follow the dirty region/layer, not the whole
    framebuffer.
- Current state: `ability_test_scene` is still immediate-mode for the base /
  shape / effect / text layers; `render()` re-emits those tile instances every
  frame. The offscreen group/backdrop/stencil composition is retained and
  skipped on clean frames (section 4.4), and the glyph quads are cached per
  framebuffer extent. Full damage-rect partial redraw of the base/shape/effect
  layers is the remaining architectural step: it needs the service to own a
  retained UI layer + damage list, not a local `if` in the tile switch.

### Fonts

Font registration, atlas generation and shaping are **font service/loader**
concerns. The renderer only consumes `TexQuad` glyphs. Variable-font axes, if
added later, change the glyph quads produced by that service; they do not add a
new shader/pass feature. They are therefore not listed as a rendering gap.

## 8. Known gaps / not claimed

1. **HDR / wide gamut surface** is not implemented (section 7). Current output is
   SDR `B8G8R8A8_SRGB`; the sRGB/linear and emoji-linearization paths are real
   SDR color management, not HDR.
2. **Full damage-rect partial redraw** is not implemented. The offscreen
   composition layer cache is implemented (section 4.4) and the glyph quads are
   cached, but the base/shape/effect/text layers are still re-emitted every
   frame. Damage-rect partial redraw needs the retained controller/service UI
   layer described in section 7.
3. `test_ui_ability2.cpp` is still not integrated line-for-line; this is a
   Widget-level reimplementation of the same capability families.
4. The screenshot capture path on this desktop is clipped/scaled by the display
   driver for the 1920x1080 fullscreen window. The resize screenshot is the
   reliable visual proof; fullscreen is verified by `[resize] new extent=1920x1080`
   and the absence of VUIDs.
5. `run_text_pipeline` prints its full normalization/bidi/shaping trace for
   every text run. The ability scene caches text per extent, so this happens on
   startup and on resize, not every frame. It is useful for the pass/ABI demo
   but noisy for a UI app.

## 9. Build / run

```powershell
cd E:\0_github_project\mcsvulkan\build
ninja mcsvulkan-vma-test_intent7
```

```powershell
E:\0_github_project\mcsvulkan\output\test_program\mcsvulkan\vma\test_intent7.exe
```

Controls:

- drag the window edge: resize; the offscreen/stencil resources are rebuilt.
- F11: fullscreen toggle (the WSI F1 handler is double-triggered).
- hover over any tile: MRT pick highlights the card.

## 10. Verdict

- UI capability showcase: 36 visible feature cases + composed UI, all in the
  `[pass ... pass]` framework.
- Fixed-pixel behaviour: confirmed at 1280x900, 1582x953 and 1920x1080.
- Runtime resize/fullscreen: offscreen MSAA/resolve and stencil resources now
  follow the swapchain; fullscreen passes validation.
- Retained offscreen composition layer: rebuild on resize/fullscreen, cached
  (5 passes) on steady frames.
- Text: MSDF, RTL, CJK, color emoji, wrapping and mixed bidi are rendered;
  emoji bitmaps are linearized for the sRGB swapchain.
- Dead compute shader/pipeline removed.
