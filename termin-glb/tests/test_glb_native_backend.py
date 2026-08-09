from termin.glb import _glb_native


def test_native_backend_reports_pinned_cgltf_revision():
    info = _glb_native.backend_info()

    assert info == {
        "name": "cgltf",
        "cgltf_version": "1.15",
        "cgltf_revision": "85cd62382dfea638278962690cf515023f33ed00",
    }
    assert _glb_native.error_code_name(_glb_native.NativeErrorCode.UNSUPPORTED) == "unsupported"

