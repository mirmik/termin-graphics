"""Versioned declarative document loader for :mod:`termin.gui_native`.

The v1 dialect deliberately covers a small, validated subset of the legacy
``tcgui`` uiscript syntax.  Parsing is toolkit-neutral; native widgets are only
created by :class:`UiScriptLoader` after the complete description validates.
"""

from __future__ import annotations

from dataclasses import dataclass
import logging
from pathlib import Path
from types import MappingProxyType
from typing import Any, Callable, Mapping

import yaml

from termin.gui_native._gui_native import (
    Color,
    Document,
    OverlayAnchor,
    Point,
    Size,
    StyleField,
    StyleOverride,
    WidgetRef,
)


_LOG = logging.getLogger("termin.gui_native.uiscript")
UISCRIPT_VERSION = 1


class UiScriptError(ValueError):
    """A deterministic parser, validation, or materialization failure."""


@dataclass(frozen=True)
class UiLength:
    value: float
    unit: str = "px"


@dataclass(frozen=True)
class UiScriptNode:
    type_name: str
    name: str | None
    properties: Mapping[str, Any]
    children: tuple["UiScriptNode", ...]
    source_path: str


@dataclass(frozen=True)
class UiScriptDescription:
    version: int
    root: UiScriptNode


@dataclass(frozen=True)
class _WidgetType:
    factory: Callable[[Document, UiScriptNode], Any]
    properties: frozenset[str]
    container: bool


@dataclass
class MaterializedWidget:
    """Both the typed public wrapper and its common ``WidgetRef``."""

    public: Any
    widget: Any
    properties: Mapping[str, Any]


class UiScriptRegistry:
    """Registry of explicitly supported widget types and properties."""

    def __init__(self) -> None:
        self._types: dict[str, _WidgetType] = {}

    def register(
        self,
        type_name: str,
        factory: Callable[[Document, UiScriptNode], Any],
        *,
        properties: set[str] | frozenset[str] = frozenset(),
        container: bool = False,
    ) -> None:
        if not type_name or type_name in self._types:
            raise ValueError(f"uiscript widget type is empty or already registered: {type_name!r}")
        self._types[type_name] = _WidgetType(
            factory=factory,
            properties=frozenset(properties),
            container=container,
        )

    def type(self, type_name: str) -> _WidgetType:
        try:
            return self._types[type_name]
        except KeyError as error:
            supported = ", ".join(sorted(self._types))
            raise UiScriptError(
                f"unsupported gui-native uiscript widget type {type_name!r}; "
                f"supported types: {supported}"
            ) from error

    @property
    def type_names(self) -> tuple[str, ...]:
        return tuple(sorted(self._types))


_COMMON_PROPERTIES = frozenset({"visible", "enabled"})
_COMPOSITION_PROPERTIES = frozenset({"anchor", "offset", "position", "width", "height"})


def default_uiscript_registry() -> UiScriptRegistry:
    registry = UiScriptRegistry()
    registry.register(
        "Overlay",
        lambda document, node: document.create_overlay_layout(node.name or "Overlay"),
        properties={"background_color"} | _COMPOSITION_PROPERTIES,
        container=True,
    )
    registry.register(
        "Panel",
        lambda document, node: document.create_panel(node.name or "Panel"),
        properties={"background_color"} | _COMPOSITION_PROPERTIES,
        container=True,
    )
    registry.register(
        "HStack",
        lambda document, node: document.create_hstack(node.name or "HStack"),
        properties={"spacing"},
        container=True,
    )
    registry.register(
        "VStack",
        lambda document, node: document.create_vstack(node.name or "VStack"),
        properties={"spacing"},
        container=True,
    )
    registry.register(
        "IconButton",
        lambda document, node: document.create_icon_button(str(node.properties.get("icon", ""))),
        properties={
            "icon",
            "tooltip",
            "size",
            "font_size",
            "background_color",
            "hover_color",
            "pressed_color",
            "active_color",
            "icon_color",
            "border_radius",
            "active",
        },
    )
    return registry


def _fail(path: str, message: str) -> UiScriptError:
    return UiScriptError(f"{path}: {message}")


def _number(value: Any, path: str, *, minimum: float | None = None) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise _fail(path, "expected a number")
    result = float(value)
    if minimum is not None and result < minimum:
        raise _fail(path, f"expected a value >= {minimum:g}")
    return result


def _color(value: Any, path: str) -> tuple[float, float, float, float]:
    if not isinstance(value, list) or len(value) not in (3, 4):
        raise _fail(path, "expected an RGB or RGBA list")
    channels = tuple(_number(channel, f"{path}[{index}]") for index, channel in enumerate(value))
    if any(channel < 0.0 or channel > 1.0 for channel in channels):
        raise _fail(path, "color channels must be in the [0, 1] range")
    return channels if len(channels) == 4 else (*channels, 1.0)


