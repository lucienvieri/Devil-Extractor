// Filter Plugin Infrastructure
// See docs/devil-filters.md for full documentation.
//
// Filters extend the DOM walker via pre-filter, text transform, and
// post-filter function pointers in walk_ctx_t — zero overhead when NULL.

#ifndef DEVIL_FILTERS_H
#define DEVIL_FILTERS_H

#include <stdbool.h>
#include <stdlib.h>
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

/* Filter node — chains pre-filters into a singly-linked list.
 * Each filter's register() prepends to ctx->filter_head. */
typedef struct filter_node {
    struct filter_node *next;
    devil_pre_filter_fn fn;
} filter_node_t;

/* walk_ctx_t extended with filter hooks (NULL = no-op).
 * Base fields match devil.c's layout for ABI compatibility. */
typedef struct devil_walk_ctx {
    void     *out;          /* lexbor_str_t* */
    void     *mraw;         /* lexbor_mraw_t* */
    bool      has_output;   /* true if we've emitted text */
    bool      in_block;     /* true if last node was block */
    bool      skip;         /* set by pre-filters to prune subtree */

    devil_pre_filter_fn  pre_filter;
    devil_text_transform_fn text_transform;
    devil_post_filter_fn post_filter;

    /* Filter chaining list (prepended by each register call). */
    filter_node_t       *filter_head;
} walk_ctx_t;

/* Register a pre-filter at the head of the chain. */
static inline void
devil_pre_filter_register(walk_ctx_t *ctx, devil_pre_filter_fn fn)
{
    /* Caller must have malloc'd a filter_node_t — we free it in devil.c */
    filter_node_t *node = malloc(sizeof(filter_node_t));
    if (!node) return;
    node->fn   = fn;
    node->next = ctx->filter_head;
    ctx->filter_head = node;
}

/* Call all registered pre-filters; returns true if any pruned the subtree. */
static inline bool
devil_pre_filter_call(walk_ctx_t *ctx, lxb_dom_node_t *node, lxb_tag_id_t tag)
{
    filter_node_t *n;
    for (n = ctx->filter_head; n; n = n->next) {
        if (n->fn(node, tag, ctx))
            return true;
    }
    return false;
}

/* Free the entire filter chain. */
static inline void
devil_pre_filter_free(walk_ctx_t *ctx)
{
    filter_node_t *n, *next;
    for (n = ctx->filter_head; n; n = next) {
        next = n->next;
        free(n);
    }
    ctx->filter_head = NULL;
}

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
