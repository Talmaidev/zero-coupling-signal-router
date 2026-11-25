/* include/zcsr/ui_text_edit.h — the one text editing state machine.
 *
 * Why this file exists:
 * before 2026-11-24 the engine widget (modules/ui/ui.c), the engine
 * text-input accessors (modules/ui/ui_text_input.c) and UIM
 * (UIM/src/uim_text_input.c) each carried their own UTF-8 caret / selection /
 * length-limit code. They disagreed on capacity, on what backspace deletes and
 * on whether an over-long insert truncates or refuses. This header is the only
 * implementation; every layer includes it and edits its own buffer through it.
 *
 * Contract:
 * - Header only, static inline, no allocation, no globals, no I/O, no logging.
 *   Including it adds no link dependency, so the engine stays standalone and
 *   UIM keeps talking to the engine through the public include directory only.
 * - The caller owns the buffer: capacity + 1 bytes, NUL terminated, capacity
 *   is the maximum number of payload bytes (excluding the terminator). Each
 *   layer keeps its own capacity (engine node 256, UIM field 1024); what is
 *   shared is the editing algorithm, not the buffer size.
 * - Text is UTF-8. length / cursor / anchor are byte offsets. cursor and
 *   anchor always sit on a code point boundary; editing is code point based.
 * - Over-long edits are refused with ZCSR_TEXT_ERR_BUFFER and change nothing
 *   (maxLength behavior, never silent truncation). Invalid UTF-8 is refused
 *   with ZCSR_TEXT_ERR_ARGUMENT and nothing is stored.
 * - Programmatic set() ignores read_only (the host is rebinding data);
 *   insert / backspace / delete_forward refuse with ZCSR_TEXT_ERR_READ_ONLY.
 * - A selection is anchor != cursor; insert replaces it, the delete
 *   operations consume it first, and a caret move collapses it to the
 *   direction-appropriate edge.
 */
#ifndef ZCSR_UI_TEXT_EDIT_H
#define ZCSR_UI_TEXT_EDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum zcsr_text_status {
    ZCSR_TEXT_OK = 0,
    ZCSR_TEXT_ERR_ARGUMENT,  /* NULL / not UTF-8 / not a code point boundary */
    ZCSR_TEXT_ERR_RANGE,     /* offset past the end */
    ZCSR_TEXT_ERR_BUFFER,    /* exceeded capacity (maxLength) */
    ZCSR_TEXT_ERR_READ_ONLY  /* field refuses user edits */
} zcsr_text_status;

typedef struct zcsr_text_edit {
    char  *data;      /* caller-owned, capacity + 1 bytes, NUL terminated */
    size_t capacity;  /* max payload bytes, excluding the terminator */
    size_t length;    /* bytes in data */
    size_t cursor;    /* caret byte offset, on a code point boundary */
    size_t anchor;    /* selection anchor; == cursor when collapsed */
    bool   read_only;
} zcsr_text_edit;

/* ---- UTF-8 primitives --------------------------------------------------- */

/* Decode one code point starting at pos. False when the sequence is invalid
 * (bad lead byte, overlong form, surrogate, past U+10FFFF, or truncated). */
static inline bool zcsr_text_utf8_codepoint(const char *s, size_t len, size_t pos,
                                            uint32_t *out_cp, size_t *out_seq)
{
    unsigned char c;
    size_t need, i;
    uint32_t cp, min_cp;
    if (!s || !out_cp || !out_seq || pos >= len) return false;
    c = (unsigned char)s[pos];
    if (c < 0x80u) { *out_cp = (uint32_t)c; *out_seq = 1u; return true; }
    if (c < 0xC2u) return false;                 /* continuation or overlong */
    if (c < 0xE0u)      { need = 2u; min_cp = 0x80u;    cp = (uint32_t)(c & 0x1Fu); }
    else if (c < 0xF0u) { need = 3u; min_cp = 0x800u;   cp = (uint32_t)(c & 0x0Fu); }
    else if (c < 0xF5u) { need = 4u; min_cp = 0x10000u; cp = (uint32_t)(c & 0x07u); }
    else return false;
    if (len - pos < need) return false;
    for (i = 1u; i < need; i++) {
        unsigned char b = (unsigned char)s[pos + i];
        if ((b & 0xC0u) != 0x80u) return false;
        cp = (cp << 6) | (uint32_t)(b & 0x3Fu);
    }
    if (cp < min_cp || cp > 0x10FFFFu) return false;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return false;   /* surrogates */
    *out_cp = cp;
    *out_seq = need;
    return true;
}

static inline bool zcsr_text_utf8_decode(const char *s, size_t len, size_t pos,
                                         size_t *out_seq)
{
    uint32_t cp = 0;
    return zcsr_text_utf8_codepoint(s, len, pos, &cp, out_seq);
}