def _length(value: Any, path: str) -> UiLength:
    if isinstance(value, bool):
        raise _fail(path, "expected a pixel or percent length")
    if isinstance(value, (int, float)):
        return UiLength(float(value))
    if not isinstance(value, str):
        raise _fail(path, "expected a pixel or percent length")
    text = value.strip()
    unit = "px"
    if text.endswith("px"):
        text = text[:-2]
    elif text.endswith("%"):
        unit = "%"
        text = text[:-1]
    try:
        return UiLength(float(text), unit)
    except ValueError as error:
        raise _fail(path, f"invalid length {value!r}") from error


def _pair(value: Any, path: str, item_parser: Callable[[Any, str], Any]) -> tuple[Any, Any]:
    if not isinstance(value, list) or len(value) != 2:
        raise _fail(path, "expected a two-item list")
    return item_parser(value[0], f"{path}[0]"), item_parser(value[1], f"{path}[1]")


def _normalize_property(key: str, value: Any, path: str) -> Any:
    if key in {"visible", "enabled", "active"}:
        if not isinstance(value, bool):
            raise _fail(path, "expected a boolean")
        return value
    if key in {"spacing", "size", "font_size", "border_radius"}:
        return _number(value, path, minimum=0.0)
    if key in {
        "background_color",
        "hover_color",
        "pressed_color",
        "active_color",
        "icon_color",
    }:
        return _color(value, path)
    if key in {"icon", "tooltip"}:
        if not isinstance(value, str):
            raise _fail(path, "expected a string")
        return value
    if key == "anchor":
        if value not in {"absolute", "top-left", "top-right", "bottom-left", "bottom-right"}:
            raise _fail(path, f"unsupported anchor {value!r}")
        return value
    if key == "offset":
        return _pair(value, path, lambda item, item_path: _number(item, item_path))
    if key == "position":
        return _pair(value, path, _length)
    if key in {"width", "height"}:
        return _length(value, path)
    raise _fail(path, f"property {key!r} has no v1 converter")


class UiScriptParser:
    def __init__(self, registry: UiScriptRegistry | None = None) -> None:
        self.registry = registry or default_uiscript_registry()

    def parse(self, source: str) -> UiScriptDescription:
        try:
            data = yaml.safe_load(source)
        except yaml.YAMLError as error:
            raise UiScriptError(f"invalid gui-native uiscript YAML: {error}") from error
        if not isinstance(data, dict):
            raise _fail("document", "expected a mapping")
        unknown_document_keys = sorted(set(data) - {"uiscript", "root"})
        if unknown_document_keys:
            raise _fail("document", f"unsupported key(s): {', '.join(unknown_document_keys)}")
        if data.get("uiscript") != UISCRIPT_VERSION:
            raise _fail("document.uiscript", f"expected dialect version {UISCRIPT_VERSION}")
        if "root" not in data:
            raise _fail("document", "missing root")
        names: set[str] = set()
        root = self._parse_node(data["root"], "root", names)
        return UiScriptDescription(UISCRIPT_VERSION, root)

    def _parse_node(self, data: Any, path: str, names: set[str]) -> UiScriptNode:
        if not isinstance(data, dict):
            raise _fail(path, "expected a widget mapping")
        type_name = data.get("type")
        if not isinstance(type_name, str) or not type_name:
            raise _fail(path, "missing non-empty string property 'type'")
        widget_type = self.registry.type(type_name)
        name = data.get("name")
        if name is not None and (not isinstance(name, str) or not name):
            raise _fail(f"{path}.name", "expected a non-empty string")
        if name in names:
            raise _fail(f"{path}.name", f"duplicate widget name {name!r}")
        if name is not None:
            names.add(name)

        supported = _COMMON_PROPERTIES | _COMPOSITION_PROPERTIES | widget_type.properties
        unsupported = sorted(set(data) - {"type", "name", "children"} - supported)
        if unsupported:
            raise _fail(path, f"unsupported {type_name} property/properties: {', '.join(unsupported)}")
        properties = {
            key: _normalize_property(key, value, f"{path}.{key}")
            for key, value in data.items()
            if key in supported
        }

        raw_children = data.get("children", [])
        if not isinstance(raw_children, list):
            raise _fail(f"{path}.children", "expected a list")
        if raw_children and not widget_type.container:
            raise _fail(f"{path}.children", f"{type_name} does not accept children")
        children = tuple(
            self._parse_node(child, f"{path}.children[{index}]", names)
            for index, child in enumerate(raw_children)
        )
        return UiScriptNode(
            type_name=type_name,
            name=name,
            properties=MappingProxyType(properties),
            children=children,
            source_path=path,
        )


def _common_widget(public: Any) -> Any:
    if isinstance(public, WidgetRef):
        return public
    return public.widget


def _as_native_color(value: tuple[float, float, float, float]) -> Color:
    return Color(*value)


def _apply_panel_style(widget: Any, properties: Mapping[str, Any]) -> None:
    fields = 0
    override = StyleOverride()
    if "background_color" in properties:
        override.value.background = _as_native_color(properties["background_color"])
        fields |= int(StyleField.Background)
    if fields:
        override.fields = fields
        widget.style_override = override


