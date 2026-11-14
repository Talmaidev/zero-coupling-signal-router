#include "ui_internal.h"
#include "zcsr/diagnostics.h"

ZCSR_Result zcsr_ui_create(const ZCSR_UIConfig* config, ZCSR_UI** out_ui)
{
    ZCSR_UI* ui;
    if (!config || !out_ui) return ZCSR_ERR_NULL_POINTER;
    if (config->max_nodes == 0) return ZCSR_ERR_INVALID_ARGUMENT;

    ui = (ZCSR_UI*)calloc(1, sizeof(ZCSR_UI));
    if (!ui) {
        ZCSR_LOG_ERROR("ui", "Failed to allocate ZCSR_UI");
        return ZCSR_ERR_OUT_OF_MEMORY;
    }

    ui->nodes = (ZCSR_UI_Node*)calloc(config->max_nodes, sizeof(ZCSR_UI_Node));
    ui->events = (ZCSR_UIEvent*)calloc(config->event_queue_capacity > 0 ? config->event_queue_capacity : 1, sizeof(ZCSR_UIEvent));
    ui->draw_cmds = (ZCSR_DrawCommand*)calloc(config->draw_command_capacity > 0 ? config->draw_command_capacity : 1, sizeof(ZCSR_DrawCommand));

    if (!ui->nodes || !ui->events || !ui->draw_cmds) {
        ZCSR_LOG_ERROR("ui", "Failed to allocate nodes/events/draw_cmds");
        zcsr_ui_destroy(ui);
        return ZCSR_ERR_OUT_OF_MEMORY;
    }

    ui->node_capacity = config->max_nodes;
    ui->event_cap = config->event_queue_capacity > 0 ? config->event_queue_capacity : 1;
    ui->draw_cap = config->draw_command_capacity > 0 ? config->draw_command_capacity : 1;
    ui->next_id = 1;
    ui->viewport_w = config->viewport_width;
    ui->viewport_h = config->viewport_height;

    *out_ui = ui;
    ZCSR_LOG_INFO("ui", "UI created (max_nodes=%u)", config->max_nodes);
    return ZCSR_OK;
}

void zcsr_ui_destroy(ZCSR_UI* ui)
{
    uint32_t i;
    if (!ui) return;

    /* Free all node text buffers */
    for (i = 0; i < ui->node_count; i++) {
        if (ui->nodes[i].text_buf) {
            free(ui->nodes[i].text_buf);
            ui->nodes[i].text_buf = NULL;
        }
    }

    free(ui->nodes);
    free(ui->events);
    free(ui->draw_cmds);
    free(ui);
    ZCSR_LOG_INFO("ui", "UI destroyed");
}

ZCSR_Result zcsr_ui_set_viewport(ZCSR_UI* ui, float w, float h)
{
    if (!ui) return ZCSR_ERR_NULL_POINTER;
    ui->viewport_w = w;
    ui->viewport_h = h;
    return ZCSR_OK;
}

ZCSR_UI_Node* ui_find_node(ZCSR_UI* ui, ZCSR_NodeId id)
{
    uint32_t i;
    if (!ui || id == ZCSR_NODE_INVALID) return NULL;
    for (i = 0; i < ui->node_count; i++)
        if (ui->nodes[i].id == id) return &ui->nodes[i];
    return NULL;
}

const ZCSR_UI_Node* ui_find_node_c(const ZCSR_UI* ui, ZCSR_NodeId id)
{
    uint32_t i;
    if (!ui || id == ZCSR_NODE_INVALID) return NULL;
    for (i = 0; i < ui->node_count; i++)
        if (ui->nodes[i].id == id) return &ui->nodes[i];
    return NULL;
}

static bool is_valid_component(ZCSR_ComponentId c)
{
    switch (c) {
    case ZCSR_COMPONENT_PANEL: case ZCSR_COMPONENT_BUTTON:
    case ZCSR_COMPONENT_TOGGLE: case ZCSR_COMPONENT_SLIDER:
    case ZCSR_COMPONENT_DROPDOWN: case ZCSR_COMPONENT_LABEL:
    case ZCSR_COMPONENT_SEPARATOR: case ZCSR_COMPONENT_IMAGE:
    case ZCSR_COMPONENT_TEXT_INPUT: case ZCSR_COMPONENT_SCROLL_VIEW:
        return true;
    }
    return false;
}

/* Check if child_id is a descendant of ancestor_id */
static bool ui_is_descendant(ZCSR_UI* ui, ZCSR_NodeId child_id, ZCSR_NodeId ancestor_id)
{
    ZCSR_NodeId cur = child_id;
    while (cur != ZCSR_NODE_INVALID) {
        if (cur == ancestor_id) return true;
        ZCSR_UI_Node* n = ui_find_node(ui, cur);
        if (!n) return false;
        cur = n->parent;
    }
    return false;
}

