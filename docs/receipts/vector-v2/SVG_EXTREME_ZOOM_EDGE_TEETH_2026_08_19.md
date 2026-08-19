# SVG extreme-zoom edge teeth — 2026-08-19

Status: **accepted post-release visual debt.** Owner glass/export check on
2026-08-19 found the SVG and PNG effectively identical at normal viewing size.
At extreme SVG magnification, tiny outward teeth are visible along some curved
Stroke edges. PNG anti-aliasing masks them at its native resolution.

This is not specific to Inkscape. Rasterizing the captured SVG independently
with Inkscape and librsvg at 1472x1792 produced an RMSE of only
`8.43473 / 65535` (`0.000128706` normalized). The visible SVG outline is
therefore the exported vector geometry as interpreted consistently by two
renderers. This receipt classifies the observation; it does not claim a root
cause or a fix.

External artifacts retained on the owner's Desktop:

- extreme-zoom screenshot, SHA-256
  `80cacbf74ffdb38417b72b9c304ee80acf10f62c6f9b690f35beaf355b6af99c`;
- native PNG, SHA-256
  `1fd3ff372f07c66a7f3cb32691af52a6bd128651005ea336b6d3f54d41b32c20`;
- SVG, SHA-256
  `d3ad7f8f59fdfb517fa92df80f5f14c2b42afe928cbc9b922ccbb8b0fef28ca0`.

Post-release acceptance should use a magnified outline oracle that detects
outward excursions at adjacent ribbon primitives, while retaining SVG/PNG
shape agreement at normal scale and the current renderer/export geometry
authority contract.

