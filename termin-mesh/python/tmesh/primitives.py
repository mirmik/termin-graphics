"""Primitive mesh shapes: Cube, Sphere, Cylinder, Cone, Plane, Ring, Torus."""

import hashlib
import numpy as np
from tgfx import Mesh3


def _primitive_uuid(name: str, *args) -> str:
    """Compute UUID for primitive from name and parameters."""
    key = f"{name}:{':'.join(str(a) for a in args)}"
    return hashlib.sha256(key.encode()).hexdigest()[:16]


def CubeMesh(size: float = 1.0, y: float = None, z: float = None) -> Mesh3:
    """Create a simple cube mesh."""
    x = size
    if y is None:
        y = x
    if z is None:
        z = x

    uuid = _primitive_uuid("CubeMesh", x, y, z)

    s_x = x * 0.5
    s_y = y * 0.5
    s_z = z * 0.5
    vertices = np.array(
        [
            [-s_x, -s_y, -s_z],
            [s_x, -s_y, -s_z],
            [s_x, s_y, -s_z],
            [-s_x, s_y, -s_z],
            [-s_x, -s_y, s_z],
            [s_x, -s_y, s_z],
            [s_x, s_y, s_z],
            [-s_x, s_y, s_z],
        ],
        dtype=np.float32,
    )
    triangles = np.array(
        [
            [1, 0, 2],
            [2, 0, 3],
            [4, 5, 7],
            [5, 6, 7],
            [0, 1, 4],
            [1, 5, 4],
            [2, 3, 6],
            [3, 7, 6],
            [3, 0, 4],
            [7, 3, 4],
            [1, 2, 5],
            [2, 6, 5],
        ],
        dtype=np.uint32,
    )
    uvs = np.array([
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
    ], dtype=np.float32)
    mesh = Mesh3(vertices=vertices, triangles=triangles, uvs=uvs, name="Cube", uuid=uuid)
    mesh.compute_normals()
    mesh.compute_tangents()
    return mesh


def TexturedCubeMesh(size: float = 1.0, y: float = None, z: float = None) -> Mesh3:
    """Create a textured cube mesh with proper UV mapping per face."""
    x = size
    if y is None:
        y = x
    if z is None:
        z = x
    uuid = _primitive_uuid("TexturedCubeMesh", x, y, z)

    vertices = np.array([
        [-0.5, -0.5, -0.5],
        [0.5, -0.5, -0.5],
        [0.5, 0.5, -0.5],
        [0.5, 0.5, -0.5],
        [-0.5, 0.5, -0.5],
        [-0.5, -0.5, -0.5],
        [-0.5, -0.5, 0.5],
        [0.5, -0.5, 0.5],
        [0.5, 0.5, 0.5],
        [0.5, 0.5, 0.5],
        [-0.5, 0.5, 0.5],
        [-0.5, -0.5, 0.5],
        [-0.5, 0.5, 0.5],
        [-0.5, 0.5, -0.5],
        [-0.5, -0.5, -0.5],
        [-0.5, -0.5, -0.5],
        [-0.5, -0.5, 0.5],
        [-0.5, 0.5, 0.5],
        [0.5, 0.5, 0.5],
        [0.5, 0.5, -0.5],
        [0.5, -0.5, -0.5],
        [0.5, -0.5, -0.5],
        [0.5, -0.5, 0.5],
        [0.5, 0.5, 0.5],
        [-0.5, -0.5, -0.5],
        [0.5, -0.5, -0.5],
        [0.5, -0.5, 0.5],
        [0.5, -0.5, 0.5],
        [-0.5, -0.5, 0.5],
        [-0.5, -0.5, -0.5],
        [-0.5, 0.5, -0.5],
        [0.5, 0.5, -0.5],
        [0.5, 0.5, 0.5],
        [0.5, 0.5, 0.5],
        [-0.5, 0.5, 0.5],
        [-0.5, 0.5, -0.5],
    ], dtype=np.float32)

    uvs = np.array([
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
        [0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [1.0, 1.0], [0.0, 1.0], [0.0, 0.0],
    ], dtype=np.float32)

    triangles = np.array([
        [1, 0, 2], [4, 3, 5],
        [6, 7, 8], [9, 10, 11],
        [12, 13, 14], [15, 16, 17],
        [19, 18, 20], [22, 21, 23],
        [24, 25, 26], [27, 28, 29],
        [31, 30, 32], [34, 33, 35],
    ], dtype=np.uint32)

    mesh = Mesh3(vertices=vertices, triangles=triangles, uvs=uvs, name="TexturedCube", uuid=uuid)
    mesh.compute_normals()
    mesh.compute_tangents()
    return mesh


def UVSphereMesh(radius: float = 1.0, n_meridians: int = 16, n_parallels: int = 16) -> Mesh3:
    """Create a UV sphere mesh with UV coordinates."""
    uuid = _primitive_uuid("UVSphereMesh", radius, n_meridians, n_parallels)
    rings = n_parallels
    segments = n_meridians

    vertices = []
    uvs = []
    triangles = []
    for r in range(rings + 1):
        theta = r * np.pi / rings
        sin_theta = np.sin(theta)
        cos_theta = np.cos(theta)
        v_coord = r / rings
        for s in range(segments):
            phi = s * 2 * np.pi / segments
            x = radius * sin_theta * np.cos(phi)
            y = radius * sin_theta * np.sin(phi)
            z = radius * cos_theta
            vertices.append([x, y, z])
            uvs.append([s / segments, v_coord])
    for r in range(rings):
        for s in range(segments):
            next_r = r + 1
            next_s = (s + 1) % segments
            triangles.append([r * segments + s, next_r * segments + s, next_r * segments + next_s])
            triangles.append([r * segments + s, next_r * segments + next_s, r * segments + next_s])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        uvs=np.array(uvs, dtype=np.float32),
        name="UVSphere",
        uuid=uuid
    )
    mesh.compute_normals()
    mesh.compute_tangents()
    return mesh