ZCSR_Result zcsr_node_create(ZCSR_UI* ui, ZCSR_ComponentId comp,
                             ZCSR_NodeId parent_id, ZCSR_NodeId* out_id)
{
    ZCSR_UI_Node* n;
    if (!ui || !out_id) return ZCSR_ERR_NULL_POINTER;
    if (!is_valid_component(comp)) {
        ZCSR_LOG_ERROR("ui", "Invalid component id: 0x%X", comp);
        return ZCSR_ERR_INVALID_ARGUMENT;
    }
    if (ui->node_count >= ui->node_capacity) {
        ZCSR_LOG_ERROR("ui", "Node capacity exhausted (%u)", ui->node_capacity);
        return ZCSR_ERR_OUT_OF_MEMORY;
    }
    if (parent_id != ZCSR_NODE_INVALID && !ui_find_node(ui, parent_id)) {
        ZCSR_LOG_ERROR("ui", "Parent node not found: %u", parent_id);
        return ZCSR_ERR_NOT_FOUND;
    }

    n = &ui->nodes[ui->node_count++];
    memset(n, 0, sizeof(*n));
    n->id = ui->next_id++;
    n->component = comp;
    n->flags = ZCSR_NODE_VISIBLE | ZCSR_NODE_ENABLED;
    n->visible = true;
    n->enabled = true;
    n->parent = parent_id;
    if (comp == ZCSR_COMPONENT_DROPDOWN) n->int_val = -1;

    /* Default style colors. */
    n->bg_color    = (ZCSR_Color){0.12f, 0.14f, 0.17f, 1.0f};
    n->text_color  = (ZCSR_Color){0.85f, 0.87f, 0.90f, 1.0f};
    n->border_color = (ZCSR_Color){0.25f, 0.28f, 0.33f, 1.0f};
    n->bg_override = false;
    n->text_override = false;
    n->border_override = false;

    if (parent_id != ZCSR_NODE_INVALID) {
        ZCSR_UI_Node* p = ui_find_node(ui, parent_id);
        n->next_sibling = p->first_child;
        p->first_child = n->id;
    }

    *out_id = n->id;
    ZCSR_LOG_DEBUG("ui", "Node created: id=%u, component=0x%X", n->id, comp);
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_destroy(ZCSR_UI* ui, ZCSR_NodeId id)
{
    uint32_t i;

    if (!ui) return ZCSR_ERR_NULL_POINTER;

    /* Check node exists */
    if (!ui_find_node(ui, id)) {
        ZCSR_LOG_WARN("ui", "Destroy failed: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }

    /* Destroy all children first (by id, not pointer, to avoid pointer invalidation) */
    while (true) {
        ZCSR_UI_Node* n = ui_find_node(ui, id);
        if (!n || n->first_child == ZCSR_NODE_INVALID) break;
        zcsr_node_destroy(ui, n->first_child);
    }

    /* Remove from parent's child list */
    {
        ZCSR_UI_Node* n = ui_find_node(ui, id);
        if (!n) return ZCSR_ERR_NOT_FOUND; /* Should not happen */

        if (n->parent != ZCSR_NODE_INVALID) {
            ZCSR_UI_Node* p = ui_find_node(ui, n->parent);
            if (p) {
                ZCSR_NodeId prev_id = ZCSR_NODE_INVALID;
                ZCSR_NodeId c = p->first_child;
                while (c != id && c != ZCSR_NODE_INVALID) {
                    prev_id = c;
                    ZCSR_UI_Node* cn = ui_find_node(ui, c);
                    if (!cn) break; /* Should not happen */
                    c = cn->next_sibling;
                }
                if (c == id) {
                    ZCSR_UI_Node* n2 = ui_find_node(ui, id);
                    if (prev_id != ZCSR_NODE_INVALID) {
                        ZCSR_UI_Node* prev = ui_find_node(ui, prev_id);
                        if (prev) prev->next_sibling = n2 ? n2->next_sibling : ZCSR_NODE_INVALID;
                    } else {
                        p->first_child = n2 ? n2->next_sibling : ZCSR_NODE_INVALID;
                    }
                }
            }
        }

        /* Free text buffer */
        if (n->text_buf) {
            free(n->text_buf);
            n->text_buf = NULL;
            n->text_cap = 0;
        }
    }

    /* Compact array - find index by id */
    for (i = 0; i < ui->node_count; i++) {
        if (ui->nodes[i].id == id) {
            memmove(&ui->nodes[i], &ui->nodes[i+1],
                    (ui->node_count - i - 1) * sizeof(ZCSR_UI_Node));
            break;
        }
    }
    ui->node_count--;

    ZCSR_LOG_DEBUG("ui", "Node destroyed: %u", id);
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_set_rect(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r)
{
    ZCSR_UI_Node* n;
    if (!ui || !r) return ZCSR_ERR_NULL_POINTER;
    n = ui_find_node(ui, id);
    if (!n) return ZCSR_ERR_NOT_FOUND;
    n->local_rect = *r;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_rect(const ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_Rect* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    *out = n->local_rect;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_world_rect(const ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_Rect* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    float ox = 0, oy = 0;
    const ZCSR_UI_Node* root = NULL;

    if (!n || !out) return ZCSR_ERR_NOT_FOUND;

    /* Walk up parent chain accumulating offset */
    while (n) {
        ox += n->local_rect.x;
        oy += n->local_rect.y;
        root = n;
        if (n->parent == ZCSR_NODE_INVALID) break;
        n = ui_find_node_c(ui, n->parent);
    }

    out->x = ox;
    out->y = oy;
    out->width = root ? root->local_rect.width : 0;
    out->height = root ? root->local_rect.height : 0;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_set_flags(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t flags)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (!n) return ZCSR_ERR_NOT_FOUND;
    n->flags = flags;
    n->visible = (flags & ZCSR_NODE_VISIBLE) != 0;
    n->enabled = (flags & ZCSR_NODE_ENABLED) != 0;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_flags(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    *out = n->flags;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_set_z_order(ZCSR_UI* ui, ZCSR_NodeId id, int32_t z)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (!n) return ZCSR_ERR_NOT_FOUND;
    n->z_order = z;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_set_parent(ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_NodeId new_p)
{
    ZCSR_UI_Node* n;
    ZCSR_NodeId old_parent;

    if (!ui) return ZCSR_ERR_NULL_POINTER;

    n = ui_find_node(ui, id);
    if (!n) {
        ZCSR_LOG_ERROR("ui", "set_parent: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }

    if (new_p == id) {
        ZCSR_LOG_ERROR("ui", "Cannot set node as own parent: %u", id);
        return ZCSR_ERR_INVALID_ARGUMENT;
    }

    /* Check new parent exists */
    if (new_p != ZCSR_NODE_INVALID && !ui_find_node(ui, new_p)) {
        ZCSR_LOG_ERROR("ui", "set_parent: new parent not found: %u", new_p);
        return ZCSR_ERR_NOT_FOUND;
    }

    /* Check for cycle: new_parent must not be a descendant of id */
    if (new_p != ZCSR_NODE_INVALID && ui_is_descendant(ui, new_p, id)) {
        ZCSR_LOG_ERROR("ui", "Cannot set parent: would create cycle (%u -> %u)", id, new_p);
        return ZCSR_ERR_INVALID_ARGUMENT;
    }

    old_parent = n->parent;

    /* If parent is unchanged, do nothing */
    if (old_parent == new_p) return ZCSR_OK;

    /* Step 1: Remove from old parent's child list */
    if (old_parent != ZCSR_NODE_INVALID) {
        ZCSR_UI_Node* p = ui_find_node(ui, old_parent);
        if (p) {
            ZCSR_NodeId prev_id = ZCSR_NODE_INVALID;
            ZCSR_NodeId c = p->first_child;
            while (c != id && c != ZCSR_NODE_INVALID) {
                prev_id = c;
                ZCSR_UI_Node* cn = ui_find_node(ui, c);
                if (!cn) break;
                c = cn->next_sibling;
            }
            if (c == id) {
                ZCSR_UI_Node* n2 = ui_find_node(ui, id);
                if (prev_id != ZCSR_NODE_INVALID) {
                    ZCSR_UI_Node* prev = ui_find_node(ui, prev_id);
                    if (prev) prev->next_sibling = n2 ? n2->next_sibling : ZCSR_NODE_INVALID;
                } else {
                    p->first_child = n2 ? n2->next_sibling : ZCSR_NODE_INVALID;
                }
            }
        }
    }

    /* Step 2: Add to new parent's child list */
    if (new_p != ZCSR_NODE_INVALID) {
        ZCSR_UI_Node* p = ui_find_node(ui, new_p);
        if (p) {
            n->next_sibling = p->first_child;
            p->first_child = id;
        }
    } else {
        n->next_sibling = ZCSR_NODE_INVALID;
    }

    /* Step 3: Update parent pointer */
    n->parent = new_p;

    ZCSR_LOG_DEBUG("ui", "Node %u parent changed: %u -> %u", id, old_parent, new_p);
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_component_id(const ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_ComponentId* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    *out = n->component;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_parent(const ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_NodeId* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    *out = n->parent;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_child_count(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    uint32_t count = 0;
    ZCSR_NodeId c;
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    c = n->first_child;
    while (c != ZCSR_NODE_INVALID) {
        count++;
        const ZCSR_UI_Node* cn = ui_find_node_c(ui, c);
        if (!cn) break;
        c = cn->next_sibling;
    }
    *out = count;
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_child_at(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t idx, ZCSR_NodeId* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    ZCSR_NodeId c;
    uint32_t i = 0;
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    c = n->first_child;
    while (c != ZCSR_NODE_INVALID) {
        if (i == idx) { *out = c; return ZCSR_OK; }
        i++;
        const ZCSR_UI_Node* cn = ui_find_node_c(ui, c);
        if (!cn) break;
        c = cn->next_sibling;
    }
    return ZCSR_ERR_NOT_FOUND;
}

ZCSR_Result zcsr_ui_set_focus(ZCSR_UI* ui, ZCSR_NodeId id)
{
    ZCSR_UI_Node* n;
    if (!ui) return ZCSR_ERR_NULL_POINTER;
    if (id == ZCSR_NODE_INVALID) { ui->focus_id = ZCSR_NODE_INVALID; return ZCSR_OK; }
    n = ui_find_node(ui, id);
    if (!n) return ZCSR_ERR_NOT_FOUND;
    ui->focus_id = id;
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_get_focus(const ZCSR_UI* ui, ZCSR_NodeId* out)
{
    if (!ui || !out) return ZCSR_ERR_NULL_POINTER;
    *out = ui->focus_id;
    return ZCSR_OK;
}

void ui_emit_event(ZCSR_UI* ui, ZCSR_EventTypeEnum type, ZCSR_NodeId src, ZCSR_ComponentId comp)
{
    uint32_t tail;
    if (ui->event_count >= ui->event_cap) {
        ZCSR_LOG_WARN("ui", "Event queue full, dropping event %d", type);
        return;
    }
    tail = (ui->event_head + ui->event_count) % ui->event_cap;
    memset(&ui->events[tail], 0, sizeof(ui->events[tail]));
    ui->events[tail].type = type;
    ui->events[tail].source_node = src;
    ui->events[tail].source_component = comp;
    ui->events[tail].sequence = ui->event_sequence++;
    ui->event_count++;
}

ZCSR_Result zcsr_ui_poll_event(ZCSR_UI* ui, ZCSR_UIEvent* out)
{
    if (!ui || !out) return ZCSR_ERR_NULL_POINTER;
    if (ui->event_count == 0) return ZCSR_ERR_NOT_FOUND;
    *out = ui->events[ui->event_head];
    ui->event_head = (ui->event_head + 1) % ui->event_cap;
    ui->event_count--;
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_peek_event(const ZCSR_UI* ui, ZCSR_UIEvent* out)
{
    if (!ui || !out) return ZCSR_ERR_NULL_POINTER;
    if (ui->event_count == 0) return ZCSR_ERR_NOT_FOUND;
    *out = ui->events[ui->event_head];
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_clear_events(ZCSR_UI* ui)
{
    if (!ui) return ZCSR_ERR_NULL_POINTER;
    ui->event_head = 0;
    ui->event_count = 0;
    return ZCSR_OK;
}

/* ---- UTF-8 helpers ---- */
static uint32_t utf8_seq_len(const char* s, uint32_t len, uint32_t pos)
{
    unsigned char c;
    if (pos >= len) return 0;
    c = (unsigned char)s[pos];
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
static uint32_t utf8_next(const char* s, uint32_t len, uint32_t pos)
{
    uint32_t sl = utf8_seq_len(s, len, pos);
    return sl == 0 ? pos : pos + sl;
}
static uint32_t utf8_prev(const char* s, uint32_t pos)
{
    uint32_t p;
    if (pos == 0) return 0;
    p = pos - 1;
    while (p > 0 && (((unsigned char)s[p] & 0xC0) == 0x80))
        p--;
    return p;
}

/* Hit test: topmost visible enabled node at (x,y), ignoring separator/label/panel background. */
static ZCSR_NodeId hit_test(ZCSR_UI* ui, float x, float y)
{
    int i;
    ZCSR_NodeId best = ZCSR_NODE_INVALID;
    int best_z = -1000000;
    /* Iterate in reverse so later nodes (higher z) win. */
    for (i = (int)ui->node_count - 1; i >= 0; i--) {
        ZCSR_UI_Node* n = &ui->nodes[i];
        ZCSR_Rect r;
        if (!n->visible) continue;
        /* Skip non-interactive for hit test. */
        if (n->component == ZCSR_COMPONENT_SEPARATOR ||
            n->component == ZCSR_COMPONENT_LABEL ||
            n->component == ZCSR_COMPONENT_PANEL ||
            n->component == ZCSR_COMPONENT_IMAGE)
            continue;
        r = n->local_rect;
        /* Simple: use local rect as world rect for now (no parent transform yet). */
        if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
            if (n->z_order > best_z) {
                best_z = n->z_order;
                best = n->id;
            }
        }
    }
    return best;
}

ZCSR_Result zcsr_ui_process_input(ZCSR_UI* ui, const ZCSR_Input* in)
{
    ZCSR_UI_Node* n;
    ZCSR_NodeId hit;
    if (!ui || !in) return ZCSR_ERR_NULL_POINTER;

    switch (in->type) {
    case ZCSR_INPUT_POINTER_MOVE:
        /* Update hover. */
        for (uint32_t i = 0; i < ui->node_count; i++)
            ui->nodes[i].hovered = false;
        hit = hit_test(ui, in->pointer_position.x, in->pointer_position.y);
        n = ui_find_node(ui, hit);
        if (n) n->hovered = true;

        /* Slider drag while captured. */
        if (ui->pressed_id != ZCSR_NODE_INVALID) {
            n = ui_find_node(ui, ui->pressed_id);
            if (n && n->component == ZCSR_COMPONENT_SLIDER && n->enabled) {
                float t = (in->pointer_position.x - n->local_rect.x) / n->local_rect.width;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                n->float_val = n->float_min + t * (n->float_max - n->float_min);
                ui_emit_event(ui, ZCSR_EVENT_SLIDER_CHANGED, n->id, n->component);
            }
        }

        /* ScrollView thumb drag while captured. */
        if (ui->scroll_capture_id != ZCSR_NODE_INVALID) {
            n = ui_find_node(ui, ui->scroll_capture_id);
            if (n && n->component == ZCSR_COMPONENT_SCROLL_VIEW) {
                float dx = in->pointer_position.y - ui->scroll_capture_start_y;
                float view_h = n->local_rect.height;
                float content_h = n->content_h;
                float thumb_h = content_h > view_h ? view_h * (view_h / content_h) : view_h;
                float track_h = view_h - thumb_h;
                if (track_h > 0) {
                    float total_range = content_h - view_h;
                    n->scroll_y = ui->scroll_capture_start_offset + dx * total_range / track_h;
                    if (n->scroll_y < 0) n->scroll_y = 0;
                    if (n->scroll_y > total_range) n->scroll_y = total_range;
                    ui_emit_event(ui, ZCSR_EVENT_SCROLL_CHANGED, n->id, n->component);
                }
            }
        }
        break;

    case ZCSR_INPUT_POINTER_DOWN:
        if (in->pointer_button == ZCSR_POINTER_LEFT) {
            /* Dropdown popup dismissal / row selection takes priority. */
            {
                uint32_t di;
                for (di = 0; di < ui->node_count; di++) {
                    ZCSR_UI_Node* d = &ui->nodes[di];
                    if (d->component != ZCSR_COMPONENT_DROPDOWN || !d->popup_open) continue;
                    float row_h = 24.0f;
                    float popup_h = d->item_count * row_h;
                    if (d->popup_max_h > 0 && popup_h > d->popup_max_h) popup_h = d->popup_max_h;
                    float px = d->local_rect.x;
                    float py = d->local_rect.y + d->local_rect.height;
                    float pw = d->local_rect.width;
                    float mx = in->pointer_position.x;
                    float my = in->pointer_position.y;
                    /* Click inside a popup row? */
                    if (mx >= px && mx < px + pw && my >= py && my < py + popup_h &&
                        d->enabled) {
                        uint32_t row = (uint32_t)((my - py) / row_h);
                        if (row < d->item_count) {
                            d->int_val = (int32_t)row;
                            d->popup_open = false;
                            ui_emit_event(ui, ZCSR_EVENT_DROPDOWN_CHANGED, d->id, d->component);
                            ui_emit_event(ui, ZCSR_EVENT_DROPDOWN_CLOSED, d->id, d->component);
                            ui->pressed_id = ZCSR_NODE_INVALID;
                            ui->captured_pointer = 0;
                            return ZCSR_OK;
                        }
                    }
                    /* Click outside the dropdown button AND popup -> close. */
                    if (! (mx >= d->local_rect.x && mx < d->local_rect.x + d->local_rect.width &&
                           my >= d->local_rect.y && my < d->local_rect.y + d->local_rect.height)) {
                        d->popup_open = false;
                        ui_emit_event(ui, ZCSR_EVENT_DROPDOWN_CLOSED, d->id, d->component);
                    }
                }
            }
            hit = hit_test(ui, in->pointer_position.x, in->pointer_position.y);
            n = ui_find_node(ui, hit);
            if (n && n->enabled) {
                n->pressed = true;
                ui->pressed_id = hit;
                ui->focus_id = hit;
                ui->captured_pointer = in->pointer_id;
                /* Slider: set value immediately. */
                if (n->component == ZCSR_COMPONENT_SLIDER) {
                    float t = (in->pointer_position.x - n->local_rect.x) / n->local_rect.width;
                    if (t < 0) t = 0;
                if (t > 1) t = 1;
                    n->float_val = n->float_min + t * (n->float_max - n->float_min);
                    ui_emit_event(ui, ZCSR_EVENT_SLIDER_CHANGED, n->id, n->component);
                }
                /* ScrollView: check if clicking the scrollbar thumb. */
                if (n->component == ZCSR_COMPONENT_SCROLL_VIEW) {
                    float bar_x = n->local_rect.x + n->local_rect.width - 8.0f;
                    if (in->pointer_position.x >= bar_x &&
                        n->content_h > n->local_rect.height) {
                        ui->scroll_capture_id = hit;
                        ui->scroll_capture_start_y = in->pointer_position.y;
                        ui->scroll_capture_start_offset = n->scroll_y;
                        ui->pressed_id = ZCSR_NODE_INVALID; /* don't treat as button press */
                    }
                }
            }
        }
        break;

    case ZCSR_INPUT_POINTER_UP:
        n = ui_find_node(ui, ui->pressed_id);
        if (n) {
            n->pressed = false;
            hit = hit_test(ui, in->pointer_position.x, in->pointer_position.y);
            if (hit == n->id && n->enabled) {
                if (n->component == ZCSR_COMPONENT_BUTTON) {
                    ui_emit_event(ui, ZCSR_EVENT_BUTTON_CLICKED, n->id, n->component);
                } else if (n->component == ZCSR_COMPONENT_TOGGLE) {
                    n->bool_val = !n->bool_val;
                    ui_emit_event(ui, ZCSR_EVENT_TOGGLE_CHANGED, n->id, n->component);
                } else if (n->component == ZCSR_COMPONENT_DROPDOWN) {
                    n->popup_open = !n->popup_open;
                    ui_emit_event(ui, n->popup_open ? ZCSR_EVENT_DROPDOWN_OPENED : ZCSR_EVENT_DROPDOWN_CLOSED,
                                  n->id, n->component);
                }
            }
        }
        ui->pressed_id = ZCSR_NODE_INVALID;
        ui->scroll_capture_id = ZCSR_NODE_INVALID;
        ui->captured_pointer = 0;
        break;

    case ZCSR_INPUT_POINTER_WHEEL: {
        ZCSR_NodeId sv = hit_test(ui, in->pointer_position.x, in->pointer_position.y);
        n = ui_find_node(ui, sv);
        if (n && n->component == ZCSR_COMPONENT_SCROLL_VIEW && n->enabled) {
            n->scroll_y += in->wheel_y * 32.0f;
            float max_s = n->content_h - n->local_rect.height;
            if (n->scroll_y < 0) n->scroll_y = 0;
            if (max_s > 0 && n->scroll_y > max_s) n->scroll_y = max_s;
            ui_emit_event(ui, ZCSR_EVENT_SCROLL_CHANGED, n->id, n->component);
        }
        break;
    }

    case ZCSR_INPUT_KEY_DOWN: {
        n = ui_find_node(ui, ui->focus_id);
        if (!n || !n->enabled) break;
        if (n->component == ZCSR_COMPONENT_BUTTON) {
            if (in->key == ZCSR_KEY_ENTER || in->key == ZCSR_KEY_SPACE)
                ui_emit_event(ui, ZCSR_EVENT_BUTTON_CLICKED, n->id, n->component);
        } else if (n->component == ZCSR_COMPONENT_TOGGLE) {
            if (in->key == ZCSR_KEY_ENTER || in->key == ZCSR_KEY_SPACE) {
                n->bool_val = !n->bool_val;
                ui_emit_event(ui, ZCSR_EVENT_TOGGLE_CHANGED, n->id, n->component);
            }
        } else if (n->component == ZCSR_COMPONENT_SLIDER) {
            float step = n->float_step > 0 ? n->float_step : (n->float_max - n->float_min) * 0.1f;
            if (in->key == ZCSR_KEY_LEFT || in->key == ZCSR_KEY_DOWN)
                n->float_val -= step;
            if (in->key == ZCSR_KEY_RIGHT || in->key == ZCSR_KEY_UP)
                n->float_val += step;
            if (in->key == ZCSR_KEY_HOME) n->float_val = n->float_min;
            if (in->key == ZCSR_KEY_END) n->float_val = n->float_max;
            if (n->float_val < n->float_min) n->float_val = n->float_min;
            if (n->float_val > n->float_max) n->float_val = n->float_max;
            ui_emit_event(ui, ZCSR_EVENT_SLIDER_CHANGED, n->id, n->component);
        } else if (n->component == ZCSR_COMPONENT_TEXT_INPUT) {
            if (in->key == ZCSR_KEY_ENTER)
                ui_emit_event(ui, ZCSR_EVENT_TEXT_SUBMITTED, n->id, n->component);
            else if (in->key == ZCSR_KEY_ESCAPE)
                ui_emit_event(ui, ZCSR_EVENT_TEXT_CANCELLED, n->id, n->component);
            else if (in->key == ZCSR_KEY_BACKSPACE && n->caret_byte > 0 && n->text_buf) {
                uint32_t len = (uint32_t)strlen(n->text_buf);
                uint32_t prev = utf8_prev(n->text_buf, n->caret_byte);
                uint32_t sl = n->caret_byte - prev; /* bytes to delete */
                memmove(n->text_buf + prev,
                        n->text_buf + n->caret_byte,
                        len - n->caret_byte);
                n->caret_byte = prev;
                n->text_buf[len - sl] = '\0';
                ui_emit_event(ui, ZCSR_EVENT_TEXT_CHANGED, n->id, n->component);
            } else if (in->key == ZCSR_KEY_LEFT && n->caret_byte > 0) {
                n->caret_byte = utf8_prev(n->text_buf, n->caret_byte);
            } else if (in->key == ZCSR_KEY_RIGHT && n->text_buf) {
                uint32_t len = (uint32_t)strlen(n->text_buf);
                uint32_t nx = utf8_next(n->text_buf, len, n->caret_byte);
                if (nx > n->caret_byte) n->caret_byte = nx;
            } else if (in->key == ZCSR_KEY_HOME)
                n->caret_byte = 0;
            else if (in->key == ZCSR_KEY_END && n->text_buf)
                n->caret_byte = (uint32_t)strlen(n->text_buf);
        }
        break;
    }

    case ZCSR_INPUT_TEXT: {
        n = ui_find_node(ui, ui->focus_id);
        if (n && n->component == ZCSR_COMPONENT_TEXT_INPUT && n->enabled && n->text_buf) {
            size_t len = strlen(n->text_buf);
            size_t cap = n->text_cap;
            uint32_t j;
            for (j = 0; j < in->text_bytes && len + 1 < cap; j++) {
                if (n->caret_byte < len)
                    memmove(n->text_buf + n->caret_byte + 1,
                            n->text_buf + n->caret_byte,
                            len - n->caret_byte);
                n->text_buf[n->caret_byte] = in->text_utf8[j];
                n->caret_byte++;
                len++;
            }
            n->text_buf[len] = '\0';
            ui_emit_event(ui, ZCSR_EVENT_TEXT_CHANGED, n->id, n->component);
        }
        break;
    }

    default: break;
    }
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_release_pointer(ZCSR_UI* ui, uint32_t pid)
{
    if (!ui) return ZCSR_ERR_NULL_POINTER;
    if (ui->captured_pointer == pid) ui->captured_pointer = 0;
    return ZCSR_OK;
}

/* Helper: push a RECT draw command. */
static void push_rect(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r,
                      ZCSR_Color c, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_RECT;
    cmd->source_node = id;
    cmd->rect = *r;
    cmd->color = c;
    cmd->layer = layer;
}

static void push_border(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r,
                        float thick, ZCSR_Color c, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_BORDER;
    cmd->source_node = id;
    cmd->rect = *r;
    cmd->thickness = thick;
    cmd->color = c;
    cmd->layer = layer;
}

static void push_image(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r,
                       uint32_t key, const ZCSR_Rect* uv, ZCSR_Color tint, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_IMAGE;
    cmd->source_node = id;
    cmd->rect = *r;
    cmd->color = tint;
    cmd->resource_key = key;
    cmd->uv_rect = *uv;
    cmd->layer = layer;
}

static void push_text(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r,
                      const char* utf8, uint32_t bytes, ZCSR_Color c, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_TEXT;
    cmd->source_node = id;
    cmd->rect = *r;
    cmd->text_utf8 = utf8;
    cmd->text_bytes = bytes;
    cmd->color = c;
    cmd->layer = layer;
}

static void push_clip_push(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_CLIP_PUSH;
    cmd->source_node = id;
    cmd->rect = *r;
    cmd->layer = layer;
}

static void push_clip_pop(ZCSR_UI* ui, ZCSR_NodeId id, int32_t layer)
{
    if (ui->draw_count >= ui->draw_cap) return;
    ZCSR_DrawCommand* cmd = &ui->draw_cmds[ui->draw_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = ZCSR_DRAW_CLIP_POP;
    cmd->source_node = id;
    cmd->layer = layer;
}

ZCSR_Result zcsr_ui_build_draw_list(ZCSR_UI* ui)
{
    uint32_t i;
    if (!ui) return ZCSR_ERR_NULL_POINTER;
    ui->draw_count = 0;

    static const ZCSR_Color bg_color    = {0.12f, 0.14f, 0.17f, 1.0f};
    static const ZCSR_Color bg_hover    = {0.17f, 0.19f, 0.22f, 1.0f};
    static const ZCSR_Color bg_pressed  = {0.22f, 0.20f, 0.18f, 1.0f};
    static const ZCSR_Color border_col  = {0.25f, 0.28f, 0.33f, 1.0f};
    static const ZCSR_Color text_col    = {0.85f, 0.87f, 0.90f, 1.0f};
    static const ZCSR_Color track_col   = {0.18f, 0.20f, 0.24f, 1.0f};
    static const ZCSR_Color knob_col    = {0.55f, 0.58f, 0.62f, 1.0f};
    static const ZCSR_Color knob_on     = {0.20f, 0.60f, 0.35f, 1.0f};
    static const ZCSR_Color knob_off    = {0.30f, 0.30f, 0.35f, 1.0f};
    static const ZCSR_Color caret_col   = {0.85f, 0.87f, 0.90f, 1.0f};

    for (i = 0; i < ui->node_count && ui->draw_count < ui->draw_cap; i++) {
        ZCSR_UI_Node* n = &ui->nodes[i];
        if (!n->visible) continue;
        ZCSR_Rect r = n->local_rect;
        int32_t layer = n->z_order;

        switch (n->component) {
        /* ---- Panel: solid background + border ---- */
        case ZCSR_COMPONENT_PANEL: {
            ZCSR_Color c = n->bg_override ? n->bg_color : bg_color;
            if (n->hovered) c = bg_hover;
            push_rect(ui, n->id, &r, c, layer);
            push_border(ui, n->id, &r, 1.0f, border_col, layer);
            break;
        }

        /* ---- Button: bg + text + border, hover/pressed tint ---- */
        case ZCSR_COMPONENT_BUTTON: {
            ZCSR_Color c = n->pressed ? bg_pressed : (n->hovered ? bg_hover : bg_color);
            push_rect(ui, n->id, &r, c, layer);
            if (n->text_buf)
                push_text(ui, n->id, &r, n->text_buf, (uint32_t)strlen(n->text_buf), text_col, layer);
            push_border(ui, n->id, &r, 1.0f, border_col, layer);
            break;
        }

        /* ---- Toggle: label text + track + knob ---- */
        case ZCSR_COMPONENT_TOGGLE: {
            /* Label text on left */
            ZCSR_Rect label_rect = { r.x, r.y, r.width - 70.0f, r.height };
            if (n->text_buf)
                push_text(ui, n->id, &label_rect, n->text_buf, (uint32_t)strlen(n->text_buf), text_col, layer);
            /* Track on right */
            ZCSR_Rect track = { r.x + r.width - 60.0f, r.y + (r.height - 20.0f) * 0.5f, 50.0f, 20.0f };
            push_rect(ui, n->id, &track, n->bool_val ? knob_on : knob_off, layer);
            /* Knob */
            float knob_x = n->bool_val ? (track.x + track.width - 18.0f) : (track.x + 2.0f);
            ZCSR_Rect knob = { knob_x, track.y + 2.0f, 16.0f, 16.0f };
            push_rect(ui, n->id, &knob, knob_col, layer + 1);
            break;
        }

        /* ---- Slider: track + knob + value text ---- */
        case ZCSR_COMPONENT_SLIDER: {
            /* Track */
            float cy = r.y + r.height * 0.5f;
            ZCSR_Rect track = { r.x, cy - 3.0f, r.width, 6.0f };
            push_rect(ui, n->id, &track, track_col, layer);
            /* Knob position */
            float range = n->float_max - n->float_min;
            float frac = range > 0.001f ? (n->float_val - n->float_min) / range : 0.5f;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            float knob_x = r.x + frac * r.width;
            ZCSR_Rect knob = { knob_x - 6.0f, cy - 8.0f, 12.0f, 16.0f };
            push_rect(ui, n->id, &knob, n->hovered ? bg_hover : knob_col, layer + 1);
            break;
        }

        /* ---- Dropdown: bg + selected text + border + popup ---- */
        case ZCSR_COMPONENT_DROPDOWN: {
            push_rect(ui, n->id, &r, n->pressed ? bg_pressed : (n->hovered ? bg_hover : bg_color), layer);
            if (n->int_val >= 0 && n->int_val < (int32_t)n->item_count)
                push_text(ui, n->id, &r, n->items[n->int_val], (uint32_t)strlen(n->items[n->int_val]), text_col, layer);
            push_border(ui, n->id, &r, 1.0f, border_col, layer);

            /* Popup */
            if (n->popup_open) {
                float row_h = 24.0f;
                float popup_h = n->item_count * row_h;
                if (n->popup_max_h > 0 && popup_h > n->popup_max_h) popup_h = n->popup_max_h;
                ZCSR_Rect pr = { r.x, r.y + r.height, r.width, popup_h };
                push_clip_push(ui, n->id, &pr, layer + 1);
                ZCSR_Color pop_bg = {0.10f, 0.12f, 0.15f, 1.0f};
                push_rect(ui, n->id, &pr, pop_bg, layer + 1);
                ZCSR_Color pop_border = {0.30f, 0.60f, 0.90f, 1.0f};
                push_border(ui, n->id, &pr, 1.0f, pop_border, layer + 1);
                uint32_t ri;
                for (ri = 0; ri < n->item_count; ri++) {
                    ZCSR_Rect row = { r.x, r.y + r.height + ri * row_h, r.width, row_h };
                    bool sel = ((int32_t)ri == n->int_val);
                    ZCSR_Color rc = sel ? (ZCSR_Color){0.18f,0.30f,0.55f,1.0f}
                                         : (ZCSR_Color){0.13f,0.15f,0.18f,1.0f};
                    push_rect(ui, n->id, &row, rc, layer + 2);
                    push_text(ui, n->id, &row, n->items[ri], (uint32_t)strlen(n->items[ri]), text_col, layer + 2);
                }
                push_clip_pop(ui, n->id, layer + 1);
            }
            break;
        }

        /* ---- Label: text only, no bg/border ---- */
        case ZCSR_COMPONENT_LABEL: {
            if (n->text_buf)
                push_text(ui, n->id, &r, n->text_buf, (uint32_t)strlen(n->text_buf), text_col, layer);
            break;
        }

        /* ---- Separator: thin line ---- */
        case ZCSR_COMPONENT_SEPARATOR: {
            ZCSR_Rect line;
            if (r.height > r.width) { /* vertical */
                line.x = r.x + r.width * 0.5f - 1.0f;
                line.y = r.y; line.width = 2.0f; line.height = r.height;
            } else { /* horizontal */
                line.x = r.x; line.y = r.y + r.height * 0.5f - 1.0f;
                line.width = r.width; line.height = 2.0f;
            }
            push_rect(ui, n->id, &line, border_col, layer);
            break;
        }

        /* ---- Image: 有资源键就推真纹理命令，没有才画"缺图"样式 ---- */
        case ZCSR_COMPONENT_IMAGE: {
            if (n->resource_key != 0u) {
                /* 未设置 UV 时按整图采样（1,1），避免 0,0 导致全透明 */
                ZCSR_Rect uv;
                uv.x = n->uv_x;
                uv.y = n->uv_y;
                uv.width  = (n->uv_w > 0.0f) ? n->uv_w : 1.0f;
                uv.height = (n->uv_h > 0.0f) ? n->uv_h : 1.0f;
                push_image(ui, n->id, &r, n->resource_key, &uv, bg_color, layer);
            } else {
                push_rect(ui, n->id, &r, (ZCSR_Color){0.08f,0.09f,0.11f,1.0f}, layer);
                push_border(ui, n->id, &r, 1.0f, border_col, layer);
            }
            break;
        }

        /* ---- TextInput: bg + text + caret ---- */
        case ZCSR_COMPONENT_TEXT_INPUT: {
            push_rect(ui, n->id, &r, n->focused ? (ZCSR_Color){0.10f,0.13f,0.18f,1.0f} : bg_color, layer);
            if (n->text_buf)
                push_text(ui, n->id, &r, n->text_buf, (uint32_t)strlen(n->text_buf), text_col, layer);
            push_border(ui, n->id, &r, 1.0f, n->focused ? (ZCSR_Color){0.30f,0.60f,0.90f,1.0f} : border_col, layer);
            /* Caret: thin vertical line */
            if (n->focused) {
                ZCSR_Rect caret = { r.x + 8.0f + n->caret_byte * 7.0f, r.y + 4.0f, 2.0f, r.height - 8.0f };
                push_rect(ui, n->id, &caret, caret_col, layer + 1);
            }
            break;
        }

        /* ---- ScrollView: clip + bg + border ---- */
        case ZCSR_COMPONENT_SCROLL_VIEW: {
            push_clip_push(ui, n->id, &r, layer);
            push_rect(ui, n->id, &r, bg_color, layer);
            push_border(ui, n->id, &r, 1.0f, border_col, layer);
            /* Scrollbar thumb on right side */
            if (n->content_h > r.height) {
                float track_h = r.height;
                float thumb_h = track_h * (r.height / n->content_h);
                float max_scroll = n->content_h - r.height;
                float frac = max_scroll > 0 ? n->scroll_y / max_scroll : 0;
                ZCSR_Rect sb = { r.x + r.width - 8.0f, r.y + frac * (track_h - thumb_h), 6.0f, thumb_h };
                push_rect(ui, n->id, &sb, knob_col, layer + 1);
            }
            push_clip_pop(ui, n->id, layer);
            break;
        }

        default:
            break;
        }
    }
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_get_draw_command_count(const ZCSR_UI* ui, uint32_t* out)
{
    if (!ui || !out) return ZCSR_ERR_NULL_POINTER;
    *out = ui->draw_count;
    return ZCSR_OK;
}

ZCSR_Result zcsr_ui_get_draw_command(const ZCSR_UI* ui, uint32_t idx, ZCSR_DrawCommand* out)
{
    if (!ui || !out) return ZCSR_ERR_NULL_POINTER;
    if (idx >= ui->draw_count) return ZCSR_ERR_NOT_FOUND;
    *out = ui->draw_cmds[idx];
    return ZCSR_OK;
}

/* ---- Style colors ---- */
ZCSR_Result zcsr_node_set_color(ZCSR_UI* ui, ZCSR_NodeId id,
                                ZCSR_ColorRole role, ZCSR_Color color)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (!n) return ZCSR_ERR_NOT_FOUND;
    switch (role) {
    case ZCSR_COLOR_BG:     n->bg_color = color;    n->bg_override = true;    break;
    case ZCSR_COLOR_TEXT:   n->text_color = color;  n->text_override = true;  break;
    case ZCSR_COLOR_BORDER: n->border_color = color; n->border_override = true; break;
    default: return ZCSR_ERR_INVALID_ARGUMENT;
    }
    return ZCSR_OK;
}

ZCSR_Result zcsr_node_get_color(const ZCSR_UI* ui, ZCSR_NodeId id,
                                ZCSR_ColorRole role, ZCSR_Color* out)
{
    const ZCSR_UI_Node* n = ui_find_node_c(ui, id);
    if (!n || !out) return ZCSR_ERR_NOT_FOUND;
    switch (role) {
    case ZCSR_COLOR_BG:     *out = n->bg_color; break;
    case ZCSR_COLOR_TEXT:   *out = n->text_color; break;
    case ZCSR_COLOR_BORDER: *out = n->border_color; break;
    default: return ZCSR_ERR_INVALID_ARGUMENT;
    }
    return ZCSR_OK;
}
