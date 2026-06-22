"""Python frame pass class registry."""

from __future__ import annotations

import importlib

from tcbase import log
from termin.render_framework.python_pass import PythonFramePass
from termin.scene.class_scanner import scan_for_subclasses


class FramePassRegistry:
    """Registry of Python frame pass classes by class name."""

    def __init__(self) -> None:
        self.classes: dict[str, type] = {}

    def register(self, name: str, cls: type) -> None:
        self.classes[name] = cls
        try:
            from termin_modules.module_context import record_frame_pass
        except ModuleNotFoundError as exc:
            if exc.name not in ("termin_modules", "termin_modules.module_context"):
                log.error("Failed to load module ownership context", exc_info=True)
            return
        except Exception:
            log.error("Failed to load module ownership context", exc_info=True)
            return

        try:
            record_frame_pass(name)
        except Exception:
            log.error(f"Failed to record module ownership for frame pass class {name}", exc_info=True)

    def unregister(self, name: str) -> None:
        if name in self.classes:
            del self.classes[name]

    def get(self, name: str) -> type | None:
        return self.classes.get(name)

    def list_names(self) -> list[str]:
        return sorted(self.classes.keys())

    def register_builtins(self, specs: list[tuple[str, str]]) -> list[str]:
        """Register frame pass classes from (module, class) specs."""
        registered: list[str] = []
        for module_name, class_name in specs:
            if class_name in self.classes:
                registered.append(class_name)
                continue

            try:
                module = importlib.import_module(module_name)
                cls = getattr(module, class_name, None)
                if cls is not None:
                    self.register(class_name, cls)
                    registered.append(class_name)
            except Exception as e:
                log.warning(f"Failed to register frame pass {class_name} from {module_name}: {e}")

        return registered

    def scan(self, paths: list[str]) -> list[str]:
        """Scan directories, modules or files and register PythonFramePass subclasses."""
        return scan_for_subclasses(paths, PythonFramePass, self.classes, "_dynamic_frame_passes_")
