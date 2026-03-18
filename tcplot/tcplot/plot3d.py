"""3D Plot widget for tcgui — renders via tgfx shaders + tmesh GPU meshes."""

from __future__ import annotations

import numpy as np

from tcgui.widgets.widget import Widget
from tcgui.widgets.events import MouseEvent, MouseWheelEvent
from tcbase import MouseButton
from tgfx import OpenGLGraphicsBackend, TcShader

from tcplot.camera3d import OrbitCamera
from tcplot.data import PlotData
from tcplot.axes import nice_ticks, format_tick
from tgfx.text3d import Text3DRenderer
from tcplot import styles

# 3D shader with jet colormap in fragment shader
_VERT_SRC = """#version 330 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec4 a_color;
uniform mat4 u_mvp;
uniform float u_z_min;
uniform float u_z_max;
uniform int u_use_jet;
out vec4 v_color;
out float v_z_norm;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
    float z_range = u_z_max - u_z_min;
    v_z_norm = (z_range > 0.0) ? (a_position.z - u_z_min) / z_range : 0.5;
}
"""

_FRAG_SRC = """#version 330 core
in vec4 v_color;
in float v_z_norm;
uniform int u_use_jet;
out vec4 frag_color;

vec3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    float r, g, b;
    if (t < 0.125) {
        r = 0.0; g = 0.0; b = 0.5 + t * 4.0;
    } else if (t < 0.375) {
        r = 0.0; g = (t - 0.125) * 4.0; b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) * 4.0; g = 1.0; b = 1.0 - (t - 0.375) * 4.0;
    } else if (t < 0.875) {
        r = 1.0; g = 1.0 - (t - 0.625) * 4.0; b = 0.0;
    } else {
        r = 1.0 - (t - 0.875) * 4.0; g = 0.0; b = 0.0;
    }
    return vec3(r, g, b);
}

void main() {
    if (u_use_jet != 0) {
        frag_color = vec4(jet(v_z_norm), v_color.a);
    } else {
        frag_color = v_color;
    }
}
"""


