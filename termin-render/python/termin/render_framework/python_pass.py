from __future__ import annotations

import atexit
from typing import Any, Callable

from termin.render_framework._render_framework_native import (
    TcPass,
    TcPassRef,
    tc_pass_registry_is_native,
    tc_pass_registry_register_python,
    tc_pass_registry_unregister_python,
)


_registered_python_pass_types: dict[str, str] = {}
_python_pass_registrations: dict[str, tuple[type, str]] = {}


def _log_cleanup_error(message: str) -> None:
    from tcbase import log

    log.error(message)


def _register_python_pass_type(
    type_name: str,
    cls: type,
    *,
    owner: str,
    parent_name: str | None = None,
    inspect_fields: dict | None = None,
    metadata: dict | None = None,
) -> bool:
    if tc_pass_registry_is_native(type_name):
        return False
    if not tc_pass_registry_register_python(
        type_name,
        cls,
        owner,
        parent_name,
        inspect_fields or {},
        metadata or {},
    ):
        return False
    _registered_python_pass_types[type_name] = owner
    _python_pass_registrations[type_name] = (cls, owner)
    return True


def shutdown_python_passes() -> None:
    """Unregister Python-authored pass classes from the process registry."""
    for type_name in list(_registered_python_pass_types):
        try:
            tc_pass_registry_unregister_python(type_name)
        except Exception as exc:
            _log_cleanup_error(f"[PythonFramePass] failed to unregister pass type '{type_name}': {exc}")
    _registered_python_pass_types.clear()


def unregister_python_pass_owner(owner: str) -> None:
    """Forget loaded pass definitions owned by an unloaded project module."""
    for type_name, (_cls, registered_owner) in tuple(
        _python_pass_registrations.items()
    ):
        if registered_owner == owner:
            _python_pass_registrations.pop(type_name, None)
            _registered_python_pass_types.pop(type_name, None)


def list_python_pass_owner(owner: str) -> list[str]:
    return sorted(
        type_name
        for type_name, (_cls, registered_owner) in _python_pass_registrations.items()
        if registered_owner == owner
    )


atexit.register(shutdown_python_passes)


def _collect_graph_socket_metadata(cls: type) -> dict:
    inputs: list = []
    outputs: list = []
    inplace_pairs: list = []

    for klass in reversed(cls.__mro__):
        if klass is object:
            continue
        class_inputs = klass.__dict__.get("node_inputs")
        if class_inputs is not None:
            inputs = list(class_inputs)
        class_outputs = klass.__dict__.get("node_outputs")
        if class_outputs is not None:
            outputs = list(class_outputs)
        class_pairs = klass.__dict__.get("node_inplace_pairs")
        if class_pairs is not None:
            inplace_pairs = list(class_pairs)

    return {
        "node_inputs": inputs,
        "node_outputs": outputs,
        "node_inplace_pairs": inplace_pairs,
    }


def _inspect_registry():
    from termin.inspect import InspectRegistry

    return InspectRegistry.instance()


def _register_python_pass_class(cls: type, owner: str | None = None) -> None:
    if owner is None:
        try:
            from termin_modules.module_context import owner_for_python_module
        except ModuleNotFoundError as exc:
            if exc.name not in ("termin_modules", "termin_modules.module_context"):
                from tcbase import log

                log.error("Failed to load module ownership context", exc_info=True)
            owner = "termin-render-python"
        except Exception:
            from tcbase import log

            log.error("Failed to load module ownership context", exc_info=True)
            owner = "termin-render-python"
        else:
            owner = owner_for_python_module(cls.__module__) or "termin-render-python"
    parent_name = None
    for klass in cls.__mro__[1:]:
        if klass.__name__ in ("PythonFramePass", "FramePass", "RenderFramePass"):
            parent_name = "PythonFramePass"
            break
        if "inspect_fields" in klass.__dict__:
            parent_name = klass.__name__
            break

    registered_python = _register_python_pass_type(
        cls.__name__,
        cls,
        owner=owner,
        parent_name=parent_name,
        inspect_fields=cls.__dict__.get("inspect_fields", {}),
        metadata={"graph": _collect_graph_socket_metadata(cls)},
    )
    if not registered_python:
        _log_cleanup_error(
            f"[PythonFramePass] registration rejected for '{cls.__name__}'"
        )


