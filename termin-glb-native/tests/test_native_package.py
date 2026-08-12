from importlib.metadata import requires
from pathlib import Path

from packaging.requirements import Requirement

from termin.glb.native import NativeGLBDocument


_BOX_GLB = (
    Path(__file__).parents[2]
    / "termin-thirdparty"
    / "cgltf"
    / "fuzz"
    / "data"
    / "Box.glb"
)


def test_native_distribution_has_only_native_mesh_dependencies() -> None:
    dependencies = {
        Requirement(value).name
        for value in (requires("termin-glb-native") or ())
    }

    assert dependencies == {"tmesh", "termin-nanobind"}
    assert dependencies.isdisjoint(
        {"termin-assets", "termin-default-assets", "tcgui"}
    )


def test_native_distribution_builds_box_mesh() -> None:
    document = NativeGLBDocument(_BOX_GLB)

    mesh = document.build_mesh(
        0,
        "termin-glb-native-box-smoke",
        convert_to_z_up=False,
    )

    assert mesh.is_valid
    assert document.meshes[0].vertex_count == 24
    assert document.meshes[0].index_count == 36
