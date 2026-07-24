"""Shared ownership fixture for native UI documents created by tests."""

from __future__ import annotations

import pytest


@pytest.fixture(autouse=True)
def _destroy_created_documents(request, monkeypatch):
    """Ensure every test-created document is explicitly destroyed at teardown."""
    module = request.module
    create = getattr(module, "tc_ui_document_create", None)
    destroy = getattr(module, "tc_ui_document_destroy", None)
    if create is None or destroy is None:
        yield
        return

    documents = []

    def tracked_create():
        document = create()
        documents.append(document)
        return document

    def tracked_destroy(document):
        if document in documents:
            documents.remove(document)
        destroy(document)

    monkeypatch.setattr(module, "tc_ui_document_create", tracked_create)
    monkeypatch.setattr(module, "tc_ui_document_destroy", tracked_destroy)
    yield
    for document in reversed(documents):
        destroy(document)
