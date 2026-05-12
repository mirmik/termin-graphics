from __future__ import annotations

from typing import Any, Callable

from termin.render_framework._render_framework_native import (
    TcPass,
    TcPassRef,
    tc_pass_registry_has,
    tc_pass_registry_register_python,
)


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
        self.enabled = True
        self.passthrough = False
        self.viewport_name = viewport_name

        self.debug_internal_symbol: str | None = None
        self._debugger_window: Any = None
        self._depth_capture_callback: Callable[[Any], None] | None = None
        self._depth_error_callback: Callable[[str], None] | None = None

        self._tc_pass_handle: TcPass = TcPass(self, self.__class__.__name__)
        self.pass_name = pass_name

    @property
    def pass_name(self) -> str:
        return self._pass_name

    @pass_name.setter
    def pass_name(self, value: str) -> None:
        self._pass_name = value
        if self._tc_pass_handle is not None:
            self._tc_pass_handle.pass_name = value

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

        if not tc_pass_registry_has(cls.__name__):
            tc_pass_registry_register_python(cls.__name__, cls)

        try:
            from termin._native.inspect import InspectRegistry
            registry = InspectRegistry.instance()

            own_fields = cls.__dict__.get("inspect_fields", {})
            if own_fields:
                registry.register_python_fields(cls.__name__, own_fields)

            metadata = registry.get_type_metadata(cls.__name__)
            if not isinstance(metadata, dict):
                metadata = {}
            metadata["graph"] = _collect_graph_socket_metadata(cls)
            registry.set_type_metadata(cls.__name__, metadata)

            parent_name = None
            for klass in cls.__mro__[1:]:
                if klass.__name__ in ("PythonFramePass", "FramePass", "RenderFramePass"):
                    parent_name = "PythonFramePass"
                    break
                if "inspect_fields" in klass.__dict__:
                    parent_name = klass.__name__
                    break

            if parent_name:
                registry.set_type_parent(cls.__name__, parent_name)
        except ImportError as e:
            from tcbase import log
            log.debug(f"[PythonFramePass] InspectRegistry not available for '{cls.__name__}': {e}")

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
        from termin._native.inspect import InspectRegistry
        return InspectRegistry.instance().serialize_all(self)

    def deserialize_data(self, data: dict) -> None:
        if not data:
            return
        from termin._native.inspect import InspectRegistry
        InspectRegistry.instance().deserialize_all(self, data)

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

__all__ = [
    "PythonFramePass",
    "FramePass",
    "RenderFramePass",
    "deserialize_pass",
]