class LoadedUiScript:
    """An owned native widget tree and its stable binding lookup."""

    def __init__(
        self,
        document: Document,
        description: UiScriptDescription,
        root: MaterializedWidget,
        widgets: Mapping[str, MaterializedWidget],
    ) -> None:
        self.document = document
        self.description = description
        self.root = root
        self._widgets = MappingProxyType(dict(widgets))
        self._closed = False

    @property
    def widgets(self) -> Mapping[str, MaterializedWidget]:
        return self._widgets

    def named(self, name: str) -> Any:
        try:
            return self._widgets[name].public
        except KeyError as error:
            raise KeyError(f"gui-native uiscript has no named widget {name!r}") from error

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self.root.widget.alive:
            self.document.destroy_widget_recursive(self.root.widget.handle)

    def __enter__(self) -> "LoadedUiScript":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()


class UiScriptLoader:
    """Parse and materialize gui-native uiscript dialect v1."""

    def __init__(self, registry: UiScriptRegistry | None = None) -> None:
        self.registry = registry or default_uiscript_registry()
        self.parser = UiScriptParser(self.registry)

    def load(self, path: str | Path, document: Document | None = None) -> LoadedUiScript:
        source_path = Path(path)
        try:
            source = source_path.read_text(encoding="utf-8")
        except OSError:
            _LOG.exception("failed to read gui-native uiscript %s", source_path)
            raise
        return self.load_string(source, document=document, source_name=str(source_path))

    def load_string(
        self,
        source: str,
        *,
        document: Document | None = None,
        source_name: str = "<string>",
    ) -> LoadedUiScript:
        try:
            description = self.parser.parse(source)
            return self.materialize(description, document=document)
        except Exception as error:
            _LOG.error("failed to load gui-native uiscript %s: %s", source_name, error)
            raise

    def materialize(
        self,
        description: UiScriptDescription,
        *,
        document: Document | None = None,
    ) -> LoadedUiScript:
        target = document or Document()
        adopted: list[Any] = []
        named: dict[str, MaterializedWidget] = {}

        def build(node: UiScriptNode) -> MaterializedWidget:
            widget_type = self.registry.type(node.type_name)
            public = widget_type.factory(target, node)
            widget = _common_widget(public)
            adopted.append(widget)
            if node.name is not None:
                widget.name = node.name
                widget.stable_id = node.name
            if "visible" in node.properties:
                widget.visible = node.properties["visible"]
            if "enabled" in node.properties:
                widget.enabled = node.properties["enabled"]
            if node.type_name in {"HStack", "VStack"} and "spacing" in node.properties:
                widget.set_layout_spacing(node.properties["spacing"])
            if node.type_name in {"Overlay", "Panel"}:
                _apply_panel_style(widget, node.properties)
            if node.type_name == "IconButton":
                if "active" in node.properties:
                    public.active = node.properties["active"]
                if "size" in node.properties:
                    widget.preferred_size = Size(node.properties["size"], node.properties["size"])
                if "tooltip" in node.properties:
                    public.tooltip = node.properties["tooltip"]
                color_setters = {
                    "background_color": public.set_background_color,
                    "hover_color": public.set_hover_color,
                    "pressed_color": public.set_pressed_color,
                    "active_color": public.set_active_color,
                    "icon_color": public.set_icon_color,
                }
                for property_name, setter in color_setters.items():
                    if property_name in node.properties:
                        setter(_as_native_color(node.properties[property_name]))
                if "border_radius" in node.properties:
                    public.set_corner_radius(node.properties["border_radius"])
                if "font_size" in node.properties:
                    public.set_font_size(node.properties["font_size"])
            result = MaterializedWidget(public, widget, node.properties)
            if node.name is not None:
                named[node.name] = result
            for child in node.children:
                child_result = build(child)
                if node.type_name == "Overlay":
                    anchors = {
                        None: OverlayAnchor.Fill,
                        "absolute": OverlayAnchor.Fill,
                        "top-left": OverlayAnchor.TopLeft,
                        "top-right": OverlayAnchor.TopRight,
                        "bottom-left": OverlayAnchor.BottomLeft,
                        "bottom-right": OverlayAnchor.BottomRight,
                    }
                    offset = child.properties.get("offset", (0.0, 0.0))
                    accepted = public.add_child(
                        child_result.widget,
                        anchors[child.properties.get("anchor")],
                        Point(*offset),
                    )
                else:
                    accepted = widget.append_child(child_result.widget)
                if not accepted:
                    raise _fail(child.source_path, "native parent rejected child")
            return result

        try:
            root = build(description.root)
            if not target.add_root(root.widget.handle):
                raise _fail(description.root.source_path, "native document rejected root")
            return LoadedUiScript(target, description, root, named)
        except Exception:
            for widget in reversed(adopted):
                if widget.alive:
                    target.destroy_widget_recursive(widget.handle)
            raise

    def reload(self, loaded: LoadedUiScript, source: str) -> LoadedUiScript:
        replacement = self.load_string(source, document=loaded.document, source_name="<reload>")
        loaded.close()
        return replacement
