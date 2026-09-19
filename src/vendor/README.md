`stb_truetype.h` is copied from the pinned ImGui submodule's
[`imstb_truetype.h`](https://github.com/RaidcoreGG/imgui/blob/58075c4414b985b352d10718b02a8c43f25efd7c/imstb_truetype.h)
(stb 1.20, with its original license retained).

The only change is in `stbtt__GetGlyphShapeTT`: a contour containing one
off-curve point is treated as a degenerate on-curve contour. There is no next
point in that contour to interpolate with. The original code reads past its
vertex allocation for SimSun's space glyph (glyph 3, one contour, one point,
flags 48), confirmed by AddressSanitizer with `simsun.ttc`.

`../ImGuiFontBuild.cpp` selects this header for the addon's local atlas builder.
`../MapGlyphAtlas.h` uses the same header for cmap checks. The ImGui submodule,
its public structures, and Nexus remain unchanged.

Review the patch with:

```
diff -u src/imgui/imstb_truetype.h src/vendor/stb_truetype.h
```

The generated fixture in `tests/fixtures/single_point.ttf` exercises this
contour without requiring Microsoft fonts on CI. Its generator documents the
tables and can recreate the fixture using only Python's standard library.