static inline bool zcsr_text_utf8_valid(const char *s, size_t len)
{
    size_t pos = 0, seq = 0;
    if (!s && len > 0) return false;
    while (pos < len) {
        if (!zcsr_text_utf8_decode(s, len, pos, &seq)) return false;
        pos += seq;
    }
    return true;
}

/* Offset of the next code point (or len). Text is assumed valid. */
static inline size_t zcsr_text_utf8_next(const char *s, size_t len, size_t pos)
{
    size_t seq = 1;
    if (pos >= len) return len;
    (void)zcsr_text_utf8_decode(s, len, pos, &seq);
    if (seq == 0 || pos + seq > len) return len;
    return pos + seq;
}

/* Offset of the previous code point (or 0). */
static inline size_t zcsr_text_utf8_prev(const char *s, size_t pos)
{
    size_t p;
    if (!s || pos == 0) return 0;
    p = pos - 1u;
    while (p > 0 && (((unsigned char)s[p]) & 0xC0u) == 0x80u) p--;
    return p;
}

/* True when pos is 0, length, or the start of a code point. */
static inline bool zcsr_text_on_boundary(const char *s, size_t len, size_t pos)
{
    size_t p = 0, seq = 0;
    if (pos == 0 || pos == len) return true;
    if (pos > len) return false;
    while (p < pos) {
        if (!zcsr_text_utf8_decode(s, len, p, &seq)) return false;
        p += seq;
    }
    return p == pos;
}

/* ---- Lifetime ----------------------------------------------------------- */

/* Bind the view to a caller buffer and empty it. capacity must be >= 1. */
static inline zcsr_text_status zcsr_text_bind(zcsr_text_edit *e, char *data,
                                              size_t capacity)
{
    if (!e || !data || capacity == 0u) return ZCSR_TEXT_ERR_ARGUMENT;
    e->data = data;
    e->capacity = capacity;
    e->length = 0u;
    e->cursor = 0u;
    e->anchor = 0u;
    e->read_only = false;
    data[0] = '\0';
    return ZCSR_TEXT_OK;
}

/* ---- Query -------------------------------------------------------------- */

static inline bool zcsr_text_has_selection(const zcsr_text_edit *e)
{
    return e && e->anchor != e->cursor;
}

static inline void zcsr_text_selection_bounds(const zcsr_text_edit *e,
                                              size_t *out_start, size_t *out_end)
{
    size_t a = e ? e->anchor : 0u;
    size_t c = e ? e->cursor : 0u;
    size_t start = a < c ? a : c;
    size_t end = a < c ? c : a;
    if (out_start) *out_start = start;
    if (out_end) *out_end = end;
}

/* ---- Whole-text set (programmatic; ignores read_only) ------------------- */

static inline zcsr_text_status zcsr_text_set(zcsr_text_edit *e, const char *utf8,
                                             size_t bytes)
{
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (!utf8 && bytes > 0u) return ZCSR_TEXT_ERR_ARGUMENT;
    if (bytes > 0u && !zcsr_text_utf8_valid(utf8, bytes)) return ZCSR_TEXT_ERR_ARGUMENT;
    if (bytes > e->capacity) return ZCSR_TEXT_ERR_BUFFER;
    if (bytes > 0u) memcpy(e->data, utf8, bytes);
    e->data[bytes] = '\0';
    e->length = bytes;
    e->cursor = bytes;
    e->anchor = bytes;
    return ZCSR_TEXT_OK;
}

/* Remove [start, end) and collapse the caret there. Returns bytes removed. */
static inline size_t zcsr_text_erase_range(zcsr_text_edit *e, size_t start, size_t end)
{
    size_t removed;
    if (!e || !e->data || start > end || end > e->length) return 0u;
    removed = end - start;
    if (removed == 0u) return 0u;
    memmove(e->data + start, e->data + end, e->length - end + 1u);
    e->length -= removed;
    e->cursor = start;
    e->anchor = start;
    return removed;
}

/* Delete the selection when one exists. Returns bytes removed (0 = none). */
static inline size_t zcsr_text_delete_selection(zcsr_text_edit *e)
{
    size_t start = 0u, end = 0u;
    if (!zcsr_text_has_selection(e)) return 0u;
    zcsr_text_selection_bounds(e, &start, &end);
    return zcsr_text_erase_range(e, start, end);
}

/* ---- Edits (user input; read_only refuses) ------------------------------ */