class Plot3D(Widget):
    """Interactive 3D plot widget.

    Supports:
    - Line plots, scatter plots in 3D
    - Orbit (left drag), pan (middle drag), zoom (scroll)
    - 3D axes with grid
    """

    def __init__(self):
        super().__init__()
        self.data = PlotData()
        self.camera = OrbitCamera()

        # GPU resources (lazy init)
        self._shader: TcShader | None = None
        self._lines_mesh = None    # TcMesh
        self._scatter_mesh = None
        self._grid_mesh = None
        self._surface_meshes: list = []       # solid TcMesh
        self._wireframe_meshes: list = []     # wireframe TcMesh
        self._dirty = True  # rebuild meshes on next render
        self._text3d = Text3DRenderer()

        # Interaction
        self._dragging = False
        self._drag_button = None
        self._drag_start_x = 0.0
        self._drag_start_y = 0.0

        # Style
        self.bg_color = styles.BG_COLOR
        self.show_grid = True
        self.show_wireframe = True
        self.z_scale: float = 1.0  # vertical exaggeration

        # Toolbar buttons
        from tcgui.widgets.button import Button
        from tcgui.widgets.units import px

        self._wire_btn = Button()
        self._wire_btn.text = "W"
        self._wire_btn.preferred_width = px(28)
        self._wire_btn.preferred_height = px(28)
        self._wire_btn.on_click = lambda: setattr(self, 'show_wireframe', not self.show_wireframe)
        self.add_child(self._wire_btn)

        self._marker_btn = Button()
        self._marker_btn.text = "M"
        self._marker_btn.preferred_width = px(28)
        self._marker_btn.preferred_height = px(28)
        self._marker_btn.on_click = self._toggle_marker_mode
        self.add_child(self._marker_btn)

        # Marker state
        self.marker_mode = False
        self._marker_pos: tuple[float, float, float] | None = None  # data coords

    def _toggle_marker_mode(self):
        self.marker_mode = not self.marker_mode
        if not self.marker_mode:
            self._marker_pos = None

    def layout(self, x, y, width, height, viewport_w, viewport_h):
        super().layout(x, y, width, height, viewport_w, viewport_h)
        # Position buttons in top-right corner
        self._wire_btn.layout(x + width - 36, y + 8, 28, 28, viewport_w, viewport_h)
        self._marker_btn.layout(x + width - 68, y + 8, 28, 28, viewport_w, viewport_h)

    # -- Public API --

    def plot(self, x, y, z, *, color=None, thickness=1.5, label=""):
        """Add a 3D line series."""
        s = self.data.add_line(x, y, color=color, thickness=thickness, label=label)
        s.z = np.asarray(z, dtype=np.float64)
        self._dirty = True
        self.camera.fit_bounds(*self._data_bounds_3d())

    def scatter(self, x, y, z, *, color=None, size=4.0, label=""):
        """Add a 3D scatter series."""
        s = self.data.add_scatter(x, y, color=color, size=size, label=label)
        s.z = np.asarray(z, dtype=np.float64)
        self._dirty = True
        self.camera.fit_bounds(*self._data_bounds_3d())

    def surface(self, X, Y, Z, *, color=None, wireframe=False, label=""):
        """Add a 3D surface from meshgrid arrays (rows x cols)."""
        from tcplot.data import SurfaceSeries
        Xa = np.asarray(X, dtype=np.float64)
        Ya = np.asarray(Y, dtype=np.float64)
        Za = np.asarray(Z, dtype=np.float64)
        if color is None:
            color = styles.cycle_color(
                len(self.data.lines) + len(self.data.scatters) + len(self.data.surfaces))
        s = SurfaceSeries(X=Xa, Y=Ya, Z=Za, color=color, wireframe=wireframe, label=label)
        self.data.surfaces.append(s)
        self._dirty = True
        self.camera.fit_bounds(*self._data_bounds_3d())

    def clear(self):
        self.data = PlotData()
        self._dirty = True
        self._release_gpu()

    # -- Data bounds --

    def _data_bounds_3d(self):
        xs, ys, zs = [], [], []
        for s in self.data.lines + self.data.scatters:
            if len(s.x) == 0:
                continue
            xs.extend([s.x.min(), s.x.max()])
            ys.extend([s.y.min(), s.y.max()])
            if s.z is not None and len(s.z) > 0:
                zs.extend([s.z.min(), s.z.max()])
        for s in self.data.surfaces:
            xs.extend([s.X.min(), s.X.max()])
            ys.extend([s.Y.min(), s.Y.max()])
            zs.extend([s.Z.min(), s.Z.max()])
        if not xs:
            return np.array([-1, -1, -1], dtype=np.float32), np.array([1, 1, 1], dtype=np.float32)
        lo = np.array([min(xs), min(ys), min(zs) if zs else 0], dtype=np.float32)
        hi = np.array([max(xs), max(ys), max(zs) if zs else 0], dtype=np.float32)
        # Ensure non-zero extent
        for i in range(3):
            if hi[i] - lo[i] < 1e-6:
                lo[i] -= 0.5
                hi[i] += 0.5
        return lo, hi

    # -- GPU mesh building --

    def _ensure_shader(self):
        if self._shader is None:
            self._shader = TcShader.from_sources(_VERT_SRC, _FRAG_SRC, "", "tcplot3d")
            self._shader.ensure_ready()

    def _rebuild_meshes(self):
        self._release_gpu()

        # Build line mesh: interleaved [x,y,z, r,g,b,a] per vertex
        line_verts = []
        line_indices = []
        idx = 0
        for s in self.data.lines:
            if s.z is None or len(s.x) < 2:
                continue
            c = s.color or styles.AXIS_COLOR
            for i in range(len(s.x)):
                line_verts.extend([s.x[i], s.y[i], s.z[i], c[0], c[1], c[2], c[3]])
            for i in range(len(s.x) - 1):
                line_indices.extend([idx + i, idx + i + 1])
            idx += len(s.x)

        if line_verts:
            self._lines_mesh = self._make_line_mesh(line_verts, line_indices)
            self._lines_mesh.upload_gpu()

        # Build scatter mesh: small cross per point (6 vertices = 3 lines)
        scatter_verts = []
        scatter_indices = []
        idx = 0
        cross_size = 0.02  # will be scaled by data range
        bounds_min, bounds_max = self._data_bounds_3d()
        data_size = np.linalg.norm(bounds_max - bounds_min)
        cs = data_size * 0.008  # cross size relative to data

        for s in self.data.scatters:
            if s.z is None or len(s.x) == 0:
                continue
            c = s.color or styles.AXIS_COLOR
            for i in range(len(s.x)):
                px, py, pz = s.x[i], s.y[i], s.z[i]
                for dx, dy, dz in [(cs,0,0), (0,cs,0), (0,0,cs)]:
                    scatter_verts.extend([px-dx, py-dy, pz-dz, c[0], c[1], c[2], c[3]])
                    scatter_verts.extend([px+dx, py+dy, pz+dz, c[0], c[1], c[2], c[3]])
                    scatter_indices.extend([idx, idx+1])
                    idx += 2

        if scatter_verts:
            self._scatter_mesh = self._make_line_mesh(scatter_verts, scatter_indices)
            self._scatter_mesh.upload_gpu()

        # Build surface meshes
        for surf in self.data.surfaces:
            self._build_surface_mesh(surf)

        # Build grid mesh
        if self.show_grid:
            self._build_grid_mesh(bounds_min, bounds_max)

        self._dirty = False

    def _build_grid_mesh(self, bounds_min, bounds_max):
        grid_verts = []
        grid_indices = []
        idx = 0
        gc = styles.GRID_COLOR
        ac = styles.AXIS_COLOR

        for axis in range(3):  # X, Y, Z
            lo, hi = bounds_min[axis], bounds_max[axis]
            ticks = nice_ticks(lo, hi, 8)
            other_axes = [a for a in range(3) if a != axis]
            a1, a2 = other_axes

            for t in ticks:
                # Line along a1 direction at this tick
                p0 = [0.0, 0.0, 0.0]
                p1 = [0.0, 0.0, 0.0]
                p0[axis] = t
                p1[axis] = t
                p0[a1] = bounds_min[a1]
                p1[a1] = bounds_max[a1]
                p0[a2] = bounds_min[a2]
                p1[a2] = bounds_min[a2]
                grid_verts.extend([*p0, gc[0], gc[1], gc[2], gc[3]])
                grid_verts.extend([*p1, gc[0], gc[1], gc[2], gc[3]])
                grid_indices.extend([idx, idx + 1])
                idx += 2

        # Axis lines through origin (or bounds_min)
        for axis in range(3):
            color = [(1,0,0,1), (0,1,0,1), (0,0,1,1)][axis]  # RGB for XYZ
            p0 = [bounds_min[0], bounds_min[1], bounds_min[2]]
            p1 = list(p0)
            p1[axis] = bounds_max[axis]
            grid_verts.extend([*p0, *color])
            grid_verts.extend([*p1, *color])
            grid_indices.extend([idx, idx + 1])
            idx += 2

        if grid_verts:
            self._grid_mesh = self._make_line_mesh(grid_verts, grid_indices)
            self._grid_mesh.upload_gpu()

    @staticmethod
    def _make_line_mesh(verts_flat, indices_flat):
        """Build a TcMesh with pos+color layout and LINES draw mode."""
        from tmesh import TcMesh, TcVertexLayout, TcAttribType, TcDrawMode
        verts = np.array(verts_flat, dtype=np.float32)
        indices = np.array(indices_flat, dtype=np.uint32)
        layout = TcVertexLayout()
        layout.add("position", 3, TcAttribType.FLOAT32, 0)
        layout.add("color", 4, TcAttribType.FLOAT32, 1)
        return TcMesh.from_interleaved(
            verts, len(verts) // 7, indices, layout,
            draw_mode=TcDrawMode.LINES,
        )

    def _build_surface_mesh(self, surf):
        """Build a triangle mesh for a surface grid."""
        from tmesh import TcMesh, TcVertexLayout, TcAttribType, TcDrawMode
        rows, cols = surf.Z.shape
        alpha = surf.color[3] if surf.color else 1.0

        z_min, z_max = surf.Z.min(), surf.Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0

        # Vertices: position + jet colormap by height
        verts = []
        for j in range(rows):
            for i in range(cols):
                x, y, z = surf.X[j, i], surf.Y[j, i], surf.Z[j, i]
                t = (z - z_min) / z_range
                if surf.color and surf.wireframe:
                    # Wireframe uses fixed color
                    r, g, b = surf.color[0], surf.color[1], surf.color[2]
                else:
                    r, g, b = styles.jet(t)
                verts.extend([x, y, z, r, g, b, alpha])

        # Indices: two triangles per grid cell
        indices = []
        for j in range(rows - 1):
            for i in range(cols - 1):
                v00 = j * cols + i
                v10 = j * cols + (i + 1)
                v01 = (j + 1) * cols + i
                v11 = (j + 1) * cols + (i + 1)
                indices.extend([v00, v10, v01])
                indices.extend([v10, v11, v01])

        verts_arr = np.array(verts, dtype=np.float32)
        indices_arr = np.array(indices, dtype=np.uint32)
        layout = TcVertexLayout()
        layout.add("position", 3, TcAttribType.FLOAT32, 0)
        layout.add("color", 4, TcAttribType.FLOAT32, 1)

        if surf.wireframe:
            # Convert triangles to line edges
            wire_indices = []
            for k in range(0, len(indices), 3):
                a, b, cc = indices[k], indices[k+1], indices[k+2]
                wire_indices.extend([a, b, b, cc, cc, a])
            indices_arr = np.array(wire_indices, dtype=np.uint32)
            tc_mesh = TcMesh.from_interleaved(
                verts_arr, len(verts_arr) // 7, indices_arr, layout,
                draw_mode=TcDrawMode.LINES)
        else:
            tc_mesh = TcMesh.from_interleaved(
                verts_arr, len(verts_arr) // 7, indices_arr, layout,
                draw_mode=TcDrawMode.TRIANGLES)

        tc_mesh.upload_gpu()
        if surf.wireframe:
            self._wireframe_meshes.append(tc_mesh)
        else:
            self._surface_meshes.append(tc_mesh)

    def _update_marker(self, data_pos: tuple[float, float, float]):
        """Update marker position (no mesh rebuild — drawn via immediate mode)."""
        self._marker_pos = data_pos

    def _release_gpu(self):
        for attr in ('_lines_mesh', '_scatter_mesh', '_grid_mesh'):
            mesh = getattr(self, attr, None)
            if mesh is not None:
                mesh.delete_gpu()
                setattr(self, attr, None)
        for mesh in self._surface_meshes:
            mesh.delete_gpu()
        self._surface_meshes.clear()
        for mesh in self._wireframe_meshes:
            mesh.delete_gpu()
        self._wireframe_meshes.clear()

    def _draw_tick_labels_3d(self, aspect, mvp, bounds_min, bounds_max, graphics, renderer):
        """Draw tick value labels as billboard text on axes."""
        graphics.set_depth_test(True)
        graphics.set_blend(True)

        font = renderer.font
        self._text3d.begin(self.camera, aspect, font=font)

        label_color = (0.8, 0.8, 0.8, 1.0)
        data_size = float(np.linalg.norm(bounds_max - bounds_min))
        text_size = data_size * 0.02  # scale labels relative to data

        axis_names = ["X", "Y", "Z"]
        for axis in range(3):
            lo, hi = float(bounds_min[axis]), float(bounds_max[axis])
            ticks = nice_ticks(lo, hi, 6)
            offset = data_size * 0.03  # distance from axis

            for t in ticks:
                label = format_tick(t)
                pos = [float(bounds_min[0]), float(bounds_min[1]),
                       float(bounds_min[2]) * self.z_scale]
                if axis == 2:
                    pos[axis] = t * self.z_scale
                else:
                    pos[axis] = t

                # Offset label away from plot area
                if axis == 0:  # X axis: offset in -Y
                    pos[1] -= offset
                elif axis == 1:  # Y axis: offset in -X
                    pos[0] -= offset
                else:  # Z axis: offset in -X
                    pos[0] -= offset

                self._text3d.draw(label, pos, color=label_color, size=text_size)

        graphics.set_depth_test(True)

    # -- Rendering --

    def render(self, renderer):
        if self.width <= 0 or self.height <= 0:
            return

        # Background
        renderer.draw_rect(self.x, self.y, self.width, self.height, self.bg_color)

        self._ensure_shader()
        if self._dirty:
            self._rebuild_meshes()

        graphics = OpenGLGraphicsBackend.get_instance()

        # Save and setup GL state for 3D
        graphics.set_depth_test(True)
        graphics.set_blend(True)
        graphics.set_cull_face(False)

        # Viewport (scissor to widget area)
        ix = int(self.x)
        iy = int(self.y)
        iw = int(self.width)
        ih = int(self.height)
        renderer.begin_clip(ix, iy, iw, ih)

        # Compute MVP with z_scale
        aspect = self.width / max(self.height, 1)
        mvp = self.camera.mvp(aspect)
        if self.z_scale != 1.0:
            model = np.eye(4, dtype=np.float32)
            model[2, 2] = self.z_scale
            mvp = mvp @ model

        self._shader.use()
        self._shader.set_uniform_mat4("u_mvp", mvp.astype(np.float32), True)

        # Z range for jet colormap
        bounds_min, bounds_max = self._data_bounds_3d()
        self._shader.set_uniform_float("u_z_min", float(bounds_min[2]))
        self._shader.set_uniform_float("u_z_max", float(bounds_max[2]))

        # Draw grid first (behind data, no jet)
        self._shader.set_uniform_int("u_use_jet", 0)
        if self._grid_mesh:
            self._grid_mesh.draw_gpu()

        # Draw opaque surfaces (with jet)
        self._shader.set_uniform_int("u_use_jet", 1)
        graphics.set_blend(False)
        for mesh in self._surface_meshes:
            mesh.draw_gpu()

        # Draw wireframe on top without depth test (no jet)
        self._shader.set_uniform_int("u_use_jet", 0)
        if self.show_wireframe:
            graphics.set_depth_test(False)
            graphics.set_blend(True)
            for mesh in self._wireframe_meshes:
                mesh.draw_gpu()
            graphics.set_depth_test(True)

        # Draw lines and scatter on top (no jet)
        if self._lines_mesh:
            self._lines_mesh.draw_gpu()
        if self._scatter_mesh:
            self._scatter_mesh.draw_gpu()

        # Draw marker via immediate mode (no mesh allocation)
        if self._marker_pos and self.marker_mode:
            self._shader.use()
            self._shader.set_uniform_mat4("u_mvp", mvp.astype(np.float32), True)
            self._shader.set_uniform_int("u_use_jet", 0)
            graphics.set_depth_test(False)

            x, y, z = self._marker_pos
            data_size = float(np.linalg.norm(bounds_max - bounds_min))
            cs = data_size * 0.015
            c = [1.0, 1.0, 0.0, 1.0]
            verts = []
            for dx, dy, dz in [(cs,0,0), (0,cs,0), (0,0,cs)]:
                verts.extend([x-dx, y-dy, z-dz, *c])
                verts.extend([x+dx, y+dy, z+dz, *c])
            graphics.draw_immediate_lines(np.array(verts, dtype=np.float32))

            graphics.set_depth_test(True)

        # 3D tick labels (billboard text)
        self._draw_tick_labels_3d(aspect, mvp, bounds_min, bounds_max, graphics, renderer)

        # Marker value label (billboard text, always on top)
        if self._marker_pos and self.marker_mode:
            graphics.set_depth_test(False)
            x, y, z = self._marker_pos
            label = f"({x:.3g}, {y:.3g}, {z:.3g})"
            data_size = float(np.linalg.norm(bounds_max - bounds_min))
            self._text3d.begin(self.camera, aspect, font=renderer.font)
            # Pre-apply z_scale to anchor position (text3d MVP has no z_scale)
            pos = [x, y, z * self.z_scale + data_size * 0.04]
            self._text3d.draw(label, pos, color=(1.0, 1.0, 0.0, 1.0),
                              size=data_size * 0.015)
            self._text3d.end()
            graphics.set_depth_test(True)

        renderer.end_clip()

        # Restore 2D state for UI rendering
        graphics.set_depth_test(False)

        # Title overlay
        if self.data.title:
            renderer.draw_text_centered(
                self.x + self.width / 2,
                self.y + 16,
                self.data.title,
                styles.LABEL_COLOR,
                14.0,
            )

        # Render child widgets (wireframe button)
        for child in self.children:
            if child.visible:
                child.render(renderer)

    # -- Picking --

    def pick(self, mx: float, my: float) -> tuple[float, float, float, float] | None:
        """Find nearest data point to mouse pixel position.

        Returns (x, y, z, screen_distance) or None if no data.
        """
        aspect = self.width / max(self.height, 1)
        mvp = self.camera.mvp(aspect)
        if self.z_scale != 1.0:
            model = np.eye(4, dtype=np.float32)
            model[2, 2] = self.z_scale
            mvp = mvp @ model

        # Collect all data points as numpy arrays
        arrays = []
        for s in self.data.lines:
            if s.z is not None and len(s.x) > 0:
                arrays.append(np.column_stack([s.x, s.y, s.z]))
        for s in self.data.scatters:
            if s.z is not None and len(s.x) > 0:
                arrays.append(np.column_stack([s.x, s.y, s.z]))
        for s in self.data.surfaces:
            arrays.append(np.column_stack([
                s.X.ravel(), s.Y.ravel(), s.Z.ravel()]))

        if not arrays:
            return None

        all_pts = np.vstack(arrays).astype(np.float32)  # (n, 3)
        n = len(all_pts)

        # Homogeneous coords
        pts = np.ones((n, 4), dtype=np.float32)
        pts[:, :3] = all_pts

        clip = (mvp @ pts.T).T  # (n, 4)
        w = clip[:, 3]

        # Skip points behind camera
        valid = w > 0.001
        ndc_x = np.where(valid, clip[:, 0] / w, np.inf)
        ndc_y = np.where(valid, clip[:, 1] / w, np.inf)

        # NDC to pixel (widget-relative)
        px = self.x + (ndc_x * 0.5 + 0.5) * self.width
        py = self.y + (-ndc_y * 0.5 + 0.5) * self.height

        # Distance to mouse
        dist = np.sqrt((px - mx) ** 2 + (py - my) ** 2)
        idx = np.argmin(dist)
        min_dist = dist[idx]

        if min_dist > 50:  # threshold in pixels
            return None

        return (float(all_pts[idx, 0]), float(all_pts[idx, 1]),
                float(all_pts[idx, 2]), float(min_dist))

    # -- Interaction --

    def on_mouse_down(self, event: MouseEvent) -> bool:
        if event.button == MouseButton.RIGHT:
            result = self.pick(event.x, event.y)
            if result:
                x, y, z, d = result
                print(f"[Pick] x={x:.4f}  y={y:.4f}  z={z:.4f}  (dist={d:.1f}px)")
            else:
                print("[Pick] no point nearby")
            return True
        if event.button in (MouseButton.LEFT, MouseButton.MIDDLE):
            self._dragging = True
            self._drag_button = event.button
            self._drag_start_x = event.x
            self._drag_start_y = event.y
            return True
        return False

    def on_mouse_move(self, event: MouseEvent):
        # Update marker on hover
        if self.marker_mode and not self._dragging:
            result = self.pick(event.x, event.y)
            if result:
                self._update_marker((result[0], result[1], result[2]))
            else:
                self._marker_pos = None

        if not self._dragging:
            return
        dx = event.x - self._drag_start_x
        dy = event.y - self._drag_start_y
        self._drag_start_x = event.x
        self._drag_start_y = event.y

        if self._drag_button == MouseButton.LEFT:
            self.camera.orbit(-dx * 0.005, dy * 0.005)
        elif self._drag_button == MouseButton.MIDDLE:
            self.camera.pan(-dx, dy)

    def on_mouse_up(self, event: MouseEvent):
        self._dragging = False
        self._drag_button = None

    def on_mouse_wheel(self, event: MouseWheelEvent) -> bool:
        factor = 0.9 if event.dy > 0 else 1.0 / 0.9
        self.camera.zoom(factor)
        return True

    def compute_size(self, viewport_w: float, viewport_h: float) -> tuple[float, float]:
        w = self.preferred_width.to_pixels(viewport_w) if self.preferred_width else viewport_w
        h = self.preferred_height.to_pixels(viewport_h) if self.preferred_height else viewport_h
        return (w, h)
