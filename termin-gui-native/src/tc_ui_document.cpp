#include <termin/gui_native/tc_ui_document.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include <tcbase/tc_log.h>

namespace {

struct WidgetSlot {
    tc_widget* widget = nullptr;
    uint32_t generation = 1;
    bool destroying = false;
};

struct RecursiveDestroyContext {
    tc_ui_document* document = nullptr;
    bool ok = true;
};

bool same_handle(tc_widget_handle lhs, tc_widget_handle rhs) {
    return tc_widget_handle_eq(lhs, rhs);
}

} // namespace

struct tc_ui_document {
    std::vector<WidgetSlot> slots;
    std::vector<uint32_t> free_slots;
    std::vector<tc_widget_handle> roots;
    size_t live_count = 0;
};

static WidgetSlot* resolve_slot(tc_ui_document* document, tc_widget_handle handle) {
    if (!document || tc_widget_handle_is_invalid(handle)) {
        return nullptr;
    }
    if (handle.index >= document->slots.size()) {
        return nullptr;
    }

    WidgetSlot& slot = document->slots[handle.index];
    if (slot.generation != handle.generation || !slot.widget) {
        return nullptr;
    }
    return &slot;
}

static const WidgetSlot* resolve_slot_const(const tc_ui_document* document, tc_widget_handle handle) {
    if (!document || tc_widget_handle_is_invalid(handle)) {
        return nullptr;
    }
    if (handle.index >= document->slots.size()) {
        return nullptr;
    }

    const WidgetSlot& slot = document->slots[handle.index];
    if (slot.generation != handle.generation || !slot.widget) {
        return nullptr;
    }
    return &slot;
}

static void remove_root_references(tc_ui_document* document, tc_widget_handle handle) {
    if (!document) {
        return;
    }
    document->roots.erase(
        std::remove_if(
            document->roots.begin(),
            document->roots.end(),
            [handle](tc_widget_handle root) { return same_handle(root, handle); }
        ),
        document->roots.end()
    );
}

static bool destroy_widget_inner(tc_ui_document* document, tc_widget_handle handle, bool recursive);

static void recursive_destroy_visit(void* user_data, tc_widget_handle handle) {
    auto* context = static_cast<RecursiveDestroyContext*>(user_data);
    if (!context || !context->document) {
        return;
    }

    if (!destroy_widget_inner(context->document, handle, true)) {
        context->ok = false;
    }
}

static bool destroy_widget_inner(tc_ui_document* document, tc_widget_handle handle, bool recursive) {
    WidgetSlot* slot = resolve_slot(document, handle);
    if (!slot) {
        tc_log_error(
            "[termin-gui-native] cannot destroy invalid widget handle index=%u generation=%u",
            handle.index,
            handle.generation
        );
        return false;
    }

    if (slot->destroying) {
        tc_log_error(
            "[termin-gui-native] recursive destroy cycle or duplicate destroy at widget handle index=%u generation=%u",
            handle.index,
            handle.generation
        );
        return false;
    }

    tc_widget* widget = slot->widget;
    slot->destroying = true;

    bool ok = true;
    if (recursive && widget->vtable && widget->vtable->visit_recursive_destroy_targets) {
        RecursiveDestroyContext context {document, true};
        widget->vtable->visit_recursive_destroy_targets(
            widget,
            document,
            &context,
            recursive_destroy_visit
        );
        ok = context.ok;
    }

    if (widget->vtable && widget->vtable->on_destroy) {
        widget->vtable->on_destroy(widget, document);
    }

    remove_root_references(document, handle);

    widget->handle = tc_widget_handle_invalid();
    tc_widget_deleter deleter = widget->deleter;

    slot->widget = nullptr;
    slot->destroying = false;
    slot->generation += 1;
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    document->free_slots.push_back(handle.index);
    document->live_count -= 1;

    if (deleter) {
        deleter(widget);
    } else {
        tc_log_error(
            "[termin-gui-native] widget handle index=%u generation=%u has no deleter; object storage was not released",
            handle.index,
            handle.generation
        );
        ok = false;
    }

    return ok;
}

