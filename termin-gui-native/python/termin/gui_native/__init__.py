"""Experimental native Termin UI document bindings."""

from typing import Protocol, runtime_checkable

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("nanobind", "termin_base", "termin_graphics2", "termin_gui_native")

from termin.gui_native._gui_native import (  # noqa: E402
    Button,
    Checkbox,
    Color,
    ColorDialog,
    ColorPicker,
    ColorPickerModel,
    ColorPickerSurface,
    ColorPickerSurfaceKind,
    ColorPickerTextureIds,
    Canvas,
    CollectionItem,
    CollectionModel,
    Command,
    CommandData,
    CommandKind,
    CommandModel,
    ComboBox,
    Constraints,
    Document,
    Dialog,
    DialogAction,
    DialogDismissReason,
    DialogResult,
    DrawCommand,
    DrawCommandType,
    DrawList,
    DrawListRenderer,
    EdgeInsets,
    EventResult,
    FileDialogConfirmResult,
    FileDialogEntry,
    FileDialogFilter,
    FileDialogMode,
    FileDialogModel,
    FileDialogOverlay,
    FileGridWidget,
    FrameTimeGraph,
    FrameTimeModel,
    GraphicsItem,
    GraphicsScene,
    FontRole,
    IconButton,
    ImageWidget,
    InputDialog,
    KeyCode,
    KeyEvent,
    KeyEventType,
    ListWidget,
    LayoutPolicy,
    Menu,
    MenuBar,
    MenuBarEntry,
    MessageBox,
    MessageBoxKind,
    ModifierFlag,
    OverlayDismissReason,
    OverlayFlag,
    PaintContext,
    Point,
    PointerEvent,
    PointerEventType,
    Rect,
    RichTextModel,
    RichTextSegment,
    RichTextStyle,
    RichTextView,
    ScrollArea,
    SceneTransform,
    SceneView,
    SelectionMode,
    Size,
    SliderEdit,
    SpinBox,
    Style,
    StyleField,
    StyleOverride,
    StyleOverrideFlag,
    StyleRole,
    StyleState,
    StatusBar,
    TableColumn,
    TableColumnLayout,
    TableColumnModel,
    TableColumnPolicy,
    TableModel,
    TableRow,
    TableRowData,
    TableWidget,
    ToolBar,
    TextArea,
    TextInput,
    TextMetrics,
    Theme,
    TreeDropPosition,
    TreeExpansionModel,
    TreeModel,
    TreeNode,
    TreeVisibleRow,
    TreeWidget,
    Viewport3D,
    ViewportExternalDragEvent,
    ViewportExternalDragPhase,
    ViewportSurfaceSize,
    WidgetFlag,
    WidgetHandle,
    WidgetLanguage,
    WidgetOwnerReloadPolicy,
    WidgetOwnership,
    WidgetRef,
    has_widget_type,
    invalid_widget_handle,
    register_widget_type,
    registered_widget_types,
    tooltip_rect,
    unregister_widget_owner,
    unregister_widget_type,
)


@runtime_checkable
class ViewportSurfaceHost(Protocol):
    """Backend-neutral offscreen surface contract consumed by ``Viewport3D``."""

    def is_valid(self) -> bool: ...

    def get_tgfx_color_tex_id(self) -> int: ...

    def framebuffer_size(self) -> tuple[int, int]: ...

    def resize(self, width: int, height: int) -> bool: ...

    def dispatch_pointer_move(self, x: float, y: float) -> bool: ...

    def dispatch_pointer_button(
        self,
        button: int,
        action: int,
        modifiers: int,
        click_count: int,
    ) -> bool: ...

    def dispatch_scroll(self, x: float, y: float, modifiers: int) -> bool: ...

    def dispatch_key(self, key: int, scancode: int, action: int, modifiers: int) -> bool: ...

    def dispatch_text(self, codepoint: int) -> bool: ...


