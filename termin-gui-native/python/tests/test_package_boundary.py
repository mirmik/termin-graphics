import ast
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[2]


def _install_requires() -> set[str]:
    setup_path = PACKAGE_ROOT / "setup.py"
    tree = ast.parse(setup_path.read_text(encoding="utf-8"), filename=str(setup_path))
    setup_call = next(
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "setup"
    )
    requirements_node = next(
        keyword.value
        for keyword in setup_call.keywords
        if keyword.arg == "install_requires"
    )
    assert isinstance(requirements_node, (ast.List, ast.Tuple))
    return {
        item.value
        for item in requirements_node.elts
        if isinstance(item, ast.Constant) and isinstance(item.value, str)
    }


def test_gui_native_declares_visual_scene_runtime_dependency() -> None:
    assert "termin-visual-scene" in _install_requires()
