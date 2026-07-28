#include "termin_visual_scene/tc_graphic_item.h"

#include <string.h>

#include <tcbase/tc_log.h>

void tc_graphic_item_init_unowned(
    tc_graphic_item* item,
    const tc_graphic_item_vtable* vtable,
    tc_language native_language,
    void* body)
{
    if (item == NULL) {
        tc_log_error("tc_graphic_item_init_unowned: item is NULL");
        return;
    }
    memset(item, 0, sizeof(*item));
    item->vtable = vtable;
    item->handle = tc_graphic_item_handle_invalid();
    item->native_language = native_language;
    item->body = body;
    tc_runtime_type_instance_link_init(&item->runtime_type_link);
    item->parent = tc_graphic_item_handle_invalid();
    item->first_child = tc_graphic_item_handle_invalid();
    item->next_sibling = tc_graphic_item_handle_invalid();
    item->previous_sibling = tc_graphic_item_handle_invalid();
    item->local_transform = tc_affine2f_identity();
    item->visible = true;
    item->enabled = true;
    item->opacity = 1.0f;
    item->dirty_flags = TC_GRAPHIC_ITEM_DIRTY_ALL;
}

bool tc_graphic_item_is_attached(const tc_graphic_item* item) {
    return item != NULL &&
           item->scene != NULL &&
           !tc_graphic_item_handle_is_invalid(item->handle);
}

const char* tc_graphic_item_type_name(const tc_graphic_item* item) {
    if (item == NULL) {
        return NULL;
    }
    if (item->runtime_type_link.type_name != NULL) {
        return item->runtime_type_link.type_name;
    }
    return item->vtable != NULL ? item->vtable->type_name : NULL;
}