extern "C" {

tc_widget_handle tc_widget_handle_invalid_value(void) {
    return tc_widget_handle_invalid();
}

bool tc_widget_handle_valid_value(tc_widget_handle handle) {
    return !tc_widget_handle_is_invalid(handle);
}

void tc_widget_init(
    tc_widget* widget,
    const tc_widget_vtable* vtable,
    tc_widget_deleter deleter,
    tc_language native_language,
    void* body
) {
    if (!widget) {
        tc_log_error("[termin-gui-native] tc_widget_init called with null widget");
        return;
    }

    widget->vtable = vtable;
    widget->deleter = deleter;
    widget->handle = tc_widget_handle_invalid();
    widget->native_language = native_language;
    widget->body = body;
    widget->debug_name = nullptr;
    widget->flags = 0;
}

tc_ui_document* tc_ui_document_create(void) {
    return new (std::nothrow) tc_ui_document();
}

void tc_ui_document_destroy(tc_ui_document* document) {
    if (!document) {
        return;
    }

    while (document->live_count > 0) {
        bool destroyed_one = false;
        for (uint32_t index = 0; index < document->slots.size(); ++index) {
            WidgetSlot& slot = document->slots[index];
            if (!slot.widget) {
                continue;
            }

            tc_widget_handle handle {index, slot.generation};
            destroy_widget_inner(document, handle, false);
            destroyed_one = true;
            break;
        }

        if (!destroyed_one) {
            tc_log_error("[termin-gui-native] document live count is inconsistent during destroy");
            break;
        }
    }

    delete document;
}

tc_widget_handle tc_ui_document_adopt_widget(tc_ui_document* document, tc_widget* widget) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot adopt widget into null document");
        return tc_widget_handle_invalid();
    }
    if (!widget) {
        tc_log_error("[termin-gui-native] cannot adopt null widget");
        return tc_widget_handle_invalid();
    }
    if (!tc_widget_handle_is_invalid(widget->handle)) {
        tc_log_error(
            "[termin-gui-native] cannot adopt widget that already has handle index=%u generation=%u",
            widget->handle.index,
            widget->handle.generation
        );
        return tc_widget_handle_invalid();
    }
    if (!widget->deleter) {
        tc_log_error("[termin-gui-native] cannot adopt widget without deleter");
        return tc_widget_handle_invalid();
    }

    uint32_t index = 0;
    if (!document->free_slots.empty()) {
        index = document->free_slots.back();
        document->free_slots.pop_back();
    } else {
        if (document->slots.size() >= std::numeric_limits<uint32_t>::max()) {
            tc_log_error("[termin-gui-native] widget slot index overflow");
            return tc_widget_handle_invalid();
        }
        index = static_cast<uint32_t>(document->slots.size());
        document->slots.push_back(WidgetSlot {});
    }

    WidgetSlot& slot = document->slots[index];
    slot.widget = widget;
    slot.destroying = false;

    tc_widget_handle handle {index, slot.generation};
    widget->handle = handle;
    document->live_count += 1;
    return handle;
}

bool tc_ui_document_is_alive(const tc_ui_document* document, tc_widget_handle handle) {
    const WidgetSlot* slot = resolve_slot_const(document, handle);
    return slot && !slot->destroying;
}

tc_widget* tc_ui_document_resolve_widget(tc_ui_document* document, tc_widget_handle handle) {
    WidgetSlot* slot = resolve_slot(document, handle);
    if (!slot || slot->destroying) {
        return nullptr;
    }
    return slot->widget;
}

const tc_widget* tc_ui_document_resolve_widget_const(
    const tc_ui_document* document,
    tc_widget_handle handle
) {
    const WidgetSlot* slot = resolve_slot_const(document, handle);
    if (!slot || slot->destroying) {
        return nullptr;
    }
    return slot->widget;
}

bool tc_ui_document_destroy_widget(tc_ui_document* document, tc_widget_handle handle) {
    return destroy_widget_inner(document, handle, false);
}

bool tc_ui_document_destroy_widget_recursive(tc_ui_document* document, tc_widget_handle handle) {
    return destroy_widget_inner(document, handle, true);
}

size_t tc_ui_document_live_widget_count(const tc_ui_document* document) {
    return document ? document->live_count : 0;
}

bool tc_ui_document_add_root(tc_ui_document* document, tc_widget_handle handle) {
    if (!tc_ui_document_is_alive(document, handle)) {
        tc_log_error(
            "[termin-gui-native] cannot add invalid root handle index=%u generation=%u",
            handle.index,
            handle.generation
        );
        return false;
    }

    const auto exists = std::any_of(
        document->roots.begin(),
        document->roots.end(),
        [handle](tc_widget_handle root) { return same_handle(root, handle); }
    );
    if (!exists) {
        document->roots.push_back(handle);
    }
    return true;
}

bool tc_ui_document_remove_root(tc_ui_document* document, tc_widget_handle handle) {
    if (!document) {
        return false;
    }

    const size_t before = document->roots.size();
    remove_root_references(document, handle);
    return document->roots.size() != before;
}

size_t tc_ui_document_root_count(const tc_ui_document* document) {
    return document ? document->roots.size() : 0;
}

tc_widget_handle tc_ui_document_root_at(const tc_ui_document* document, size_t index) {
    if (!document || index >= document->roots.size()) {
        return tc_widget_handle_invalid();
    }
    return document->roots[index];
}

void tc_ui_document_paint_roots(tc_ui_document* document, tc_ui_paint_context* context) {
    if (!document) {
        tc_log_error("[termin-gui-native] cannot paint roots of null document");
        return;
    }

    const std::vector<tc_widget_handle> roots = document->roots;
    for (tc_widget_handle root : roots) {
        tc_widget* widget = tc_ui_document_resolve_widget(document, root);
        if (!widget) {
            tc_log_error(
                "[termin-gui-native] skipping stale root handle index=%u generation=%u",
                root.index,
                root.generation
            );
            continue;
        }
        if (widget->vtable && widget->vtable->paint) {
            widget->vtable->paint(widget, document, context);
        }
    }
}

} // extern "C"
