#ifndef ZCSR_UI_TYPES_H
#define ZCSR_UI_TYPES_H
/* ZCSR UI Runtime — public type definitions.
 * This header is the single source of truth for result codes, identities,
 * geometry, colors and node flags shared across all primitives. */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Result codes ---- */
typedef enum ZCSR_Result {
    ZCSR_OK = 0,
    ZCSR_ERR_INVALID_ARGUMENT,
    ZCSR_ERR_NULL_POINTER,
    ZCSR_ERR_OUT_OF_MEMORY,
    ZCSR_ERR_NOT_FOUND,
    ZCSR_ERR_ALREADY_EXISTS,
    ZCSR_ERR_INVALID_STATE,
    ZCSR_ERR_UNSUPPORTED,
    ZCSR_ERR_BUFFER_TOO_SMALL,
    ZCSR_ERR_QUEUE_FULL,
    ZCSR_ERR_TYPE_MISMATCH
} ZCSR_Result;

/* ---- Identities ---- */
typedef uint32_t ZCSR_NodeId;
typedef uint32_t ZCSR_ComponentId;
typedef uint32_t ZCSR_EventType;
typedef uint32_t ZCSR_PropertyId;

#define ZCSR_NODE_INVALID ((ZCSR_NodeId)0u)

/* ---- Component IDs (stable, do not renumber) ---- */
typedef enum ZCSR_Component {
    ZCSR_COMPONENT_PANEL       = 0x5A430001u,
    ZCSR_COMPONENT_BUTTON      = 0x5A430002u,
    ZCSR_COMPONENT_TOGGLE      = 0x5A430003u,
    ZCSR_COMPONENT_SLIDER      = 0x5A430004u,
    ZCSR_COMPONENT_DROPDOWN    = 0x5A430005u,
    ZCSR_COMPONENT_LABEL       = 0x5A430006u,
    ZCSR_COMPONENT_SEPARATOR   = 0x5A430007u,
    ZCSR_COMPONENT_IMAGE       = 0x5A430008u,
    ZCSR_COMPONENT_TEXT_INPUT  = 0x5A430009u,
    ZCSR_COMPONENT_SCROLL_VIEW = 0x5A43000Au
} ZCSR_Component;

/* ---- Geometry ---- */
typedef struct ZCSR_Vec2 {
    float x, y;
} ZCSR_Vec2;

typedef struct ZCSR_Rect {
    float x, y, width, height;
} ZCSR_Rect;

typedef struct ZCSR_Color {
    float r, g, b, a;
} ZCSR_Color;

/* ---- Node flags ---- */
typedef enum ZCSR_NodeFlags {
    ZCSR_NODE_VISIBLE   = 1u << 0,
    ZCSR_NODE_ENABLED   = 1u << 1,
    ZCSR_NODE_FOCUSABLE = 1u << 2,
    ZCSR_NODE_CLIP      = 1u << 3,
    ZCSR_NODE_LOCKED    = 1u << 4
} ZCSR_NodeFlags;

/* ---- Alignment ---- */
typedef enum ZCSR_Alignment {
    ZCSR_ALIGN_START = 0,
    ZCSR_ALIGN_CENTER,
    ZCSR_ALIGN_END
} ZCSR_Alignment;

/* ---- Image fit ---- */
typedef enum ZCSR_ImageFit {
    ZCSR_IMAGE_STRETCH = 0,
    ZCSR_IMAGE_CONTAIN,
    ZCSR_IMAGE_COVER
} ZCSR_ImageFit;

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_TYPES_H */
