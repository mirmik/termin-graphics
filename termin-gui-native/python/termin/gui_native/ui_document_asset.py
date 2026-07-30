"""Thin Python facade over native immutable UI document assets."""

from termin.gui_native._gui_native import (
    UiDocumentAsset,
    UiDocumentAssetHandle,
)

UI_DOCUMENT_ASSET_SCHEMA_VERSION = 1

__all__ = [
    "UI_DOCUMENT_ASSET_SCHEMA_VERSION",
    "UiDocumentAsset",
    "UiDocumentAssetHandle",
]