class PythonFramePass:
    """
    Base class for Python-authored render pipeline passes.

    PythonFramePass owns a native TcPass wrapper, registers subclasses in the
    native pass registry, and exposes the same scheduling/render hooks that the
    C framegraph and render engine consume through tc_pass callbacks.
    """

    inspect_fields: dict = {}
    category: str = "Other"
    node_inputs: list = []
    node_outputs: list = []
    node_inplace_pairs: list = []

    def __init__(self, pass_name: str = "PythonFramePass", viewport_name: str | None = None):
        self._pass_name = pass_name
        self._tc_pass_handle: TcPass | None = None
        self._enabled = True
        self._passthrough = False
        self._viewport_name = viewport_name

        self.debug_internal_symbol: str | None = None
        self._debugger_window: Any = None
        self._depth_capture_callback: Callable[[Any], None] | None = None
        self._depth_error_callback: Callable[[str], None] | None = None

        self._tc_pass_handle: TcPass = TcPass(self, self.__class__.__name__)
        self.pass_name = pass_name
        self.enabled = True
        self.passthrough = False
        self.viewport_name = viewport_name

    @property
    def pass_name(self) -> str:
        return self._pass_name

    @pass_name.setter
    def pass_name(self, value: str) -> None:
        self._pass_name = value
        if self._tc_pass_handle is not None:
            self._tc_pass_handle.pass_name = value

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool) -> None:
        self._enabled = value
        if self._tc_pass_handle is not None:
            self._tc_pass_handle.enabled = value

    @property
    def passthrough(self) -> bool:
        return self._passthrough

    @passthrough.setter
    def passthrough(self, value: bool) -> None:
        self._passthrough = value
        if self._tc_pass_handle is not None:
            self._tc_pass_handle.passthrough = value

    @property
    def viewport_name(self) -> str | None:
        return self._viewport_name

    @viewport_name.setter
    def viewport_name(self, value: str | None) -> None:
        self._viewport_name = value
        if self._tc_pass_handle is not None:
            self._tc_pass_handle.ref().set_viewport_name(value or "")

    @property
    def _tc_pass(self) -> TcPassRef:
        return self._tc_pass_handle.ref()

    @property
    def reads(self) -> set[str]:
        return self.compute_reads()

    @property
    def writes(self) -> set[str]:
        return self.compute_writes()

    def compute_reads(self) -> set[str]:
        return set()

    def compute_writes(self) -> set[str]:
        return set()

    def execute(self, ctx) -> None:
        raise NotImplementedError

    def required_resources(self) -> set[str]:
        return set(self.reads) | set(self.writes)

    def get_resource_specs(self) -> list:
        return []

    def destroy(self) -> None:
        pass

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)

        if cls.__name__ in ("PythonFramePass", "FramePass", "RenderFramePass"):
            return

        _register_python_pass_class(cls)

    def __repr__(self) -> str:
        return f"PythonFramePass({self.pass_name!r})"

    def get_inplace_aliases(self) -> list[tuple[str, str]]:
        return []

    @property
    def inplace(self) -> bool:
        return len(self.get_inplace_aliases()) > 0

    def get_internal_symbols(self) -> list[str]:
        return []

    def set_debug_internal_point(self, symbol: str | None) -> None:
        self.debug_internal_symbol = symbol

    def get_debug_internal_point(self) -> str | None:
        return self.debug_internal_symbol

    def set_debugger_window(
        self,
        window,
        depth_callback: Callable[[Any], None] | None = None,
        depth_error_callback: Callable[[str], None] | None = None,
    ) -> None:
        self._debugger_window = window
        self._depth_capture_callback = depth_callback
        self._depth_error_callback = depth_error_callback

    def get_debugger_window(self):
        return self._debugger_window

    def serialize_data(self) -> dict:
        return _inspect_registry().serialize_all(self)

    def deserialize_data(self, data: dict) -> None:
        if not data:
            return
        _inspect_registry().deserialize_all(self, data)

    def serialize(self) -> dict:
        result = {
            "type": self.__class__.__name__,
            "pass_name": self.pass_name,
            "enabled": self.enabled,
            "passthrough": self.passthrough,
            "data": self.serialize_data(),
        }
        if self.viewport_name is not None:
            result["viewport_name"] = self.viewport_name
        return result


def deserialize_pass(data: dict, resource_manager=None):
    pass_type = data.get("type")
    if pass_type is None:
        raise ValueError("Missing 'type' in pass data")
    if resource_manager is None:
        raise ValueError("deserialize_pass requires resource_manager")

    pass_cls = resource_manager.get_frame_pass(pass_type)
    if pass_cls is None:
        raise ValueError(f"Unknown pass type: {pass_type}")

    custom_ctor = getattr(pass_cls, "_deserialize_instance", None)
    if custom_ctor is not None:
        instance = custom_ctor(data, resource_manager)
    else:
        instance = pass_cls()
        instance.pass_name = data.get("pass_name", "unnamed")

    instance.enabled = data.get("enabled", True)
    instance.passthrough = data.get("passthrough", False)
    instance.viewport_name = data.get("viewport_name")
    instance._tc_pass.deserialize_data(data.get("data", {}))

    return instance


FramePass = PythonFramePass
RenderFramePass = PythonFramePass


def register_loaded_python_passes() -> None:
    """Re-register loaded pass classes after a complete runtime shutdown."""
    _register_python_pass_type(
        "PythonFramePass",
        PythonFramePass,
        owner="termin-render-python",
        inspect_fields=PythonFramePass.inspect_fields,
        metadata={"graph": _collect_graph_socket_metadata(PythonFramePass)},
    )
    definitions = list(_python_pass_registrations.values())
    for cls, owner in definitions:
        if cls is not PythonFramePass:
            _register_python_pass_class(cls, owner)


register_loaded_python_passes()

__all__ = [
    "PythonFramePass",
    "FramePass",
    "RenderFramePass",
    "deserialize_pass",
    "register_loaded_python_passes",
]
