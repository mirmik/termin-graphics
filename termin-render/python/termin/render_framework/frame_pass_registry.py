"""Python frame pass class registry."""

from __future__ import annotations

import importlib

from tcbase import log


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
                cls = getattr(module, class_name)
                self.register(class_name, cls)
                registered.append(class_name)
            except AttributeError:
                log.error(
                    f"Builtin frame pass module {module_name} does not expose {class_name}",
                    exc_info=True,
                )
            except Exception as e:
                log.warning(f"Failed to register frame pass {class_name} from {module_name}: {e}")

        return registered
