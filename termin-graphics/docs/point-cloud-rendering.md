# Point-cloud rendering

`tgfx::PointCloud` stores point data in a persistent GPU instance buffer, and
`tgfx::PointCloudRenderer` draws it as instanced camera-facing quads. This is
deliberately not implemented with hardware point primitives: quad expansion
keeps point diameter and shape consistent across OpenGL, Vulkan, D3D11, and
WebGPU.

Each point has a world-space position, a linear RGBA color, and a size scale.
`PointCloudStyle::size_px` is the common diameter in screen pixels; the final
diameter is `size_px * point.size_scale`. Circle and square shapes are
available. Depth test, depth write, and a draw-wide linear tint are explicit
style state. Points are currently rendered as opaque geometry; this keeps the
same pipeline contract on every supported backend, including WebGPU.

## Python

Upload only when generated data changes, then reuse the same cloud each frame:

```python
import numpy as np
import tgfx
from tcbase._geom_native import SrgbColor

positions = np.ascontiguousarray(model_points, dtype=np.float32)  # Nx3

cloud = tgfx.PointCloud()
cloud.upload(ctx, positions, SrgbColor(0.85, 0.9, 1.0, 1.0))

style = tgfx.PointCloudStyle()
style.size_px = 4.0
style.shape = tgfx.PointCloudShape.Circle

params = tgfx.PointCloudDrawParams()
params.view_projection = tuple(view_projection_column_major)

# Inside an open RenderContext2 pass with the desired viewport:
renderer = tgfx.PointCloudRenderer()
renderer.draw(ctx, cloud, style, params)
```

For individual colors, use `upload_srgb(ctx, positions, colors, sizes=None)` or
`upload_linear(...)`. `colors` is contiguous float32 `Nx3` or `Nx4`; optional
`sizes` is contiguous float32 `N`. Authored/display colors belong in
`upload_srgb`; already renderer-linear/HDR values belong in `upload_linear`.

The cloud reports `point_count`, `bounds_min`, and `bounds_max`, so callers can
frame the camera without re-reading model data. Call `cloud.release(ctx)` and
`renderer.release(ctx)` while the owning graphics device is alive.

## C++

Fill a `std::vector<tgfx::PointCloudPoint>`, call `cloud.upload(ctx, points)`,
and draw it with `PointCloudStyle` and `PointCloudDrawParams`. The point value
is a standard-layout, trivially-copyable 32-byte GPU contract. Its color is
linear; convert authored sRGB through `termin::srgb_to_linear` at the input
boundary.