static inline zcsr_text_status zcsr_text_insert(zcsr_text_edit *e, const char *utf8,
                                                size_t bytes)
{
    size_t start = 0u, end = 0u, sel, needed;
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (!utf8 && bytes > 0u) return ZCSR_TEXT_ERR_ARGUMENT;
    if (bytes == 0u) return ZCSR_TEXT_OK;
    if (e->read_only) return ZCSR_TEXT_ERR_READ_ONLY;
    if (!zcsr_text_utf8_valid(utf8, bytes)) return ZCSR_TEXT_ERR_ARGUMENT;
    zcsr_text_selection_bounds(e, &start, &end);
    sel = end - start;
    needed = e->length - sel + bytes;
    if (needed > e->capacity) return ZCSR_TEXT_ERR_BUFFER;
    if (sel > 0u) zcsr_text_erase_range(e, start, end);
    if (e->cursor < e->length)
        memmove(e->data + e->cursor + bytes, e->data + e->cursor,
                e->length - e->cursor + 1u);
    memcpy(e->data + e->cursor, utf8, bytes);
    e->length += bytes;
    e->cursor += bytes;
    e->anchor = e->cursor;
    return ZCSR_TEXT_OK;
}

static inline zcsr_text_status zcsr_text_backspace(zcsr_text_edit *e, bool *changed)
{
    size_t prev;
    if (changed) *changed = false;
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (e->read_only) return ZCSR_TEXT_ERR_READ_ONLY;
    if (zcsr_text_has_selection(e)) {
        if (zcsr_text_delete_selection(e) > 0u && changed) *changed = true;
        return ZCSR_TEXT_OK;
    }
    if (e->cursor == 0u) return ZCSR_TEXT_OK;
    prev = zcsr_text_utf8_prev(e->data, e->cursor);
    zcsr_text_erase_range(e, prev, e->cursor);
    if (changed) *changed = true;
    return ZCSR_TEXT_OK;
}

static inline zcsr_text_status zcsr_text_delete_forward(zcsr_text_edit *e, bool *changed)
{
    size_t next;
    if (changed) *changed = false;
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (e->read_only) return ZCSR_TEXT_ERR_READ_ONLY;
    if (zcsr_text_has_selection(e)) {
        if (zcsr_text_delete_selection(e) > 0u && changed) *changed = true;
        return ZCSR_TEXT_OK;
    }
    if (e->cursor >= e->length) return ZCSR_TEXT_OK;
    next = zcsr_text_utf8_next(e->data, e->length, e->cursor);
    zcsr_text_erase_range(e, e->cursor, next);
    if (changed) *changed = true;
    return ZCSR_TEXT_OK;
}

/* ---- Caret / selection -------------------------------------------------- */

/* Move delta code points (negative = left). A selection collapses to its
 * start for negative delta and to its end otherwise. Both ends clamp. */
static inline void zcsr_text_move_cursor(zcsr_text_edit *e, int delta)
{
    if (!e || !e->data) return;
    if (zcsr_text_has_selection(e)) {
        size_t start = 0u, end = 0u;
        zcsr_text_selection_bounds(e, &start, &end);
        e->cursor = delta < 0 ? start : end;
        e->anchor = e->cursor;
        return;
    }
    while (delta < 0 && e->cursor > 0u) {
        e->cursor = zcsr_text_utf8_prev(e->data, e->cursor);
        delta++;
    }
    while (delta > 0 && e->cursor < e->length) {
        e->cursor = zcsr_text_utf8_next(e->data, e->length, e->cursor);
        delta--;
    }
    e->anchor = e->cursor;
}

static inline void zcsr_text_cursor_home(zcsr_text_edit *e)
{
    if (!e) return;
    e->cursor = 0u;
    e->anchor = 0u;
}

static inline void zcsr_text_cursor_end(zcsr_text_edit *e)
{
    if (!e) return;
    e->cursor = e->length;
    e->anchor = e->length;
}

/* Place the caret; pos must be 0..length and on a code point boundary. */
static inline zcsr_text_status zcsr_text_set_cursor(zcsr_text_edit *e, size_t pos)
{
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (pos > e->length) return ZCSR_TEXT_ERR_RANGE;
    if (!zcsr_text_on_boundary(e->data, e->length, pos)) return ZCSR_TEXT_ERR_ARGUMENT;
    e->cursor = pos;
    e->anchor = pos;
    return ZCSR_TEXT_OK;
}

static inline void zcsr_text_select_all(zcsr_text_edit *e)
{
    if (!e) return;
    e->anchor = 0u;
    e->cursor = e->length;
}

/* Explicit selection: both offsets must be 0..length and on boundaries. */
static inline zcsr_text_status zcsr_text_set_selection(zcsr_text_edit *e, size_t start,
                                                       size_t end)
{
    if (!e || !e->data) return ZCSR_TEXT_ERR_ARGUMENT;
    if (start > e->length || end > e->length) return ZCSR_TEXT_ERR_RANGE;
    if (!zcsr_text_on_boundary(e->data, e->length, start) ||
        !zcsr_text_on_boundary(e->data, e->length, end))
        return ZCSR_TEXT_ERR_ARGUMENT;
    e->anchor = start;
    e->cursor = end;
    return ZCSR_TEXT_OK;
}

#ifdef __cplusplus
}
#endif
#endif /* ZCSR_UI_TEXT_EDIT_H */
