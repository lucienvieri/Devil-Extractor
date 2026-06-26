// Filter Plugin Infrastructure
// See docs/devil-filters.md for full documentation.
//
// Filters extend the DOM walker via pre-filter, text transform, and
// post-filter function pointers in walk_ctx_t — zero overhead when NULL.

#ifndef DEVIL_FILTERS_H
#define DEVIL_FILTERS_H

#include <stdbool.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/tag/tag.h>

/* Filter function pointers — called at three extension points during DOM walk.
 * A filter sets ctx->skip = true to signal subtree pruning. */

typedef bool (*devil_pre_filter_fn)(lxb_dom_node_t *node,
                                    lxb_tag_id_t tag,
                                    void *ctx);

typedef bool (*devil_text_transform_fn)(lxb_dom_node_t *node,
                                         void *ctx);

typedef void (*devil_post_filter_fn)(lxb_dom_node_t *node,
                                      lxb_tag_id_t tag,
                                      void *ctx);

/* walk_ctx_t extended with filter hooks (NULL = no-op).
 * Base fields match devil.c's layout for ABI compatibility. */
typedef struct devil_walk_ctx {
    void     *out;        /* lexbor_str_t* */
    void     *mraw;       /* lexbor_mraw_t* */
    bool      has_output; /* true if we've emitted text */
    bool      in_block;   /* true if last node was block */
    bool      skip;       /* set by pre-filters to prune subtree */

    devil_pre_filter_fn  pre_filter;
    devil_text_transform_fn text_transform;
    devil_post_filter_fn post_filter;
} walk_ctx_t;

/* Noise tags — shared table used by devil.c and filter plugins. */
#define DEVIL_NOISE_TAGS \
    XX(SCRIPT)  XX(STYLE)  XX(NAV)     XX(ASIDE)  XX(FOOTER) \
    XX(HEADER)  XX(NOSCRIPT) XX(IFRAME) XX(FORM)  XX(SELECT) \
    XX(HEAD)    XX(META)   XX(LINK)    XX(BUTTON) XX(EMBED) \
    XX(OBJECT)  XX(SOURCE) XX(TRACK)   XX(MAP)    XX(BASE)  \
    XX(AREA)    XX(MARQUEE)

/* Block-level tags — shared by filters for block-boundary newline detection. */
#define DEVIL_BLOCK_TAGS \
    XX(DIV)   XX(P)       XX(H1) XX(H2) XX(H3) XX(H4) XX(H5) XX(H6) \
    XX(UL)    XX(OL)      XX(LI) XX(DL) XX(DT) XX(DD) XX(FIGURE) \
    XX(FIGCAPTION) \
    XX(ARTICLE) XX(SECTION) XX(ADDRESS) \
    XX(BLOCKQUOTE) XX(PRE) XX(HR) XX(BR) XX(CAPTION) \
    XX(TABLE) XX(TH) XX(TD) XX(TR) XX(THEAD) XX(TBODY) XX(TFOOT)

/* _devil_is_noise() — check if a tag ID is a noise tag (shared). */
static inline bool
_devil_is_noise(lxb_tag_id_t tag)
{
    switch (tag) {
#define XX(t) case LXB_TAG_##t: return true;
        DEVIL_NOISE_TAGS
#undef XX
        default: return false;
    }
}

/* _devil_is_block() — check if a tag ID is a block-level tag (shared). */
static inline bool
_devil_is_block(lxb_tag_id_t tag)
{
    switch (tag) {
#define XX(t) case LXB_TAG_##t: return true;
        DEVIL_BLOCK_TAGS
#undef XX
        default: return false;
    }
}

#endif /* DEVIL_FILTERS_H */