class Widget:
    """Base class for Python widgets adopted by ``Document``."""

    debug_name: str | None = None
    _native: WidgetRef | None = None

    def _bind_native(self, native: WidgetRef) -> None:
        if self._native is not None and self._native.alive:
            raise RuntimeError("widget is already adopted")
        self._native = native

    @property
    def native(self) -> WidgetRef:
        if self._native is None:
            raise RuntimeError("widget has not been adopted")
        return self._native

    @property
    def handle(self) -> WidgetHandle:
        if self._native is None:
            return invalid_widget_handle()
        return self._native.handle

    @property
    def alive(self) -> bool:
        return self._native is not None and self._native.alive

    @property
    def bounds(self) -> Rect:
        return self.native.bounds

    @bounds.setter
    def bounds(self, value: Rect) -> None:
        self.native.bounds = value

    @property
    def visible(self) -> bool:
        return self.native.visible

    @visible.setter
    def visible(self, value: bool) -> None:
        self.native.visible = value

    @property
    def enabled(self) -> bool:
        return self.native.enabled

    @enabled.setter
    def enabled(self, value: bool) -> None:
        self.native.enabled = value

    @property
    def mouse_transparent(self) -> bool:
        return self.native.mouse_transparent

    @mouse_transparent.setter
    def mouse_transparent(self, value: bool) -> None:
        self.native.mouse_transparent = value

    @property
    def focusable(self) -> bool:
        return self.native.focusable

    @focusable.setter
    def focusable(self, value: bool) -> None:
        self.native.focusable = value

    def measure(self, constraints: Constraints) -> Size:
        preferred = self.native.preferred_size
        width = max(constraints.min_size.width, preferred.width)
        height = max(constraints.min_size.height, preferred.height)
        if constraints.max_size.width > 0.0:
            width = min(width, constraints.max_size.width)
        if constraints.max_size.height > 0.0:
            height = min(height, constraints.max_size.height)
        return Size(width, height)

    def layout(self, rect: Rect) -> None:
        pass

    def paint(self, context: PaintContext) -> None:
        for child in self.native.children:
            if child.visible:
                child.paint(context)

    def pointer_event(self, event: PointerEvent) -> EventResult:
        return EventResult.Ignored

    def hit_test(self, x: float, y: float) -> WidgetHandle:
        bounds = self.bounds
        inside = bounds.x <= x < bounds.x + bounds.width and bounds.y <= y < bounds.y + bounds.height
        if not self.visible or not inside:
            return invalid_widget_handle()
        for child in reversed(self.native.children):
            hit = child.hit_test(x, y)
            if hit:
                return hit
        if self.mouse_transparent:
            return invalid_widget_handle()
        return self.handle

    def key_event(self, event: KeyEvent) -> EventResult:
        return EventResult.Ignored

    def text_event(self, text: str) -> EventResult:
        return EventResult.Ignored

    def focus_event(self, focused: bool) -> None:
        pass

    def overlay_dismissed(self, reason: OverlayDismissReason) -> None:
        pass

    def on_destroy(self) -> None:
        pass


from termin.gui_native.showcase import PythonShowcase, build_python_showcase  # noqa: E402


__all__ = [
    "Button",
    "Checkbox",
    "Color",
    "ColorDialog",
    "ColorPicker",
    "ColorPickerModel",
    "ColorPickerSurface",
    "ColorPickerSurfaceKind",
    "ColorPickerTextureIds",
    "Canvas",
    "CollectionItem",
    "CollectionModel",
    "Command",
    "CommandData",
    "CommandKind",
    "CommandModel",
    "ComboBox",
    "Constraints",
    "Document",
    "Dialog",
    "DialogAction",
    "DialogDismissReason",
    "DialogResult",
    "DrawCommand",
    "DrawCommandType",
    "DrawList",
    "DrawListRenderer",
    "EdgeInsets",
    "EventResult",
    "FileDialogConfirmResult",
    "FileDialogEntry",
    "FileDialogFilter",
    "FileDialogMode",
    "FileDialogModel",
    "FileDialogOverlay",
    "FileGridWidget",
    "FrameTimeGraph",
    "FrameTimeModel",
    "GraphicsItem",
    "GraphicsScene",
    "FontRole",
    "IconButton",
    "ImageWidget",
    "InputDialog",
    "KeyCode",
    "KeyEvent",
    "KeyEventType",
    "ListWidget",
    "LayoutPolicy",
    "Menu",
    "MenuBar",
    "MenuBarEntry",
    "MessageBox",
    "MessageBoxKind",
    "ModifierFlag",
    "OverlayDismissReason",
    "OverlayFlag",
    "PaintContext",
    "Point",
    "PointerEvent",
    "PointerEventType",
    "PythonShowcase",
    "Rect",
    "RichTextModel",
    "RichTextSegment",
    "RichTextStyle",
    "RichTextView",
    "ScrollArea",
    "SceneTransform",
    "SceneView",
    "SelectionMode",
    "Size",
    "SliderEdit",
    "SpinBox",
    "Style",
    "StyleField",
    "StyleOverride",
    "StyleOverrideFlag",
    "StyleRole",
    "StyleState",
    "StatusBar",
    "TableColumn",
    "TableColumnLayout",
    "TableColumnModel",
    "TableColumnPolicy",
    "TableModel",
    "TableRow",
    "TableRowData",
    "TableWidget",
    "ToolBar",
    "TextArea",
    "TextInput",
    "TextMetrics",
    "Theme",
    "TreeDropPosition",
    "TreeExpansionModel",
    "TreeModel",
    "TreeNode",
    "TreeVisibleRow",
    "TreeWidget",
    "Viewport3D",
    "ViewportExternalDragEvent",
    "ViewportExternalDragPhase",
    "ViewportSurfaceSize",
    "ViewportSurfaceHost",
    "Widget",
    "WidgetFlag",
    "WidgetHandle",
    "WidgetLanguage",
    "WidgetOwnerReloadPolicy",
    "WidgetOwnership",
    "WidgetRef",
    "build_python_showcase",
    "has_widget_type",
    "invalid_widget_handle",
    "register_widget_type",
    "registered_widget_types",
    "tooltip_rect",
    "unregister_widget_owner",
    "unregister_widget_type",
]
