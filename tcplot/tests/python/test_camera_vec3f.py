from tcbase._geom_native import Vec3f
from tcplot._tcplot_native import OrbitCamera


def test_orbit_camera_vec3f_api():
    camera = OrbitCamera()

    camera.target = Vec3f(1.0, 2.0, 3.0)
    assert isinstance(camera.target, Vec3f)
    assert camera.target.approx_eq(Vec3f(1.0, 2.0, 3.0))

    camera.fit_bounds(Vec3f(-1.0, -1.0, -1.0), Vec3f(1.0, 1.0, 1.0))

    assert isinstance(camera.target, Vec3f)
    assert isinstance(camera.eye, Vec3f)
