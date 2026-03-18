"""Simple orbit camera for 3D plots — pure numpy, no engine dependencies."""

from __future__ import annotations

import math
import numpy as np


def perspective(fov_y: float, aspect: float, near: float, far: float) -> np.ndarray:
    """Create perspective projection matrix (column-major, OpenGL convention)."""
    f = 1.0 / math.tan(fov_y / 2.0)
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = f / aspect
    m[1, 1] = f
    m[2, 2] = (far + near) / (near - far)
    m[2, 3] = (2.0 * far * near) / (near - far)
    m[3, 2] = -1.0
    return m


def look_at(eye: np.ndarray, target: np.ndarray, up: np.ndarray) -> np.ndarray:
    """Create view matrix looking from eye to target."""
    f = target - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    s = s / (np.linalg.norm(s) + 1e-12)
    u = np.cross(s, f)

    m = np.eye(4, dtype=np.float32)
    m[0, 0:3] = s
    m[1, 0:3] = u
    m[2, 0:3] = -f
    m[0, 3] = -np.dot(s, eye)
    m[1, 3] = -np.dot(u, eye)
    m[2, 3] = np.dot(f, eye)
    return m


class OrbitCamera:
    """Orbit camera around a target point.

    Coordinate system: Y-forward (depth), Z-up.
    """

    def __init__(self):
        self.target = np.array([0.0, 0.0, 0.0], dtype=np.float32)
        self.distance: float = 5.0
        self.azimuth: float = math.radians(45.0)    # horizontal angle
        self.elevation: float = math.radians(30.0)   # vertical angle
        self.fov_y: float = math.radians(45.0)
        self.near: float = 0.01
        self.far: float = 1000.0

        # Limits
        self.min_distance: float = 0.01
        self.max_distance: float = 10000.0
        self.min_elevation: float = math.radians(-89.0)
        self.max_elevation: float = math.radians(89.0)

    @property
    def eye(self) -> np.ndarray:
        """Camera position in world space."""
        ce = math.cos(self.elevation)
        se = math.sin(self.elevation)
        ca = math.cos(self.azimuth)
        sa = math.sin(self.azimuth)
        return self.target + self.distance * np.array([
            ce * sa,
            -ce * ca,
            se,
        ], dtype=np.float32)

    def view_matrix(self) -> np.ndarray:
        up = np.array([0.0, 0.0, 1.0], dtype=np.float32)
        return look_at(self.eye, self.target, up)

    def projection_matrix(self, aspect: float) -> np.ndarray:
        return perspective(self.fov_y, aspect, self.near, self.far)

    def mvp(self, aspect: float) -> np.ndarray:
        """Model-View-Projection matrix (model = identity)."""
        return self.projection_matrix(aspect) @ self.view_matrix()

    def orbit(self, d_azimuth: float, d_elevation: float):
        """Rotate around target."""
        self.azimuth += d_azimuth
        self.elevation = max(self.min_elevation,
                             min(self.elevation + d_elevation, self.max_elevation))

    def zoom(self, factor: float):
        """Zoom in/out (factor < 1 = closer)."""
        self.distance = max(self.min_distance,
                            min(self.distance * factor, self.max_distance))

    def pan(self, dx: float, dy: float):
        """Pan target in screen-aligned directions."""
        ce = math.cos(self.elevation)
        se = math.sin(self.elevation)
        ca = math.cos(self.azimuth)
        sa = math.sin(self.azimuth)

        # Right vector (screen X)
        right = np.array([ca, sa, 0.0], dtype=np.float32)
        # Up vector (screen Y) — perpendicular to view direction and right
        forward = np.array([-ce * sa, ce * ca, -se], dtype=np.float32)
        up = np.cross(right, forward)
        up = up / (np.linalg.norm(up) + 1e-12)

        scale = self.distance * 0.002
        self.target += right * dx * scale + up * dy * scale

    def fit_bounds(self, bounds_min: np.ndarray, bounds_max: np.ndarray):
        """Adjust camera to fit bounding box."""
        center = (bounds_min + bounds_max) * 0.5
        size = np.linalg.norm(bounds_max - bounds_min)
        self.target = center.astype(np.float32)
        self.distance = max(size * 1.2, self.min_distance)