def IcoSphereMesh(radius: float = 1.0, subdivisions: int = 2) -> Mesh3:
    """Create an icosphere mesh."""
    t = (1.0 + np.sqrt(5.0)) / 2.0
    vertices = np.array(
        [
            [-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0],
            [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
            [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1],
        ],
        dtype=np.float32,
    )
    vertices /= np.linalg.norm(vertices[0])
    vertices *= radius
    triangles = np.array(
        [
            [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
            [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
            [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
            [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1],
        ],
        dtype=np.uint32,
    )

    # Subdivide
    for _ in range(subdivisions):
        vertices, triangles = _subdivide_icosphere(vertices, triangles, radius)

    mesh = Mesh3(vertices=vertices, triangles=triangles, name="IcoSphere")
    mesh.compute_normals()
    return mesh


def _subdivide_icosphere(vertices, triangles, radius):
    """Subdivide icosphere triangles."""
    midpoint_cache = {}
    new_triangles = []

    def get_midpoint(v1_idx, v2_idx):
        nonlocal vertices
        key = tuple(sorted((v1_idx, v2_idx)))
        if key in midpoint_cache:
            return midpoint_cache[key]
        v1 = vertices[v1_idx]
        v2 = vertices[v2_idx]
        midpoint = (v1 + v2) / 2.0
        midpoint = midpoint / np.linalg.norm(midpoint) * radius
        new_idx = len(vertices)
        vertices = np.vstack((vertices, midpoint))
        midpoint_cache[key] = new_idx
        return new_idx

    for tri in triangles:
        v0, v1, v2 = tri
        a = get_midpoint(v0, v1)
        b = get_midpoint(v1, v2)
        c = get_midpoint(v2, v0)
        new_triangles.append([v0, a, c])
        new_triangles.append([v1, b, a])
        new_triangles.append([v2, c, b])
        new_triangles.append([a, b, c])

    return vertices.astype(np.float32), np.array(new_triangles, dtype=np.uint32)


def PlaneMesh(width: float = 1.0, height: float = 1.0, segments_w: int = 1, segments_h: int = 1) -> Mesh3:
    """Create a plane mesh in XY plane with UV coordinates."""
    uuid = _primitive_uuid("PlaneMesh", width, height, segments_w, segments_h)
    vertices = []
    uvs = []
    triangles = []
    for h in range(segments_h + 1):
        y = (h / segments_h - 0.5) * height
        v_coord = h / segments_h
        for w in range(segments_w + 1):
            x = (w / segments_w - 0.5) * width
            u_coord = w / segments_w
            vertices.append([x, y, 0.0])
            uvs.append([u_coord, v_coord])
    for h in range(segments_h):
        for w in range(segments_w):
            v0 = h * (segments_w + 1) + w
            v1 = v0 + 1
            v2 = v0 + (segments_w + 1)
            v3 = v2 + 1
            triangles.append([v0, v1, v2])
            triangles.append([v1, v3, v2])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        uvs=np.array(uvs, dtype=np.float32),
        name="Plane",
        uuid=uuid
    )
    mesh.compute_normals()
    mesh.compute_tangents()
    return mesh


def CylinderMesh(radius: float = 1.0, height: float = 1.0, segments: int = 16) -> Mesh3:
    """Create a cylinder mesh."""
    uuid = _primitive_uuid("CylinderMesh", radius, height, segments)
    vertices = []
    triangles = []
    half_height = height * 0.5

    for y_pos in [-half_height, half_height]:
        for s in range(segments):
            theta = s * 2 * np.pi / segments
            x = radius * np.cos(theta)
            z = radius * np.sin(theta)
            vertices.append([x, y_pos, z])

    for s in range(segments):
        next_s = (s + 1) % segments
        bottom0 = s
        bottom1 = next_s
        top0 = s + segments
        top1 = next_s + segments
        triangles.append([bottom0, top0, bottom1])
        triangles.append([bottom1, top0, top1])

    # Add center vertices for caps
    bottom_center_idx = len(vertices)
    vertices.append([0.0, -half_height, 0.0])
    top_center_idx = len(vertices)
    vertices.append([0.0, half_height, 0.0])

    for s in range(segments):
        next_s = (s + 1) % segments
        bottom0 = s
        bottom1 = next_s
        top0 = s + segments
        top1 = next_s + segments
        triangles.append([bottom1, bottom_center_idx, bottom0])
        triangles.append([top0, top_center_idx, top1])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        name="Cylinder",
        uuid=uuid
    )
    mesh.compute_normals()
    return mesh


def ConeMesh(radius: float = 1.0, height: float = 1.0, segments: int = 16) -> Mesh3:
    """Create a cone mesh."""
    uuid = _primitive_uuid("ConeMesh", radius, height, segments)
    vertices = []
    triangles = []
    half_height = height * 0.5

    # Apex
    vertices.append([0.0, half_height, 0.0])

    # Base vertices
    for s in range(segments):
        theta = s * 2 * np.pi / segments
        x = radius * np.cos(theta)
        z = radius * np.sin(theta)
        vertices.append([x, -half_height, z])

    # Side triangles
    for s in range(segments):
        next_s = (s + 1) % segments
        base0 = s + 1
        base1 = next_s + 1
        triangles.append([0, base1, base0])
        triangles.append([base0, base1, len(vertices)])

    # Base center
    vertices.append([0.0, -half_height, 0.0])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        name="Cone",
        uuid=uuid
    )
    mesh.compute_normals()
    return mesh


def TorusMesh(
    major_radius: float = 1.0,
    minor_radius: float = 0.25,
    major_segments: int = 32,
    minor_segments: int = 16,
) -> Mesh3:
    """Create a torus mesh in XZ plane with UV coordinates."""
    if major_segments < 3:
        raise ValueError("TorusMesh: major_segments must be >= 3")
    if minor_segments < 3:
        raise ValueError("TorusMesh: minor_segments must be >= 3")

    uuid = _primitive_uuid("TorusMesh", major_radius, minor_radius, major_segments, minor_segments)
    vertices = []
    uvs = []
    triangles = []

    for i in range(major_segments):
        u = 2.0 * np.pi * i / major_segments
        u_coord = i / major_segments
        cos_u = np.cos(u)
        sin_u = np.sin(u)

        for j in range(minor_segments):
            v = 2.0 * np.pi * j / minor_segments
            v_coord = j / minor_segments
            cos_v = np.cos(v)
            sin_v = np.sin(v)

            x = (major_radius + minor_radius * cos_v) * cos_u
            y = minor_radius * sin_v
            z = (major_radius + minor_radius * cos_v) * sin_u
            vertices.append([x, y, z])
            uvs.append([u_coord, v_coord])

    for i in range(major_segments):
        next_i = (i + 1) % major_segments
        for j in range(minor_segments):
            next_j = (j + 1) % minor_segments
            v0 = i * minor_segments + j
            v1 = next_i * minor_segments + j
            v2 = next_i * minor_segments + next_j
            v3 = i * minor_segments + next_j
            triangles.append([v0, v2, v1])
            triangles.append([v0, v3, v2])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        uvs=np.array(uvs, dtype=np.float32),
        name="Torus",
        uuid=uuid
    )
    mesh.compute_normals()
    mesh.compute_tangents()
    return mesh


