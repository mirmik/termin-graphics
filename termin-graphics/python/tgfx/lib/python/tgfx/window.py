from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable, Optional, Tuple


class BackendWindow(ABC):
    """Abstract window wrapper."""

    def set_graphics(self, graphics) -> None:
        """Set graphics backend for framebuffer creation."""
        pass

    @abstractmethod
    def get_window_framebuffer(self):
        """Return a handle for the default window framebuffer."""
        ...

    @abstractmethod
    def close(self):
        ...

    @abstractmethod
    def should_close(self) -> bool:
        ...

    @abstractmethod
    def make_current(self):
        ...

    @abstractmethod
    def swap_buffers(self):
        ...

    @abstractmethod
    def framebuffer_size(self) -> Tuple[int, int]:
        ...

    @abstractmethod
    def window_size(self) -> Tuple[int, int]:
        ...

    @abstractmethod
    def get_cursor_pos(self) -> Tuple[float, float]:
        ...

    @abstractmethod
    def set_should_close(self, flag: bool):
        ...

    @abstractmethod
    def set_user_pointer(self, ptr: Any):
        ...

    @abstractmethod
    def set_framebuffer_size_callback(self, callback: Callable):
        ...

    @abstractmethod
    def set_cursor_pos_callback(self, callback: Callable):
        ...

    @abstractmethod
    def set_scroll_callback(self, callback: Callable):
        ...

    @abstractmethod
    def set_mouse_button_callback(self, callback: Callable):
        ...

    @abstractmethod
    def set_key_callback(self, callback: Callable):
        ...

    def drives_render(self) -> bool:
        """
        Returns True if the backend drives rendering itself (e.g. Qt widget),
        False if the engine calls render() each frame (e.g. GLFW).
        """
        return False

    @abstractmethod
    def request_update(self):
        ...


class WindowBackend(ABC):
    """Abstract window backend (GLFW, SDL, etc.)."""

    @abstractmethod
    def create_window(self, width: int, height: int, title: str, share: Optional[Any] = None) -> BackendWindow:
        ...

    @abstractmethod
    def poll_events(self):
        ...

    @abstractmethod
    def terminate(self):
        ...
