# SVG extreme-zoom edge teeth — 2026-08-19

Status: **fixed and host-verified in `b6630d6`.** Owner glass/export check on
2026-08-19 found the SVG and PNG effectively identical at normal viewing size,
but exposed tiny outward teeth along some curved Stroke edges at extreme SVG
magnification. PNG anti-aliasing masked them at its native resolution.

This is not specific to Inkscape. Rasterizing the captured SVG independently
with Inkscape and librsvg at 1472x1792 produced an RMSE of only
`8.43473 / 65535` (`0.000128706` normalized). The visible SVG outline is
therefore the exported vector geometry as interpreted consistently by two
renderers.

The confirmed cause was the shared ribbon authority's deliberate `0.75 px`
tangent overlap between each pair of adjacent quadratic subspans. It prevents
cracks in the fixed-grid screen/PNG rasterizer, but each SVG convex retained two
displaced seam corners that projected beyond the intended outline.

SVG pen and eraser streams now use exact shared section boundaries. Raster
streams keep the overlap, preserving their crack prevention and performance.
The focused regression failed before the fix because adjacent SVG spans shared
zero vertices; it now proves they share exactly two. The broader raster oracle
finds only 79/2,048 antialiased edge pixels changed and no fully covered
black/white disagreements. Host Debug (31/31), ASan (13/13), and Release
(31/31) suites pass; the SVG authority suite passes 87 cases / 25,718
assertions.

External artifacts retained on the owner's Desktop:

- extreme-zoom screenshot, SHA-256
  `80cacbf74ffdb38417b72b9c304ee80acf10f62c6f9b690f35beaf355b6af99c`;
- native PNG, SHA-256
  `1fd3ff372f07c66a7f3cb32691af52a6bd128651005ea336b6d3f54d41b32c20`;
- SVG, SHA-256
  `d3ad7f8f59fdfb517fa92df80f5f14c2b42afe928cbc9b922ccbb8b0fef28ca0`.

Final acceptance is an owner re-export and extreme-zoom outline check on the
fixed firmware.