def RingMesh(
    radius: float = 1.0,
    thickness: float = 0.05,
    segments: int = 32,
) -> Mesh3:
    """Create a flat ring (annulus) in XZ plane."""
    if segments < 3:
        raise ValueError("RingMesh: segments must be >= 3")

    uuid = _primitive_uuid("RingMesh", radius, thickness, segments)
    inner_radius = max(radius - thickness * 0.5, 1e-4)
    outer_radius = radius + thickness * 0.5

    vertices = []
    triangles = []

    for i in range(segments):
        angle = 2.0 * np.pi * i / segments
        c = np.cos(angle)
        s = np.sin(angle)
        vertices.append([inner_radius * c, 0.0, inner_radius * s])
        vertices.append([outer_radius * c, 0.0, outer_radius * s])

    for i in range(segments):
        i_inner = 2 * i
        i_outer = 2 * i + 1
        next_i = (i + 1) % segments
        n_inner = 2 * next_i
        n_outer = 2 * next_i + 1
        triangles.append([i_inner, n_inner, i_outer])
        triangles.append([i_outer, n_inner, n_outer])

    mesh = Mesh3(
        vertices=np.array(vertices, dtype=np.float32),
        triangles=np.array(triangles, dtype=np.uint32),
        name="Ring",
        uuid=uuid
    )
    mesh.compute_normals()
    return mesh
